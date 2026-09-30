// Copyright Jokate. All Rights Reserved.

#include "YSTagUsageIndex.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "YSTagUsageExtractor.h"

namespace
{
	constexpr int32 MaxInFlight = 4;
	constexpr double TickBudgetSeconds = 0.008;
	constexpr double BroadcastInterval = 0.5;
	constexpr float CacheSaveDelay = 2.0f;

	IAssetRegistry& GetRegistry()
	{
		return FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	}
}

FYSTagUsageIndex::FYSTagUsageIndex()
{
	IAssetRegistry& Registry = GetRegistry();
	SavedHandle = UPackage::PackageSavedWithContextEvent.AddRaw(this, &FYSTagUsageIndex::OnPackageSaved);
	RemovedHandle = Registry.OnAssetRemoved().AddRaw(this, &FYSTagUsageIndex::OnAssetRemoved);
	RenamedHandle = Registry.OnAssetRenamed().AddRaw(this, &FYSTagUsageIndex::OnAssetRenamed);

	// 스캔이 끝나기 전에도 지난 결과를 보여줄 수 있게 캐시부터 올린다.
	LoadCache();

	if (Registry.IsLoadingAssets())
	{
		FilesLoadedHandle = Registry.OnFilesLoaded().AddLambda([this]() { BeginScan(false); });
	}
	else
	{
		BeginScan(false);
	}
}

FYSTagUsageIndex::~FYSTagUsageIndex()
{
	FTSTicker::RemoveTicker(TickHandle);
	if (SaveHandle.IsValid())
	{
		FTSTicker::RemoveTicker(SaveHandle);
		SaveCache();
	}

	UPackage::PackageSavedWithContextEvent.Remove(SavedHandle);
	if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetRegistry")))
	{
		IAssetRegistry& Registry = GetRegistry();
		Registry.OnAssetRemoved().Remove(RemovedHandle);
		Registry.OnAssetRenamed().Remove(RenamedHandle);
		Registry.OnFilesLoaded().Remove(FilesLoadedHandle);
	}
}

void FYSTagUsageIndex::Rescan()
{
	BeginScan(true);
}

void FYSTagUsageIndex::BeginScan(bool bIgnoreCache)
{
	if (bIgnoreCache)
	{
		Packages.Reset();
	}

	FGameplayTagContainer AllTags;
	UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, false);

	// 소스 스캔은 수백 파일 텍스트라 동기로 충분하다.
	ScanCode(AllTags);

	const TSet<FName> Candidates = CollectCandidates(AllTags);
	for (auto It = Packages.CreateIterator(); It; ++It)
	{
		if (!Candidates.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}

	Pending.Reset();
	for (const FName PackageName : Candidates)
	{
		const FYSPackageUsage* Cached = Packages.Find(PackageName);
		const FString Key = GetSavedKey(PackageName);
		if (!Cached || Key.IsEmpty() || Cached->SavedKey != Key)
		{
			Pending.Add(PackageName);
		}
	}

	ScanDone = 0;
	ScanTotal = Pending.Num();
	bCountsDirty = true;

	if (ScanTotal > 0)
	{
		EnsureTicking();
	}
	else
	{
		ScheduleCacheSave();
	}
	ChangedEvent.Broadcast();
}

void FYSTagUsageIndex::ScanCode(const FGameplayTagContainer& AllTags)
{
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	TSet<FName> KnownTags;
	NativeTags.Reset();
	for (const FGameplayTag& Tag : AllTags)
	{
		KnownTags.Add(Tag.GetTagName());

		FString Comment;
		TArray<FName> Sources;
		bool bExplicit = false, bRestricted = false, bAllowNonRestrictedChildren = false;
		Manager.GetTagEditorData(Tag.GetTagName(), Comment, Sources, bExplicit, bRestricted, bAllowNonRestrictedChildren);
		for (const FName SourceName : Sources)
		{
			const FGameplayTagSource* Source = Manager.FindTagSource(SourceName);
			if (Source && Source->SourceType == EGameplayTagSourceType::Native)
			{
				NativeTags.Add(Tag.GetTagName());
				break;
			}
		}
	}

	CodeRefs.Reset();
	YSTagUsage::ScanSource(KnownTags, CodeRefs);
	YSTagUsage::ScanConfig(KnownTags, CodeRefs);

	CodeCounts.Reset();
	ConfigCounts.Reset();
	for (const FYSTagCodeRef& Ref : CodeRefs)
	{
		if (Ref.bConfig)
		{
			++ConfigCounts.FindOrAdd(Ref.Tag);
		}
		else if (!Ref.bDefinition)
		{
			++CodeCounts.FindOrAdd(Ref.Tag);
		}
	}
}

void FYSTagUsageIndex::RescanSource()
{
	FGameplayTagContainer AllTags;
	UGameplayTagsManager::Get().RequestAllGameplayTags(AllTags, false);
	ScanCode(AllTags);
	ChangedEvent.Broadcast();
}

void FYSTagUsageIndex::GetCodeRefsForTags(const TSet<FName>& Tags, TArray<FYSTagCodeRef>& OutRefs) const
{
	for (const FYSTagCodeRef& Ref : CodeRefs)
	{
		if (Tags.Contains(Ref.Tag))
		{
			OutRefs.Add(Ref);
		}
	}
}

TSet<FName> FYSTagUsageIndex::CollectCandidates(const FGameplayTagContainer& AllTags) const
{
	// 태그를 저장하는 애셋은 레지스트리에 태그 이름을 SearchableName 으로 남긴다(FGameplayTag::Serialize).
	// 이걸로 로드할 패키지를 먼저 추린다.
	IAssetRegistry& Registry = GetRegistry();
	TSet<FName> Result;
	TArray<FAssetIdentifier> Referencers;
	for (const FGameplayTag& Tag : AllTags)
	{
		Referencers.Reset();
		Registry.GetReferencers(FAssetIdentifier(FGameplayTag::StaticStruct(), Tag.GetTagName()), Referencers,
			UE::AssetRegistry::EDependencyCategory::SearchableName);
		for (const FAssetIdentifier& Referencer : Referencers)
		{
			if (!Result.Contains(Referencer.PackageName) && IsScannable(Referencer.PackageName))
			{
				Result.Add(Referencer.PackageName);
			}
		}
	}
	return Result;
}

bool FYSTagUsageIndex::IsScannable(FName PackageName) const
{
	const FString Name = PackageName.ToString();
	if (!Name.StartsWith(TEXT("/Game/")) || Name.Contains(TEXT("/__External")))
	{
		return false;
	}

	// 레벨은 로드하면 월드가 통째로 올라온다.
	TArray<FAssetData> Assets;
	GetRegistry().GetAssetsByPackageName(PackageName, Assets);
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetClassPath == UWorld::StaticClass()->GetClassPathName())
		{
			return false;
		}
	}
	return true;
}

FString FYSTagUsageIndex::GetSavedKey(FName PackageName) const
{
	if (const TOptional<FAssetPackageData> Data = GetRegistry().GetAssetPackageDataCopy(PackageName))
	{
		const FIoHash Hash = Data->GetPackageSavedHash();
		if (!Hash.IsZero())
		{
			return LexToString(Hash);
		}
	}

	FString Filename;
	if (FPackageName::TryConvertLongPackageNameToFilename(PackageName.ToString(), Filename, FPackageName::GetAssetPackageExtension()))
	{
		const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Filename);
		if (Stamp != FDateTime::MinValue())
		{
			return FString::Printf(TEXT("t%lld:%lld"), Stamp.GetTicks(), IFileManager::Get().FileSize(*Filename));
		}
	}
	return FString();
}

void FYSTagUsageIndex::EnsureTicking()
{
	if (!TickHandle.IsValid())
	{
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FYSTagUsageIndex::Tick));
	}
}

bool FYSTagUsageIndex::Tick(float /*DeltaTime*/)
{
	const double Start = FPlatformTime::Seconds();
	while (Pending.Num() > 0 && InFlight.Num() < MaxInFlight && FPlatformTime::Seconds() - Start < TickBudgetSeconds)
	{
		RequestLoad(Pending.Pop(EAllowShrinking::No));
	}

	const bool bFinished = Pending.IsEmpty() && InFlight.IsEmpty();
	const double Now = FPlatformTime::Seconds();
	if (bFinished || (bBroadcastPending && Now - LastBroadcastTime > BroadcastInterval))
	{
		bBroadcastPending = false;
		LastBroadcastTime = Now;
		ChangedEvent.Broadcast();
	}

	if (bFinished)
	{
		ScheduleCacheSave();
		// 스캔하느라 올린 애셋 중 아무도 안 잡고 있는 건 내린다.
		if (GEngine)
		{
			GEngine->ForceGarbageCollection();
		}
		TickHandle.Reset();
		return false;
	}
	return true;
}

void FYSTagUsageIndex::RequestLoad(FName PackageName)
{
	UPackage* Loaded = FindPackage(nullptr, *PackageName.ToString());
	if (Loaded && Loaded->IsFullyLoaded())
	{
		++ScanDone;
		ProcessPackage(PackageName, Loaded);
		return;
	}

	InFlight.Add(PackageName);
	TWeakPtr<bool> WeakAlive = Alive;
	LoadPackageAsync(PackageName.ToString(), FLoadPackageAsyncDelegate::CreateLambda(
		[this, WeakAlive](const FName& Name, UPackage* Package, EAsyncLoadingResult::Type Result)
		{
			if (WeakAlive.IsValid())
			{
				OnPackageLoaded(Name, Package, Result);
			}
		}));
}

void FYSTagUsageIndex::OnPackageLoaded(const FName& PackageName, UPackage* Package, EAsyncLoadingResult::Type Result)
{
	InFlight.Remove(PackageName);
	++ScanDone;

	if (Result == EAsyncLoadingResult::Succeeded && Package)
	{
		ProcessPackage(PackageName, Package);
	}
	else if (Packages.Remove(PackageName) > 0)
	{
		MarkChanged();
	}
}

void FYSTagUsageIndex::ProcessPackage(FName PackageName, UPackage* Package)
{
	// 태그가 없어도 키는 남긴다. 그래야 다음 시작 때 같은 패키지를 또 로드하지 않는다.
	FYSPackageUsage& Usage = Packages.FindOrAdd(PackageName);
	Usage.SavedKey = GetSavedKey(PackageName);
	Usage.Records.Reset();
	YSTagUsage::ExtractPackage(Package, Usage.Records);
	MarkChanged();
}

void FYSTagUsageIndex::OnPackageSaved(const FString& /*Filename*/, UPackage* Package, FObjectPostSaveContext Context)
{
	if (!Package || Context.IsProceduralSave() || Context.IsCooking())
	{
		return;
	}

	const FName PackageName = Package->GetFName();
	if (!IsScannable(PackageName))
	{
		return;
	}

	TArray<FYSTagUsageRecord> Records;
	YSTagUsage::ExtractPackage(Package, Records);
	if (Records.IsEmpty() && !Packages.Contains(PackageName))
	{
		return;
	}

	Pending.Remove(PackageName);
	FYSPackageUsage& Usage = Packages.FindOrAdd(PackageName);
	Usage.Records = MoveTemp(Records);
	// 저장 직후엔 레지스트리가 새 해시를 아직 모를 수 있다. 캐시를 쓸 때 다시 읽는다.
	KeysToRefresh.Add(PackageName);
	MarkChanged();
	ScheduleCacheSave();
}

void FYSTagUsageIndex::OnAssetRemoved(const FAssetData& AssetData)
{
	Pending.Remove(AssetData.PackageName);
	if (Packages.Remove(AssetData.PackageName) > 0)
	{
		MarkChanged();
		ScheduleCacheSave();
	}
}

void FYSTagUsageIndex::OnAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath)
{
	const FName OldPackageName = FSoftObjectPath(OldObjectPath).GetLongPackageFName();
	Pending.Remove(OldPackageName);
	if (Packages.Remove(OldPackageName) == 0)
	{
		return;
	}

	if (IsScannable(AssetData.PackageName))
	{
		Pending.AddUnique(AssetData.PackageName);
		++ScanTotal;
		EnsureTicking();
	}
	MarkChanged();
	ScheduleCacheSave();
}

void FYSTagUsageIndex::MarkChanged()
{
	bCountsDirty = true;
	if (TickHandle.IsValid())
	{
		// 스캔 중엔 Tick 에서 몰아서 알린다.
		bBroadcastPending = true;
	}
	else
	{
		ChangedEvent.Broadcast();
	}
}

bool FYSTagUsageIndex::IsScanning() const
{
	return TickHandle.IsValid();
}

const TMap<FName, int32>& FYSTagUsageIndex::GetDirectCounts() const
{
	if (bCountsDirty)
	{
		DirectCounts.Reset();
		for (const TPair<FName, FYSPackageUsage>& Pair : Packages)
		{
			for (const FYSTagUsageRecord& Record : Pair.Value.Records)
			{
				++DirectCounts.FindOrAdd(Record.Tag);
			}
		}
		bCountsDirty = false;
	}
	return DirectCounts;
}

void FYSTagUsageIndex::GetRecordsForTags(const TSet<FName>& Tags, TArray<FYSTagUsageRecord>& OutRecords) const
{
	for (const TPair<FName, FYSPackageUsage>& Pair : Packages)
	{
		for (const FYSTagUsageRecord& Record : Pair.Value.Records)
		{
			if (Tags.Contains(Record.Tag))
			{
				OutRecords.Add(Record);
			}
		}
	}
}

void FYSTagUsageIndex::GetRecordsForPackages(const TSet<FName>& PackageNames, TArray<FYSTagUsageRecord>& OutRecords) const
{
	for (const FName PackageName : PackageNames)
	{
		if (const FYSPackageUsage* Usage = Packages.Find(PackageName))
		{
			OutRecords.Append(Usage->Records);
		}
	}
}

void FYSTagUsageIndex::ScheduleCacheSave()
{
	if (SaveHandle.IsValid())
	{
		return;
	}
	SaveHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this](float)
	{
		SaveHandle.Reset();
		SaveCache();
		return false;
	}), CacheSaveDelay);
}

FString FYSTagUsageIndex::GetCachePath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("YSTagUsage"), TEXT("Cache.json"));
}

void FYSTagUsageIndex::LoadCache()
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *GetCachePath()))
	{
		return;
	}

	TSharedPtr<FJsonObject> RootJson;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), RootJson) || !RootJson.IsValid())
	{
		return;
	}
	if (static_cast<int32>(RootJson->GetNumberField(TEXT("version"))) != CacheVersion)
	{
		return;
	}

	const TSharedPtr<FJsonObject>* PackagesJson = nullptr;
	if (!RootJson->TryGetObjectField(TEXT("packages"), PackagesJson))
	{
		return;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& PackagePair : (*PackagesJson)->Values)
	{
		const TSharedPtr<FJsonObject> PackageJson = PackagePair.Value->AsObject();
		if (!PackageJson.IsValid())
		{
			continue;
		}

		const FName PackageName(*PackagePair.Key);
		FYSPackageUsage& Usage = Packages.Add(PackageName);
		Usage.SavedKey = PackageJson->GetStringField(TEXT("key"));

		const TArray<TSharedPtr<FJsonValue>>* RecordsJson = nullptr;
		if (!PackageJson->TryGetArrayField(TEXT("records"), RecordsJson))
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& RecordValue : *RecordsJson)
		{
			const TSharedPtr<FJsonObject> RecordJson = RecordValue->AsObject();
			if (!RecordJson.IsValid())
			{
				continue;
			}

			FYSTagUsageRecord& Record = Usage.Records.AddDefaulted_GetRef();
			Record.Tag = FName(*RecordJson->GetStringField(TEXT("tag")));
			Record.PackageName = PackageName;
			Record.Asset = FSoftObjectPath(RecordJson->GetStringField(TEXT("asset")));
			Record.Root = FSoftObjectPath(RecordJson->GetStringField(TEXT("root")));
			Record.TypeName = RecordJson->GetStringField(TEXT("type"));
			Record.DisplayPath = RecordJson->GetStringField(TEXT("display"));
			Record.bInherited = RecordJson->GetBoolField(TEXT("inherited"));

			const TArray<TSharedPtr<FJsonValue>>* PathJson = nullptr;
			if (RecordJson->TryGetArrayField(TEXT("path"), PathJson))
			{
				for (const TSharedPtr<FJsonValue>& SegmentValue : *PathJson)
				{
					// [kind, property, index]
					const TArray<TSharedPtr<FJsonValue>>& Parts = SegmentValue->AsArray();
					if (Parts.Num() == 3)
					{
						Record.Path.Add({
							static_cast<EYSTagPathKind>(static_cast<int32>(Parts[0]->AsNumber())),
							FName(*Parts[1]->AsString()),
							static_cast<int32>(Parts[2]->AsNumber()) });
					}
				}
			}
		}
	}
	bCountsDirty = true;
}

void FYSTagUsageIndex::SaveCache()
{
	for (const FName PackageName : KeysToRefresh)
	{
		if (FYSPackageUsage* Usage = Packages.Find(PackageName))
		{
			Usage->SavedKey = GetSavedKey(PackageName);
		}
	}
	KeysToRefresh.Reset();

	TSharedRef<FJsonObject> PackagesJson = MakeShared<FJsonObject>();
	for (const TPair<FName, FYSPackageUsage>& PackagePair : Packages)
	{
		TArray<TSharedPtr<FJsonValue>> RecordsJson;
		for (const FYSTagUsageRecord& Record : PackagePair.Value.Records)
		{
			TArray<TSharedPtr<FJsonValue>> PathJson;
			for (const FYSTagPathSegment& Segment : Record.Path)
			{
				TArray<TSharedPtr<FJsonValue>> Parts;
				Parts.Add(MakeShared<FJsonValueNumber>(static_cast<int32>(Segment.Kind)));
				Parts.Add(MakeShared<FJsonValueString>(Segment.Property.ToString()));
				Parts.Add(MakeShared<FJsonValueNumber>(Segment.Index));
				PathJson.Add(MakeShared<FJsonValueArray>(Parts));
			}

			TSharedRef<FJsonObject> RecordJson = MakeShared<FJsonObject>();
			RecordJson->SetStringField(TEXT("tag"), Record.Tag.ToString());
			RecordJson->SetStringField(TEXT("asset"), Record.Asset.ToString());
			RecordJson->SetStringField(TEXT("root"), Record.Root.ToString());
			RecordJson->SetStringField(TEXT("type"), Record.TypeName);
			RecordJson->SetStringField(TEXT("display"), Record.DisplayPath);
			RecordJson->SetBoolField(TEXT("inherited"), Record.bInherited);
			RecordJson->SetArrayField(TEXT("path"), PathJson);
			RecordsJson.Add(MakeShared<FJsonValueObject>(RecordJson));
		}

		TSharedRef<FJsonObject> PackageJson = MakeShared<FJsonObject>();
		PackageJson->SetStringField(TEXT("key"), PackagePair.Value.SavedKey);
		PackageJson->SetArrayField(TEXT("records"), RecordsJson);
		PackagesJson->SetObjectField(PackagePair.Key.ToString(), PackageJson);
	}

	TSharedRef<FJsonObject> RootJson = MakeShared<FJsonObject>();
	RootJson->SetNumberField(TEXT("version"), CacheVersion);
	RootJson->SetObjectField(TEXT("packages"), PackagesJson);

	FString Text;
	FJsonSerializer::Serialize(RootJson, TJsonWriterFactory<>::Create(&Text));
	FFileHelper::SaveStringToFile(Text, *GetCachePath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

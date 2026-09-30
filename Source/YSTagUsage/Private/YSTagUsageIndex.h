// Copyright Jokate. All Rights Reserved.

#pragma once

#include "Containers/Ticker.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/UObjectGlobals.h"
#include "YSTagCodeScanner.h"
#include "YSTagUsageTypes.h"

struct FAssetData;
struct FGameplayTagContainer;

class FYSTagUsageIndex
{
public:
	FYSTagUsageIndex();
	~FYSTagUsageIndex();

	// 캐시를 버리고 전부 다시 스캔한다.
	void Rescan();

	bool IsScanning() const;
	int32 GetScanDone() const { return ScanDone; }
	int32 GetScanTotal() const { return ScanTotal; }

	const TMap<FName, int32>& GetDirectCounts() const;
	void GetRecordsForTags(const TSet<FName>& Tags, TArray<FYSTagUsageRecord>& OutRecords) const;
	void GetRecordsForPackages(const TSet<FName>& PackageNames, TArray<FYSTagUsageRecord>& OutRecords) const;

	// 소스·설정만 다시 훑는다. 태그를 만들거나 지운 뒤에 쓴다.
	void RescanSource();

	// C++ 사용처 수. 정의 줄은 세지 않는다.
	const TMap<FName, int32>& GetCodeCounts() const { return CodeCounts; }
	const TMap<FName, int32>& GetConfigCounts() const { return ConfigCounts; }
	void GetCodeRefsForTags(const TSet<FName>& Tags, TArray<FYSTagCodeRef>& OutRefs) const;
	// 태그 매니저 기준 C++ 네이티브 태그(엔진·플러그인 포함)
	bool IsNativeTag(FName Tag) const { return NativeTags.Contains(Tag); }

	FSimpleMulticastDelegate& OnChanged() { return ChangedEvent; }

private:
	void BeginScan(bool bIgnoreCache);
	void ScanCode(const FGameplayTagContainer& AllTags);
	TSet<FName> CollectCandidates(const FGameplayTagContainer& AllTags) const;
	bool IsScannable(FName PackageName) const;
	FString GetSavedKey(FName PackageName) const;

	void EnsureTicking();
	bool Tick(float DeltaTime);
	void RequestLoad(FName PackageName);
	void OnPackageLoaded(const FName& PackageName, UPackage* Package, EAsyncLoadingResult::Type Result);
	void ProcessPackage(FName PackageName, UPackage* Package);

	void OnPackageSaved(const FString& Filename, UPackage* Package, FObjectPostSaveContext Context);
	void OnAssetRemoved(const FAssetData& AssetData);
	void OnAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);

	void MarkChanged();
	void ScheduleCacheSave();
	void LoadCache();
	void SaveCache();
	FString GetCachePath() const;

	TMap<FName, FYSPackageUsage> Packages;
	TSet<FName> KeysToRefresh;

	TArray<FYSTagCodeRef> CodeRefs;
	TMap<FName, int32> CodeCounts;
	TMap<FName, int32> ConfigCounts;
	TSet<FName> NativeTags;

	TArray<FName> Pending;
	TSet<FName> InFlight;
	int32 ScanDone = 0;
	int32 ScanTotal = 0;
	double LastBroadcastTime = 0.0;
	bool bBroadcastPending = false;

	mutable TMap<FName, int32> DirectCounts;
	mutable bool bCountsDirty = true;

	FTSTicker::FDelegateHandle TickHandle;
	FTSTicker::FDelegateHandle SaveHandle;
	FDelegateHandle FilesLoadedHandle;
	FDelegateHandle SavedHandle;
	FDelegateHandle RemovedHandle;
	FDelegateHandle RenamedHandle;

	FSimpleMulticastDelegate ChangedEvent;

	// 비동기 로드 콜백이 파괴 뒤에 도착할 수 있다.
	TSharedRef<bool> Alive = MakeShared<bool>(true);

	// 추출 규칙이 바뀌면 올린다. 다르면 캐시를 통째로 버린다.
	static constexpr int32 CacheVersion = 1;
};

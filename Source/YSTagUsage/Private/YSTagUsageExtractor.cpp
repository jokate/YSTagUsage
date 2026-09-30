// Copyright Jokate. All Rights Reserved.

#include "YSTagUsageExtractor.h"

#include "Engine/Blueprint.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"

namespace YSTagUsage
{
namespace
{
	const TCHAR* ObjectSeparator = TEXT(" › ");

	struct FHit
	{
		FName Tag;
		TArray<FYSTagPathSegment> Path;
		FString DisplayPath;
	};

	FString MakeHitKey(const FName Tag, const TArray<FYSTagPathSegment>& Path)
	{
		FString Key = Tag.ToString();
		for (const FYSTagPathSegment& Segment : Path)
		{
			Key.Appendf(TEXT("/%d:%s:%d"), static_cast<int32>(Segment.Kind), *Segment.Property.ToString(), Segment.Index);
		}
		return Key;
	}

	bool ShouldVisit(const FProperty* Property)
	{
		// 사람이 세팅하는 값만 센다. VisibleAnywhere(EditConst) 는 GE 의 CombinedTags 처럼 파생 값이다.
		return Property->HasAnyPropertyFlags(CPF_Edit)
			&& !Property->HasAnyPropertyFlags(CPF_EditConst | CPF_Transient | CPF_Deprecated);
	}

	class FWalker
	{
	public:
		explicit FWalker(const UObject* InRoot) : Root(InRoot) {}

		void Run() { WalkObject(Root); }

		const UObject* Root;
		TArray<FHit> Hits;
		TSet<const UObject*> Visited;

	private:
		TArray<FYSTagPathSegment> Path;
		FString Label;

		void AddHit(const FGameplayTag& Tag)
		{
			if (Tag.IsValid())
			{
				Hits.Add({ Tag.GetTagName(), Path, Label });
			}
		}

		void WalkObject(const UObject* Object)
		{
			Visited.Add(Object);
			WalkStruct(Object->GetClass(), Object);
		}

		void WalkStruct(const UStruct* Struct, const void* Data)
		{
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				const FProperty* Property = *It;
				if (!ShouldVisit(Property))
				{
					continue;
				}

				for (int32 StaticIndex = 0; StaticIndex < Property->ArrayDim; ++StaticIndex)
				{
					const bool bStaticArray = Property->ArrayDim > 1;
					const int32 SavedLen = Label.Len();
					if (!Label.IsEmpty() && !Label.EndsWith(ObjectSeparator))
					{
						Label += TEXT(".");
					}
					Label += Property->GetDisplayNameText().ToString();
					if (bStaticArray)
					{
						Label += FString::Printf(TEXT("[%d]"), StaticIndex);
					}

					Path.Add({ EYSTagPathKind::Field, Property->GetFName(), bStaticArray ? StaticIndex : INDEX_NONE });
					WalkValue(Property, Property->ContainerPtrToValuePtr<void>(Data, StaticIndex));
					Path.Pop();
					Label.LeftInline(SavedLen);
				}
			}
		}

		void WalkElement(EYSTagPathKind Kind, const FProperty* Inner, const void* Value, int32 Index, const FString& Suffix)
		{
			const int32 SavedLen = Label.Len();
			Label += Suffix;
			Path.Add({ Kind, NAME_None, Index });
			WalkValue(Inner, Value);
			Path.Pop();
			Label.LeftInline(SavedLen);
		}

		void WalkValue(const FProperty* Property, const void* Value)
		{
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				const UScriptStruct* Struct = StructProperty->Struct;
				if (Struct == FGameplayTag::StaticStruct())
				{
					AddHit(*static_cast<const FGameplayTag*>(Value));
				}
				else if (Struct == FGameplayTagContainer::StaticStruct())
				{
					// ParentTags 는 파생 값이라 명시된 태그만 센다.
					for (const FGameplayTag& Tag : *static_cast<const FGameplayTagContainer*>(Value))
					{
						AddHit(Tag);
					}
				}
				else if (Struct == FGameplayTagQuery::StaticStruct())
				{
					for (const FGameplayTag& Tag : static_cast<const FGameplayTagQuery*>(Value)->GetGameplayTagArray())
					{
						AddHit(Tag);
					}
				}
				else if (Struct == FInstancedStruct::StaticStruct())
				{
					const FInstancedStruct& Instanced = *static_cast<const FInstancedStruct*>(Value);
					if (Instanced.IsValid())
					{
						WalkStruct(Instanced.GetScriptStruct(), Instanced.GetMemory());
					}
				}
				else
				{
					WalkStruct(Struct, Value);
				}
			}
			else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				FScriptArrayHelper Helper(ArrayProperty, Value);
				for (int32 Index = 0; Index < Helper.Num(); ++Index)
				{
					WalkElement(EYSTagPathKind::Element, ArrayProperty->Inner, Helper.GetRawPtr(Index), Index,
						FString::Printf(TEXT("[%d]"), Index));
				}
			}
			else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
			{
				FScriptSetHelper Helper(SetProperty, Value);
				for (FScriptSetHelper::FIterator It(Helper); It; ++It)
				{
					const int32 Index = It.GetLogicalIndex();
					WalkElement(EYSTagPathKind::Element, SetProperty->ElementProp, Helper.GetElementPtr(It), Index,
						FString::Printf(TEXT("[%d]"), Index));
				}
			}
			else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
			{
				FScriptMapHelper Helper(MapProperty, Value);
				for (FScriptMapHelper::FIterator It(Helper); It; ++It)
				{
					const int32 Index = It.GetLogicalIndex();
					WalkElement(EYSTagPathKind::MapKey, MapProperty->KeyProp, Helper.GetKeyPtr(It), Index,
						FString::Printf(TEXT("[%d 키]"), Index));
					WalkElement(EYSTagPathKind::Element, MapProperty->ValueProp, Helper.GetValuePtr(It), Index,
						FString::Printf(TEXT("[%d]"), Index));
				}
			}
			else if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
			{
				// 루트가 소유한 서브오브젝트만 따라간다. 다른 애셋 참조는 그 애셋 쪽에서 센다.
				const UObject* Subobject = ObjectProperty->GetObjectPropertyValue(Value);
				if (Subobject && Subobject->IsIn(Root) && !Visited.Contains(Subobject))
				{
					const int32 SavedLen = Label.Len();
					Label += FString::Printf(TEXT(" (%s)%s"), *Subobject->GetClass()->GetDisplayNameText().ToString(), ObjectSeparator);
					WalkObject(Subobject);
					Label.LeftInline(SavedLen);
				}
			}
		}
	};

	const UClass* FirstNativeClass(const UClass* Class)
	{
		while (Class && !Class->HasAnyClassFlags(CLASS_Native))
		{
			Class = Class->GetSuperClass();
		}
		return Class;
	}

	FString MakeTypeName(const UObject* Asset)
	{
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
		{
			const UClass* Native = FirstNativeClass(Blueprint->ParentClass);
			return FString::Printf(TEXT("%s (BP)"), Native ? *Native->GetName() : TEXT("?"));
		}
		const UClass* Native = FirstNativeClass(Asset->GetClass());
		return Native ? Native->GetName() : Asset->GetClass()->GetName();
	}

	// 블루프린트 컴파일 부산물(SKEL_/REINST_ 클래스와 그 CDO)
	bool IsCompileArtifact(const UObject* Object)
	{
		for (const UObject* It = Object; It; It = It->GetOuter())
		{
			const UClass* Class = It->IsA<UClass>() ? static_cast<const UClass*>(It)
				: It->HasAnyFlags(RF_ClassDefaultObject) ? It->GetClass() : nullptr;
			if (Class)
			{
				const FString Name = Class->GetName();
				if (Name.StartsWith(TEXT("SKEL_")) || Name.StartsWith(TEXT("REINST_")) || Name.StartsWith(TEXT("TRASHCLASS_")))
				{
					return true;
				}
			}
		}
		return false;
	}

	int32 OuterDepth(const UObject* Object)
	{
		int32 Depth = 0;
		for (const UObject* It = Object->GetOuter(); It; It = It->GetOuter())
		{
			++Depth;
		}
		return Depth;
	}

	const TSet<FString>& GetInheritedKeys(const UObject* Root, TMap<const UObject*, TSet<FString>>& Cache)
	{
		static const TSet<FString> Empty;
		const UObject* Archetype = Root->GetArchetype();
		if (!Archetype || Archetype == Root)
		{
			return Empty;
		}
		if (const TSet<FString>* Found = Cache.Find(Archetype))
		{
			return *Found;
		}

		FWalker Walker(Archetype);
		Walker.Run();
		TSet<FString>& Keys = Cache.Add(Archetype);
		for (const FHit& Hit : Walker.Hits)
		{
			Keys.Add(MakeHitKey(Hit.Tag, Hit.Path));
		}
		return Keys;
	}
}

void ExtractPackage(UPackage* Package, TArray<FYSTagUsageRecord>& OutRecords)
{
	TArray<UObject*> Objects;
	GetObjectsWithPackage(Package, Objects, true);

	const UObject* MainAsset = nullptr;
	TArray<TPair<const UObject*, const UObject*>> AssetRoots;
	for (const UObject* Object : Objects)
	{
		if (!Object->IsAsset())
		{
			continue;
		}
		MainAsset = MainAsset ? MainAsset : Object;

		if (const UBlueprint* Blueprint = Cast<UBlueprint>(Object))
		{
			if (Blueprint->GeneratedClass)
			{
				AssetRoots.Emplace(Blueprint->GeneratedClass->GetDefaultObject(), Object);
			}
		}
		else
		{
			AssetRoots.Emplace(Object, Object);
		}
	}
	if (!MainAsset)
	{
		return;
	}

	TMap<const UObject*, TSet<FString>> ArchetypeCache;
	TSet<const UObject*> Visited;

	auto EmitRoot = [&](const UObject* Root, const UObject* Asset, const FString& Prefix)
	{
		FWalker Walker(Root);
		Walker.Run();
		Visited.Append(Walker.Visited);
		if (Walker.Hits.IsEmpty())
		{
			return;
		}

		const TSet<FString>& InheritedKeys = GetInheritedKeys(Root, ArchetypeCache);
		const FString TypeName = MakeTypeName(Asset);
		for (FHit& Hit : Walker.Hits)
		{
			FYSTagUsageRecord& Record = OutRecords.AddDefaulted_GetRef();
			Record.Tag = Hit.Tag;
			Record.PackageName = Package->GetFName();
			Record.Asset = FSoftObjectPath(Asset);
			Record.Root = FSoftObjectPath(Root);
			Record.TypeName = TypeName;
			Record.DisplayPath = Prefix + Hit.DisplayPath;
			Record.bInherited = InheritedKeys.Contains(MakeHitKey(Hit.Tag, Hit.Path));
			Record.Path = MoveTemp(Hit.Path);
		}
	};

	for (const TPair<const UObject*, const UObject*>& Pair : AssetRoots)
	{
		EmitRoot(Pair.Key, Pair.Value, FString());
	}

	// 루트에서 프로퍼티로 닿지 않는 오브젝트(SCS 컴포넌트 템플릿 등).
	// 바깥 오브젝트부터 돌아야 안쪽 서브오브젝트를 두 번 세지 않는다.
	TArray<const UObject*> Strays;
	for (const UObject* Object : Objects)
	{
		if (!Visited.Contains(Object)
			&& !Object->IsAsset()
			&& !Object->HasAnyFlags(RF_Transient)
			&& !Object->IsA<UField>()
			&& !IsCompileArtifact(Object))
		{
			Strays.Add(Object);
		}
	}
	Strays.Sort([](const UObject& A, const UObject& B) { return OuterDepth(&A) < OuterDepth(&B); });

	for (const UObject* Stray : Strays)
	{
		if (Visited.Contains(Stray))
		{
			continue;
		}
		FString Name = Stray->GetName();
		Name.RemoveFromEnd(TEXT("_GEN_VARIABLE"));
		const FString Prefix = FString::Printf(TEXT("%s (%s)%s"), *Name, *Stray->GetClass()->GetDisplayNameText().ToString(), ObjectSeparator);
		EmitRoot(Stray, MainAsset, Prefix);
	}
}
}

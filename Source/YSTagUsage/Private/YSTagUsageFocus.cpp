// Copyright Jokate. All Rights Reserved.

#include "YSTagUsageFocus.h"

#include "BlueprintEditor.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "PropertyPath.h"
#include "StructUtils/InstancedStruct.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWindow.h"

namespace YSTagUsage
{
namespace
{
	// 디테일 뷰는 에디터가 열린 뒤 몇 프레임 지나야 채워진다.
	constexpr int32 MaxFocusAttempts = 60;

	// 직전 프로퍼티 값이 구조체·서브오브젝트면 그 안을 다음 탐색 대상으로 삼는다.
	bool DescendInto(const FProperty* Property, const void* Value, const UStruct*& OutStruct, const void*& OutData)
	{
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (StructProperty->Struct == FInstancedStruct::StaticStruct())
			{
				const FInstancedStruct& Instanced = *static_cast<const FInstancedStruct*>(Value);
				OutStruct = Instanced.GetScriptStruct();
				OutData = Instanced.GetMemory();
				return OutStruct != nullptr;
			}
			OutStruct = StructProperty->Struct;
			OutData = Value;
			return true;
		}
		if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
		{
			const UObject* Object = ObjectProperty->GetObjectPropertyValue(Value);
			OutStruct = Object ? Object->GetClass() : nullptr;
			OutData = Object;
			return Object != nullptr;
		}
		return false;
	}

	// 저장된 경로를 실제 값을 따라가며 FPropertyPath 로 되살린다.
	// 인스턴스드 서브오브젝트의 클래스는 값을 봐야만 알 수 있다.
	TSharedPtr<FPropertyPath> BuildPropertyPath(const UObject* Root, const TArray<FYSTagPathSegment>& Segments)
	{
		TSharedRef<FPropertyPath> Result = FPropertyPath::CreateEmpty();
		const UStruct* Struct = Root->GetClass();
		const void* Data = Root;
		FProperty* Current = nullptr;
		const void* CurrentValue = nullptr;

		for (const FYSTagPathSegment& Segment : Segments)
		{
			if (Segment.Kind == EYSTagPathKind::Field)
			{
				if (Current && !DescendInto(Current, CurrentValue, Struct, Data))
				{
					return nullptr;
				}
				FProperty* Property = FindFProperty<FProperty>(Struct, Segment.Property);
				if (!Property)
				{
					return nullptr;
				}
				Result = Result->ExtendPath(FPropertyInfo(Property, Segment.Index));
				Current = Property;
				CurrentValue = Property->ContainerPtrToValuePtr<void>(Data, Segment.Index == INDEX_NONE ? 0 : Segment.Index);
				continue;
			}

			FProperty* Inner = nullptr;
			const void* ElementValue = nullptr;
			if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Current))
			{
				FScriptArrayHelper Helper(ArrayProperty, CurrentValue);
				if (!Helper.IsValidIndex(Segment.Index))
				{
					return nullptr;
				}
				Inner = ArrayProperty->Inner;
				ElementValue = Helper.GetRawPtr(Segment.Index);
			}
			else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Current))
			{
				FScriptSetHelper Helper(SetProperty, CurrentValue);
				FScriptSetHelper::FIterator It(Helper, Segment.Index);
				if (!It)
				{
					return nullptr;
				}
				Inner = SetProperty->ElementProp;
				ElementValue = Helper.GetElementPtr(It);
			}
			else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Current))
			{
				FScriptMapHelper Helper(MapProperty, CurrentValue);
				FScriptMapHelper::FIterator It(Helper, Segment.Index);
				if (!It)
				{
					return nullptr;
				}
				const bool bKey = Segment.Kind == EYSTagPathKind::MapKey;
				Inner = bKey ? MapProperty->KeyProp : MapProperty->ValueProp;
				ElementValue = bKey ? Helper.GetKeyPtr(It) : Helper.GetValuePtr(It);
			}
			else
			{
				return nullptr;
			}

			Result = Result->ExtendPath(FPropertyInfo(Inner, Segment.Index));
			Current = Inner;
			CurrentValue = ElementValue;
		}
		return Result;
	}

	void CollectDetailsViews(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<IDetailsView>>& OutViews)
	{
		static const FName DetailsViewType(TEXT("SDetailsView"));
		if (Widget->GetType() == DetailsViewType)
		{
			// SDetailsView → … → IDetailsView → SCompoundWidget 로 이어지는 사슬이라 정적 캐스트가 성립한다.
			OutViews.Add(StaticCastSharedRef<IDetailsView>(Widget));
			return;
		}

		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			CollectDetailsViews(Children->GetChildAt(Index), OutViews);
		}
	}

	TSharedPtr<IDetailsView> FindDetailsViewShowing(const UObject* Object)
	{
		TArray<TSharedRef<SWindow>> Windows;
		FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);

		TArray<TSharedRef<IDetailsView>> Views;
		for (const TSharedRef<SWindow>& Window : Windows)
		{
			CollectDetailsViews(Window, Views);
		}

		for (const TSharedRef<IDetailsView>& View : Views)
		{
			for (const TWeakObjectPtr<UObject>& Selected : View->GetSelectedObjects())
			{
				if (Selected.Get() == Object)
				{
					return View;
				}
			}
		}
		return nullptr;
	}

	void FocusWhenReady(TWeakObjectPtr<UObject> WeakRoot, TArray<FYSTagPathSegment> Segments)
	{
		TSharedRef<int32> Attempts = MakeShared<int32>(0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[WeakRoot, Segments = MoveTemp(Segments), Attempts](float)
			{
				const UObject* Root = WeakRoot.Get();
				if (!Root || ++(*Attempts) > MaxFocusAttempts)
				{
					return false;
				}

				const TSharedPtr<IDetailsView> View = FindDetailsViewShowing(Root);
				if (!View.IsValid())
				{
					return true;
				}

				if (const TSharedPtr<FPropertyPath> Path = BuildPropertyPath(Root, Segments))
				{
					View->ScrollPropertyIntoView(*Path, true);
					View->HighlightProperty(*Path);
				}
				return false;
			}));
	}
}

void OpenAndFocus(const FYSTagUsageRecord& Record)
{
	UObject* Asset = Record.Asset.TryLoad();
	if (!Asset || !GEditor)
	{
		return;
	}

	UAssetEditorSubsystem* AssetEditors = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditors || !AssetEditors->OpenEditorForAsset(Asset))
	{
		return;
	}

	UObject* Root = Record.Root.ResolveObject();
	if (!Root)
	{
		return;
	}

	// BP 는 "클래스 디폴트" 를 띄워야 CDO 가 디테일 패널에 올라온다.
	if (Cast<UBlueprint>(Asset) && Root->HasAnyFlags(RF_ClassDefaultObject))
	{
		// UBlueprint 를 여는 에디터는 모두 FAssetEditorToolkit 파생이다. IsBlueprintEditor 는 FBlueprintEditor 만 true.
		if (IAssetEditorInstance* Instance = AssetEditors->FindEditorForAsset(Asset, false))
		{
			FAssetEditorToolkit* Toolkit = static_cast<FAssetEditorToolkit*>(Instance);
			if (Toolkit->IsBlueprintEditor())
			{
				static_cast<FBlueprintEditor*>(Toolkit)->StartEditingDefaults(true, true);
			}
		}
	}

	FocusWhenReady(Root, Record.Path);
}
}

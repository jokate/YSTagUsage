// Copyright Jokate. All Rights Reserved.

#include "SYSTagUsageTab.h"

#include "Algo/Count.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsModule.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "SYSTagCreateDialog.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "YSTagSourceEditor.h"
#include "SourceCodeNavigation.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SExpanderArrow.h"
#include "YSTagUsageFocus.h"
#include "YSTagUsageIndex.h"

#define LOCTEXT_NAMESPACE "SYSTagUsageTab"

struct FYSTagTreeItem
{
	FName FullName;
	FString SimpleName;
	int32 AssetUses = 0;
	int32 CodeUses = 0;
	int32 ConfigUses = 0;
	bool bNative = false;
	// 자신 포함 하위 전체의 애셋 + C++ + 설정 사용
	int32 SubtreeUses = 0;
	bool bHasDefinedOnlyInSubtree = false;
	bool bHasUnusedInSubtree = false;
	TArray<TSharedPtr<FYSTagTreeItem>> Children;
	TArray<TSharedPtr<FYSTagTreeItem>> VisibleChildren;

	int32 Uses() const { return AssetUses + CodeUses + ConfigUses; }
	bool IsUsed() const { return Uses() > 0; }
	// 하위까지 아무 데서도 안 쓰는데 C++ 가 정의한 태그
	bool IsDefinedOnly() const { return SubtreeUses == 0 && bNative; }
	bool IsUnused() const { return SubtreeUses == 0 && !bNative; }
};

// 결과 트리 노드. Record 가 없으면 애셋 묶음이다.
struct FYSResultNode
{
	FString AssetName;
	FString TypeName;
	int32 Count = 0;
	TSharedPtr<FYSTagUsageRecord> Record;
	TArray<TSharedPtr<FYSResultNode>> Children;
};

namespace
{
	namespace Palette
	{
		FLinearColor AssetFill() { return FLinearColor(FColor::FromHex(TEXT("085041"))); }
		FLinearColor AssetText() { return FLinearColor(FColor::FromHex(TEXT("9FE1CB"))); }
		FLinearColor CodeFill() { return FLinearColor(FColor::FromHex(TEXT("3C3489"))); }
		FLinearColor CodeText() { return FLinearColor(FColor::FromHex(TEXT("CECBF6"))); }
		FLinearColor ConfigFill() { return FLinearColor(FColor::FromHex(TEXT("633806"))); }
		FLinearColor ConfigText() { return FLinearColor(FColor::FromHex(TEXT("FAC775"))); }
		FLinearColor Location() { return FLinearColor(FColor::FromHex(TEXT("85B7EB"))); }
		FLinearColor NeutralFill() { return FLinearColor(1.f, 1.f, 1.f, 0.06f); }
		FLinearColor TileFill() { return FLinearColor(1.f, 1.f, 1.f, 0.04f); }
	}

	// 에셋이 이 수를 넘으면 묶음을 접어서 보여준다.
	constexpr int32 AutoExpandRecordLimit = 40;

	const FSlateBrush* RoundedBrush()
	{
		static const FSlateRoundedBoxBrush Brush(FLinearColor::White, 4.f);
		return &Brush;
	}

	FSlateFontInfo SmallFont() { return FAppStyle::GetFontStyle(TEXT("SmallFont")); }
	FSlateFontInfo MonoFont() { return FCoreStyle::GetDefaultFontStyle(TEXT("Mono"), 9); }

	TSharedRef<SWidget> MakeChip(const FText& Text, const FLinearColor& Fill, const FSlateColor& TextColor)
	{
		return SNew(SBorder)
			.BorderImage(RoundedBrush())
			.BorderBackgroundColor(Fill)
			.Padding(FMargin(5.f, 0.f))
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(SmallFont())
				.Text(Text)
				.ColorAndOpacity(TextColor)
			];
	}

	TSharedRef<SWidget> MakeNeutralChip(const FText& Text)
	{
		return MakeChip(Text, Palette::NeutralFill(), FSlateColor::UseSubduedForeground());
	}

	TSharedRef<SWidget> MakeTile(const FText& Label, TAttribute<FText> Value)
	{
		return SNew(SBorder)
			.BorderImage(RoundedBrush())
			.BorderBackgroundColor(Palette::TileFill())
			.Padding(FMargin(10.f, 6.f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Font(SmallFont())
					.Text(Label)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13))
					.Text(Value)
				]
			];
	}

	TSharedRef<SWidget> MakeSectionHeader(const FName IconName, TAttribute<FText> Text)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(IconName))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(Text)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
	}

	FText AllTypesText()
	{
		return LOCTEXT("AllTypes", "모든 타입");
	}

	void Notify(const FText& Text, SNotificationItem::ECompletionState State)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = 5.f;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(State);
		}
	}

	TSharedPtr<FYSTagTreeItem> BuildItem(const TSharedPtr<FGameplayTagNode>& Node, const FYSTagUsageIndex& Index,
		TMap<FName, TSharedPtr<FYSTagTreeItem>>& OutByName)
	{
		TSharedPtr<FYSTagTreeItem> Item = MakeShared<FYSTagTreeItem>();
		Item->FullName = Node->GetCompleteTagName();
		Item->SimpleName = Node->GetSimpleTagName().ToString();
		if (const int32* Count = Index.GetDirectCounts().Find(Item->FullName))
		{
			Item->AssetUses = *Count;
		}
		if (const int32* Count = Index.GetCodeCounts().Find(Item->FullName))
		{
			Item->CodeUses = *Count;
		}
		if (const int32* Count = Index.GetConfigCounts().Find(Item->FullName))
		{
			Item->ConfigUses = *Count;
		}
		Item->bNative = Index.IsNativeTag(Item->FullName);
		Item->SubtreeUses = Item->Uses();

		for (const TSharedPtr<FGameplayTagNode>& ChildNode : Node->GetChildTagNodes())
		{
			TSharedPtr<FYSTagTreeItem> Child = BuildItem(ChildNode, Index, OutByName);
			Item->SubtreeUses += Child->SubtreeUses;
			Item->bHasDefinedOnlyInSubtree |= Child->bHasDefinedOnlyInSubtree;
			Item->bHasUnusedInSubtree |= Child->bHasUnusedInSubtree;
			Item->Children.Add(Child);
		}
		Item->bHasDefinedOnlyInSubtree |= Item->IsDefinedOnly();
		Item->bHasUnusedInSubtree |= Item->IsUnused();

		OutByName.Add(Item->FullName, Item);
		return Item;
	}

	bool IsVisibleIn(const FYSTagTreeItem& Item, EYSTagListMode Mode)
	{
		switch (Mode)
		{
		case EYSTagListMode::Used: return Item.SubtreeUses > 0;
		case EYSTagListMode::DefinedOnly: return Item.bHasDefinedOnlyInSubtree;
		default: return Item.bHasUnusedInSubtree;
		}
	}

	// 지금 목록의 대상인가. 아니면 구조만 잡아주는 부모라 흐리게 그린다.
	bool IsPrimaryIn(const FYSTagTreeItem& Item, EYSTagListMode Mode)
	{
		switch (Mode)
		{
		case EYSTagListMode::Used: return Item.IsUsed();
		case EYSTagListMode::DefinedOnly: return Item.IsDefinedOnly();
		default: return Item.IsUnused();
		}
	}

	bool FilterItem(const TSharedPtr<FYSTagTreeItem>& Item, EYSTagListMode Mode)
	{
		Item->VisibleChildren.Reset();
		for (const TSharedPtr<FYSTagTreeItem>& Child : Item->Children)
		{
			if (FilterItem(Child, Mode))
			{
				Item->VisibleChildren.Add(Child);
			}
		}
		return IsVisibleIn(*Item, Mode);
	}

	void CollectDescendants(const TSharedPtr<FYSTagTreeItem>& Item, TSet<FName>& OutNames)
	{
		for (const TSharedPtr<FYSTagTreeItem>& Child : Item->Children)
		{
			OutNames.Add(Child->FullName);
			CollectDescendants(Child, OutNames);
		}
	}

	bool MatchesText(const FString& Filter, std::initializer_list<const FString*> Fields)
	{
		for (const FString* Field : Fields)
		{
			if (Field->Contains(Filter))
			{
				return true;
			}
		}
		return false;
	}
}

void SYSTagUsageTab::Construct(const FArguments& InArgs)
{
	WeakIndex = InArgs._Index;
	if (FYSTagUsageIndex* Index = GetIndex())
	{
		IndexChangedHandle = Index->OnChanged().AddSP(this, &SYSTagUsageTab::OnIndexChanged);
	}
	TagTreeChangedHandle = IGameplayTagsModule::OnGameplayTagTreeChanged.AddSP(this, &SYSTagUsageTab::OnIndexChanged);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BuildToolbar()
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Horizontal)
			+ SSplitter::Slot()
			.Value(0.32f)
			[
				BuildTagPanel()
			]
			+ SSplitter::Slot()
			.Value(0.68f)
			[
				BuildResultPanel()
			]
		]
	];

	RebuildTree();
	RefreshResults();
}

SYSTagUsageTab::~SYSTagUsageTab()
{
	if (FYSTagUsageIndex* Index = GetIndex())
	{
		Index->OnChanged().Remove(IndexChangedHandle);
	}
	IGameplayTagsModule::OnGameplayTagTreeChanged.Remove(TagTreeChangedHandle);
}

FYSTagUsageIndex* SYSTagUsageTab::GetIndex() const
{
	// 인덱스는 모듈이 들고 있고 게임 스레드에서만 파괴된다.
	return WeakIndex.Pin().Get();
}

TSharedRef<SWidget> SYSTagUsageTab::BuildToolbar()
{
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
		.Padding(FMargin(10.f, 6.f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(TEXT("Icons.Tag")))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
				.Text(LOCTEXT("ToolTitle", "태그 사용처"))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(16.f, 0.f, 8.f, 0.f)
			[
				SNew(SBox)
				.WidthOverride(120.f)
				.HeightOverride(4.f)
				.Visibility_Lambda([this]()
				{
					const FYSTagUsageIndex* Index = GetIndex();
					return Index && Index->IsScanning() ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					SNew(SProgressBar)
					.Percent_Lambda([this]() -> TOptional<float>
					{
						const FYSTagUsageIndex* Index = GetIndex();
						if (!Index || Index->GetScanTotal() == 0)
						{
							return TOptional<float>();
						}
						return static_cast<float>(Index->GetScanDone()) / Index->GetScanTotal();
					})
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(8.f, 0.f)
			[
				SNew(STextBlock)
				.Text(this, &SYSTagUsageTab::GetStatusText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
				.ToolTipText(LOCTEXT("RescanTip", "캐시를 버리고 /Game 전체와 Source/ 를 다시 스캔합니다."))
				.IsEnabled_Lambda([this]() { const FYSTagUsageIndex* Index = GetIndex(); return Index && !Index->IsScanning(); })
				.OnClicked_Lambda([this]()
				{
					if (FYSTagUsageIndex* Index = GetIndex())
					{
						Index->Rescan();
					}
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 4.f, 0.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(TEXT("Icons.Refresh")))
						.ColorAndOpacity(FSlateColor::UseForeground())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(LOCTEXT("Rescan", "다시 스캔"))
					]
				]
			]
		];
}

TSharedRef<SWidget> SYSTagUsageTab::BuildTagPanel()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.f, 6.f, 6.f, 0.f)
		.HAlign(HAlign_Left)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
			.ToolTipText(LOCTEXT("AddTagTip", "새 게임플레이 태그를 C++ 파일이나 ini 에 추가합니다. 트리 우클릭으로 하위 태그를 바로 만들 수도 있습니다."))
			.OnClicked_Lambda([this]()
			{
				OpenCreateDialog(SelectedTag.IsNone() ? FString() : SelectedTag.ToString() + TEXT("."));
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.f, 0.f, 4.f, 0.f)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush(TEXT("Icons.Plus")))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock).Text(LOCTEXT("AddTag", "태그 추가"))
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(6.f)
		[
			SNew(SSegmentedControl<EYSTagListMode>)
			.Value_Lambda([this]() { return ListMode; })
			.OnValueChanged_Lambda([this](EYSTagListMode NewMode)
			{
				ListMode = NewMode;
				ApplyListMode();
			})
			+ SSegmentedControl<EYSTagListMode>::Slot(EYSTagListMode::Used)
			.Text_Lambda([this]() { return FText::Format(LOCTEXT("UsedSegment", "사용 중 {0}"), UsedCount); })
			.ToolTip(LOCTEXT("UsedSegmentTip", "애셋, C++, ini 설정 중 어디서든 쓰는 태그"))
			+ SSegmentedControl<EYSTagListMode>::Slot(EYSTagListMode::DefinedOnly)
			.Text_Lambda([this]() { return FText::Format(LOCTEXT("DefinedOnlySegment", "C++ 정의만 {0}"), DefinedOnlyCount); })
			.ToolTip(LOCTEXT("DefinedOnlySegmentTip", "C++ 로 정의했지만 애셋·C++ 어디서도 쓰지 않는 태그 (하위 포함)"))
			+ SSegmentedControl<EYSTagListMode>::Slot(EYSTagListMode::Unused)
			.Text_Lambda([this]() { return FText::Format(LOCTEXT("UnusedSegment", "미사용 {0}"), UnusedCount); })
			.ToolTip(LOCTEXT("UnusedSegmentTip", "ini·데이터 테이블에만 있고 하위까지 아무 데서도 쓰지 않는 태그"))
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(4.f, 0.f)
		[
			SAssignNew(TagTree, STreeView<FItemPtr>)
			.TreeItemsSource(&VisibleRoots)
			.SelectionMode(ESelectionMode::Single)
			.OnGenerateRow(this, &SYSTagUsageTab::OnGenerateTagRow)
			.OnGetChildren(this, &SYSTagUsageTab::OnGetTagChildren)
			.OnSelectionChanged(this, &SYSTagUsageTab::OnTagSelectionChanged)
			.OnExpansionChanged(this, &SYSTagUsageTab::OnTagExpansionChanged)
			.OnContextMenuOpening(this, &SYSTagUsageTab::OnTagContextMenu)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 6.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 4.f, 0.f)
			[
				MakeChip(LOCTEXT("LegendAsset", "애셋"), Palette::AssetFill(), Palette::AssetText())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 4.f, 0.f)
			[
				MakeChip(LOCTEXT("LegendCode", "C++"), Palette::CodeFill(), Palette::CodeText())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 8.f, 0.f)
			[
				MakeChip(LOCTEXT("LegendConfig", "ini"), Palette::ConfigFill(), Palette::ConfigText())
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(SmallFont())
				.Text(LOCTEXT("LegendDim", "흐린 항목 = 하위만 해당하는 부모"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
		];
}

TSharedRef<SWidget> SYSTagUsageTab::BuildResultPanel()
{
	TSharedRef<SWidget> Content = SNew(SVerticalBox)
		// 제목
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.f, 10.f, 12.f, 0.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13))
				.Text_Lambda([this]()
				{
					return IsAssetMode()
						? FText::Format(LOCTEXT("AssetTitle", "애셋: {0}"), SelectedPackagesLabel)
						: FText::FromName(SelectedTag);
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.f, 0.f, 0.f, 0.f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
				.ToolTipText(LOCTEXT("CopyTagTip", "태그 이름 복사"))
				.Visibility_Lambda([this]() { return IsAssetMode() ? EVisibility::Collapsed : EVisibility::Visible; })
				.OnClicked_Lambda([this]()
				{
					FPlatformApplicationMisc::ClipboardCopy(*SelectedTag.ToString());
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush(TEXT("GenericCommands.Copy")))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
				.ToolTipText(LOCTEXT("DeleteTagTip", "태그 삭제…"))
				.Visibility_Lambda([this]() { return IsAssetMode() ? EVisibility::Collapsed : EVisibility::Visible; })
				.OnClicked_Lambda([this]()
				{
					DeleteTag(SelectedTag);
					return FReply::Handled();
				})
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("BackToTag", "태그 보기로"))
				.Visibility_Lambda([this]() { return IsAssetMode() ? EVisibility::Visible : EVisibility::Collapsed; })
				.OnClicked_Lambda([this]()
				{
					SelectedPackages.Reset();
					RefreshResults();
					return FReply::Handled();
				})
			]
		]
		// 설명 · 정의 위치
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.f, 2.f, 12.f, 8.f)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text_Lambda([this]() { return SummaryDetail; })
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		]
		// 요약 타일
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.f, 0.f, 12.f, 8.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				MakeTile(LOCTEXT("TileAssets", "애셋"), TAttribute<FText>::CreateLambda([this]() { return FText::AsNumber(AssetTotal); }))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				MakeTile(LOCTEXT("TileCode", "C++ 사용"), TAttribute<FText>::CreateLambda([this]() { return FText::AsNumber(CodeTotal); }))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				MakeTile(LOCTEXT("TileConfig", "ini 설정"), TAttribute<FText>::CreateLambda([this]() { return FText::AsNumber(ConfigTotal); }))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.f)
			[
				MakeTile(LOCTEXT("TileInherited", "상속된 값"), TAttribute<FText>::CreateLambda([this]() { return FText::AsNumber(InheritedTotal); }))
			]
		]
		// 필터
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.f, 0.f, 12.f, 6.f)
		[
			BuildFilterBar()
		]
		// 애셋 / C++
		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(12.f, 0.f, 12.f, 0.f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+ SSplitter::Slot()
			.Value(0.65f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 4.f)
				[
					MakeSectionHeader(TEXT("Icons.Package"), TAttribute<FText>::CreateLambda([this]()
					{
						return FText::Format(LOCTEXT("AssetSection", "애셋 {0} · 사용처 {1}"), ResultRoots.Num(), ShownRecordCount);
					}))
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
					.Padding(2.f)
					[
						SAssignNew(ResultTree, STreeView<FNodePtr>)
						.TreeItemsSource(&ResultRoots)
						.SelectionMode(ESelectionMode::Single)
						.OnGenerateRow(this, &SYSTagUsageTab::OnGenerateResultRow)
						.OnGetChildren(this, &SYSTagUsageTab::OnGetResultChildren)
						.OnMouseButtonDoubleClick(this, &SYSTagUsageTab::OnResultDoubleClicked)
					]
				]
			]
			+ SSplitter::Slot()
			.Value(0.35f)
			[
				SNew(SVerticalBox)
				.Visibility_Lambda([this]() { return IsAssetMode() ? EVisibility::Collapsed : EVisibility::Visible; })
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 6.f, 0.f, 4.f)
				[
					MakeSectionHeader(TEXT("MainFrame.OpenSourceCodeEditor"), TAttribute<FText>::CreateLambda([this]()
					{
						return FText::Format(LOCTEXT("CodeSection", "C++ · ini {0}"), CodeRows.Num());
					}))
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
					.Padding(2.f)
					[
						SAssignNew(CodeList, SListView<FCodePtr>)
						.ListItemsSource(&CodeRows)
						.SelectionMode(ESelectionMode::Single)
						.OnGenerateRow(this, &SYSTagUsageTab::OnGenerateCodeRow)
						.OnMouseButtonDoubleClick(this, &SYSTagUsageTab::OnCodeDoubleClicked)
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(12.f, 6.f)
		[
			SNew(STextBlock)
			.Font(SmallFont())
			.Text(LOCTEXT("DoubleClickHint", "더블클릭: 애셋은 해당 필드로, C++ 는 IDE 에서 해당 줄로"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];

	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() { return HasSelection() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Content
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Visibility_Lambda([this]() { return HasSelection() ? EVisibility::Collapsed : EVisibility::Visible; })
			.Text(LOCTEXT("NoSelection", "왼쪽에서 태그를 고르세요"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
}

TSharedRef<SWidget> SYSTagUsageTab::BuildFilterBar()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 12.f, 0.f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]() { return bIncludeChildren ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.IsEnabled_Lambda([this]() { return !IsAssetMode(); })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
			{
				bIncludeChildren = State == ECheckBoxState::Checked;
				RefreshResults();
			})
			[
				SNew(STextBlock).Text(LOCTEXT("IncludeChildren", "하위 포함"))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 12.f, 0.f)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]() { return bShowInherited ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
			{
				bShowInherited = State == ECheckBoxState::Checked;
				RefreshResults();
			})
			[
				SNew(STextBlock).Text(LOCTEXT("ShowInherited", "상속 표시"))
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 8.f, 0.f)
		[
			SAssignNew(TypeCombo, SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&TypeOptions)
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Option)
			{
				return SNew(STextBlock).Text(Option->IsEmpty() ? AllTypesText() : FText::FromString(*Option));
			})
			.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Option, ESelectInfo::Type SelectInfo)
			{
				if (SelectInfo == ESelectInfo::Direct)
				{
					return;
				}
				TypeFilter = Option.IsValid() ? *Option : FString();
				RefreshResults();
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return TypeFilter.IsEmpty() ? AllTypesText() : FText::FromString(TypeFilter); })
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("ResultFilterHint", "애셋·위치·코드 거르기"))
			.OnTextChanged_Lambda([this](const FText& Text)
			{
				TextFilter = Text.ToString();
				RefreshResults();
			})
		];
}

void SYSTagUsageTab::ShowAssets(const TArray<FName>& PackageNames, const FText& Label)
{
	SelectedPackages = PackageNames;
	SelectedPackagesLabel = Label;
	TagTree->ClearSelection();
	RefreshResults();
}

void SYSTagUsageTab::OnIndexChanged()
{
	RebuildTree();
	RefreshResults();
}

// ── 태그 트리 ─────────────────────────────────────────────

void SYSTagUsageTab::RebuildTree()
{
	const FYSTagUsageIndex* Index = GetIndex();
	if (!Index)
	{
		return;
	}

	TArray<TSharedPtr<FGameplayTagNode>> RootNodes;
	UGameplayTagsManager::Get().GetFilteredGameplayRootTags(FString(), RootNodes);

	AllRoots.Reset();
	ItemsByName.Reset();
	for (const TSharedPtr<FGameplayTagNode>& Node : RootNodes)
	{
		AllRoots.Add(BuildItem(Node, *Index, ItemsByName));
	}

	UsedCount = 0;
	DefinedOnlyCount = 0;
	UnusedCount = 0;
	for (const TPair<FName, FItemPtr>& Pair : ItemsByName)
	{
		UsedCount += Pair.Value->IsUsed() ? 1 : 0;
		DefinedOnlyCount += Pair.Value->IsDefinedOnly() ? 1 : 0;
		UnusedCount += Pair.Value->IsUnused() ? 1 : 0;
	}

	if (bFirstBuild)
	{
		ItemsByName.GetKeys(ExpandedTags);
		bFirstBuild = false;
	}
	ApplyListMode();
}

void SYSTagUsageTab::ApplyListMode()
{
	VisibleRoots.Reset();
	for (const FItemPtr& Root : AllRoots)
	{
		if (FilterItem(Root, ListMode))
		{
			VisibleRoots.Add(Root);
		}
	}
	TagTree->RequestTreeRefresh();

	// 트리 아이템을 새로 만들었으니 펼침·선택을 이름으로 되살린다.
	TGuardValue<bool> Guard(bRestoringTreeState, true);
	for (const TPair<FName, FItemPtr>& Pair : ItemsByName)
	{
		TagTree->SetItemExpansion(Pair.Value, ExpandedTags.Contains(Pair.Key));
	}
	if (const FItemPtr* Selected = ItemsByName.Find(SelectedTag); Selected && !IsAssetMode())
	{
		TagTree->SetSelection(*Selected, ESelectInfo::Direct);
	}
}

TSharedRef<ITableRow> SYSTagUsageTab::OnGenerateTagRow(FItemPtr Item, const TSharedRef<STableViewBase>& Owner)
{
	const bool bPrimary = IsPrimaryIn(*Item, ListMode);

	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		.Padding(0.f, 2.f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Item->SimpleName))
			.ColorAndOpacity(bPrimary ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground())
		];

	auto AddChip = [&Box](TSharedRef<SWidget> Chip)
	{
		Box->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4.f, 1.f, 0.f, 1.f)
			[
				Chip
			];
	};

	if (Item->AssetUses > 0)
	{
		AddChip(MakeChip(FText::Format(LOCTEXT("AssetChip", "애셋 {0}"), Item->AssetUses), Palette::AssetFill(), Palette::AssetText()));
	}
	if (Item->CodeUses > 0)
	{
		AddChip(MakeChip(FText::Format(LOCTEXT("CodeChip", "C++ {0}"), Item->CodeUses), Palette::CodeFill(), Palette::CodeText()));
	}
	if (Item->ConfigUses > 0)
	{
		AddChip(MakeChip(FText::Format(LOCTEXT("ConfigChip", "ini {0}"), Item->ConfigUses), Palette::ConfigFill(), Palette::ConfigText()));
	}
	if (!Item->IsUsed() && Item->SubtreeUses > 0)
	{
		AddChip(MakeNeutralChip(FText::Format(LOCTEXT("ChildrenChip", "하위 {0}"), Item->SubtreeUses)));
	}

	return SNew(STableRow<FItemPtr>, Owner)
		.ToolTipText(FText::FromName(Item->FullName))
		[
			Box
		];
}

void SYSTagUsageTab::OnGetTagChildren(FItemPtr Item, TArray<FItemPtr>& OutChildren)
{
	OutChildren = Item->VisibleChildren;
}

void SYSTagUsageTab::OnTagSelectionChanged(FItemPtr Item, ESelectInfo::Type /*SelectInfo*/)
{
	if (bRestoringTreeState || !Item.IsValid())
	{
		return;
	}
	SelectedTag = Item->FullName;
	SelectedPackages.Reset();
	RefreshResults();
}

void SYSTagUsageTab::OnTagExpansionChanged(FItemPtr Item, bool bExpanded)
{
	if (bRestoringTreeState)
	{
		return;
	}
	if (bExpanded)
	{
		ExpandedTags.Add(Item->FullName);
	}
	else
	{
		ExpandedTags.Remove(Item->FullName);
	}
}

TSharedPtr<SWidget> SYSTagUsageTab::OnTagContextMenu()
{
	const TArray<FItemPtr> Selection = TagTree->GetSelectedItems();
	if (Selection.IsEmpty())
	{
		return nullptr;
	}
	const FName TagName = Selection[0]->FullName;

	FMenuBuilder Menu(true, nullptr);
	Menu.AddMenuEntry(
		LOCTEXT("AddChildTag", "하위 태그 추가…"),
		FText::Format(LOCTEXT("AddChildTagTip", "{0} 아래에 새 태그를 만듭니다."), FText::FromName(TagName)),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Plus")),
		FUIAction(FExecuteAction::CreateSPLambda(this, [this, TagName]() { OpenCreateDialog(TagName.ToString() + TEXT(".")); })));
	Menu.AddMenuEntry(
		LOCTEXT("CopyTagName", "이름 복사"),
		FText::GetEmpty(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("GenericCommands.Copy")),
		FUIAction(FExecuteAction::CreateLambda([TagName]() { FPlatformApplicationMisc::ClipboardCopy(*TagName.ToString()); })));
	Menu.AddSeparator();
	Menu.AddMenuEntry(
		LOCTEXT("DeleteTagMenu", "태그 삭제…"),
		LOCTEXT("DeleteTagMenuTip", "정의된 ini 와 C++ 파일에서 지웁니다. 지우기 전에 사용처를 보여주고 확인을 받습니다."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Delete")),
		FUIAction(FExecuteAction::CreateSP(this, &SYSTagUsageTab::DeleteTag, TagName)));
	return Menu.MakeWidget();
}

void SYSTagUsageTab::OpenCreateDialog(const FString& InitialTag)
{
	SYSTagCreateDialog::OpenModal(InitialTag, SharedThis(this), SYSTagCreateDialog::FOnTagCreated::CreateSPLambda(this, [this](FName TagName)
	{
		// C++ 로 만들었으면 소스 스캔을 다시 해야 정의 위치가 잡힌다.
		if (FYSTagUsageIndex* Index = GetIndex())
		{
			Index->RescanSource();
		}
		SelectTag(TagName);
		Notify(FText::Format(LOCTEXT("TagCreated", "{0} 태그를 추가했습니다."), FText::FromName(TagName)), SNotificationItem::CS_Success);
	}));
}

void SYSTagUsageTab::DeleteTag(FName TagName)
{
	FYSTagUsageIndex* Index = GetIndex();
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	const TSharedPtr<FGameplayTagNode> Node = TagName.IsNone() ? nullptr : Manager.FindTagNode(TagName);
	if (!Index || !Node.IsValid())
	{
		return;
	}

	const FText Title = LOCTEXT("DeleteTitle", "태그 삭제");
	if (!Node->IsExplicitTag())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			FText::Format(LOCTEXT("ImplicitTag", "{0} 은(는) 하위 태그 때문에 생긴 암시적 태그라 직접 지울 수 없습니다. 하위 태그를 먼저 정리하세요."), FText::FromName(TagName)),
			Title);
		return;
	}

	TArray<FString> IniSources;
	TArray<FString> OtherSources;
	for (const FName SourceName : Node->GetAllSourceNames())
	{
		const FGameplayTagSource* Source = Manager.FindTagSource(SourceName);
		const EGameplayTagSourceType Type = Source ? Source->SourceType : EGameplayTagSourceType::Invalid;
		if (Type == EGameplayTagSourceType::DefaultTagList || Type == EGameplayTagSourceType::TagList || Type == EGameplayTagSourceType::RestrictedTagList)
		{
			IniSources.Add(SourceName.ToString());
		}
		else if (Type != EGameplayTagSourceType::Native)
		{
			OtherSources.Add(SourceName.ToString());
		}
	}

	// 빌드 전 임시 태그처럼 매니저가 네이티브로 모르는 경우도 있어서 소스 파일에서 직접 찾는다.
	TArray<FYSTagCodeRef> NativeLines;
	YSTagUsage::FindNativeTagLines(TagName, NativeLines);

	if (IniSources.IsEmpty() && NativeLines.IsEmpty())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
			FText::Format(LOCTEXT("CannotDelete", "{0} 은(는) 이 툴이 고칠 수 없는 곳({1})에 정의돼 있습니다. 엔진·플러그인 C++ 이거나 데이터 테이블일 수 있습니다."),
				FText::FromName(TagName), FText::FromString(OtherSources.IsEmpty() ? TEXT("프로젝트 밖") : FString::Join(OtherSources, TEXT(", ")))),
			Title);
		return;
	}

	TSet<FName> JustThis;
	JustThis.Add(TagName);
	TArray<FYSTagUsageRecord> Records;
	Index->GetRecordsForTags(JustThis, Records);
	TSet<FSoftObjectPath> Assets;
	for (const FYSTagUsageRecord& Record : Records)
	{
		Assets.Add(Record.Asset);
	}
	TArray<FYSTagCodeRef> Refs;
	Index->GetCodeRefsForTags(JustThis, Refs);
	const int32 CodeUses = Algo::CountIf(Refs, [](const FYSTagCodeRef& Ref) { return !Ref.bDefinition && !Ref.bConfig; });
	const int32 ConfigUses = Algo::CountIf(Refs, [](const FYSTagCodeRef& Ref) { return Ref.bConfig; });

	FString Message = FString::Printf(TEXT("%s 태그를 삭제합니다.\n\n고칠 파일:"), *TagName.ToString());
	for (const FString& Source : IniSources)
	{
		Message += FString::Printf(TEXT("\n  · %s"), *Source);
	}
	bool bTouchesGenerated = false;
	for (const FYSTagCodeRef& Line : NativeLines)
	{
		Message += FString::Printf(TEXT("\n  · %s:%d"), *FPaths::GetCleanFilename(Line.File), Line.Line);
		bTouchesGenerated |= FPaths::GetBaseFilename(Line.File).Contains(TEXT("Generated"));
	}

	if (!Assets.IsEmpty() || CodeUses > 0 || ConfigUses > 0)
	{
		Message += FString::Printf(TEXT("\n\n아직 사용 중입니다: 애셋 %d개, C++ %d곳, ini %d곳."), Assets.Num(), CodeUses, ConfigUses);
		if (CodeUses > 0)
		{
			Message += TEXT("\nC++ 참조가 남아 있으면 빌드 에러가 납니다.");
		}
		if (!IniSources.IsEmpty() && !Assets.IsEmpty())
		{
			Message += TEXT("\n애셋 참조가 남아 있으면 엔진이 ini 삭제를 거부합니다.");
		}
	}
	if (bTouchesGenerated)
	{
		Message += TEXT("\n\n생성기 파일에서 지운 태그는 GenerateGameplayTags 를 다시 돌리면 되살아납니다.");
	}
	if (!NativeLines.IsEmpty())
	{
		Message += TEXT("\n\nC++ 태그는 다시 빌드해야 목록에서 사라집니다.");
	}
	if (const int32 ChildCount = Node->GetChildTagNodes().Num())
	{
		Message += FString::Printf(TEXT("\n\n하위 태그 %d개는 그대로 남습니다."), ChildCount);
	}

	if (FMessageDialog::Open(EAppMsgType::YesNo, FText::FromString(Message), Title) != EAppReturnType::Yes)
	{
		return;
	}

	TArray<FString> Failures;
	if (!NativeLines.IsEmpty())
	{
		int32 RemovedLines = 0;
		FText Error;
		if (!YSTagUsage::RemoveNativeTag(TagName, RemovedLines, Error))
		{
			Failures.Add(Error.ToString());
		}
	}
	if (!IniSources.IsEmpty() && !IGameplayTagsEditorModule::Get().DeleteTagFromINI(Node))
	{
		Failures.Add(TEXT("ini 에서 지우지 못했습니다. 에디터 알림을 확인하세요."));
	}

	Index->RescanSource();

	if (Failures.IsEmpty())
	{
		Notify(FText::Format(NativeLines.IsEmpty()
				? LOCTEXT("TagDeleted", "{0} 태그를 삭제했습니다.")
				: LOCTEXT("TagDeletedNative", "{0} 태그를 삭제했습니다. C++ 태그는 빌드 후 반영됩니다."),
			FText::FromName(TagName)), SNotificationItem::CS_Success);
	}
	else
	{
		Notify(FText::FromString(FString::Join(Failures, TEXT("\n"))), SNotificationItem::CS_Fail);
	}
}

void SYSTagUsageTab::SelectTag(FName TagName)
{
	const FItemPtr* Item = ItemsByName.Find(TagName);
	if (!Item)
	{
		return;
	}

	SelectedTag = TagName;
	SelectedPackages.Reset();
	const FYSTagTreeItem& Found = **Item;
	ListMode = Found.IsDefinedOnly() ? EYSTagListMode::DefinedOnly
		: Found.IsUnused() ? EYSTagListMode::Unused
		: EYSTagListMode::Used;

	// 조상을 펼쳐야 보인다.
	FString Prefix;
	TArray<FString> Parts;
	TagName.ToString().ParseIntoArray(Parts, TEXT("."));
	for (int32 Index = 0; Index + 1 < Parts.Num(); ++Index)
	{
		Prefix = Prefix.IsEmpty() ? Parts[Index] : Prefix + TEXT(".") + Parts[Index];
		ExpandedTags.Add(FName(*Prefix));
	}

	ApplyListMode();
	TagTree->RequestScrollIntoView(*Item);
	RefreshResults();
}

// ── 결과 ─────────────────────────────────────────────────

void SYSTagUsageTab::RefreshResults()
{
	const FYSTagUsageIndex* Index = GetIndex();
	TArray<FYSTagUsageRecord> Raw;
	TArray<FYSTagCodeRef> RawCode;
	if (Index && IsAssetMode())
	{
		TSet<FName> PackageSet;
		PackageSet.Append(SelectedPackages);
		Index->GetRecordsForPackages(PackageSet, Raw);
	}
	else if (Index && !SelectedTag.IsNone())
	{
		TSet<FName> Tags;
		Tags.Add(SelectedTag);
		if (bIncludeChildren)
		{
			if (const FItemPtr* Item = ItemsByName.Find(SelectedTag))
			{
				CollectDescendants(*Item, Tags);
			}
		}
		Index->GetRecordsForTags(Tags, Raw);
		Index->GetCodeRefsForTags(Tags, RawCode);
	}

	// 요약 타일은 필터와 무관한 전체 수
	TSet<FString> Types;
	TSet<FSoftObjectPath> AllAssets;
	InheritedTotal = 0;
	for (const FYSTagUsageRecord& Record : Raw)
	{
		Types.Add(Record.TypeName);
		AllAssets.Add(Record.Asset);
		InheritedTotal += Record.bInherited ? 1 : 0;
	}
	AssetTotal = AllAssets.Num();
	CodeTotal = Algo::CountIf(RawCode, [](const FYSTagCodeRef& Ref) { return !Ref.bDefinition && !Ref.bConfig; });
	ConfigTotal = Algo::CountIf(RawCode, [](const FYSTagCodeRef& Ref) { return Ref.bConfig; });
	RebuildTypeOptions(Types);
	RefreshSummary(RawCode);

	// 애셋별로 묶는다
	TMap<FSoftObjectPath, FNodePtr> Groups;
	ResultRoots.Reset();
	ShownRecordCount = 0;
	for (FYSTagUsageRecord& Record : Raw)
	{
		if ((!bShowInherited && Record.bInherited)
			|| (!TypeFilter.IsEmpty() && Record.TypeName != TypeFilter))
		{
			continue;
		}
		const FString AssetName = Record.Asset.GetAssetName();
		const FString TagName = Record.Tag.ToString();
		if (!TextFilter.IsEmpty() && !MatchesText(TextFilter, { &AssetName, &Record.DisplayPath, &TagName }))
		{
			continue;
		}

		FNodePtr& Group = Groups.FindOrAdd(Record.Asset);
		if (!Group.IsValid())
		{
			Group = MakeShared<FYSResultNode>();
			Group->AssetName = AssetName;
			Group->TypeName = Record.TypeName;
			ResultRoots.Add(Group);
		}
		FNodePtr Leaf = MakeShared<FYSResultNode>();
		Leaf->Record = MakeShared<FYSTagUsageRecord>(MoveTemp(Record));
		Group->Children.Add(Leaf);
		++Group->Count;
		++ShownRecordCount;
	}

	ResultRoots.Sort([](const FNodePtr& A, const FNodePtr& B) { return A->AssetName < B->AssetName; });
	for (const FNodePtr& Group : ResultRoots)
	{
		Group->Children.Sort([](const FNodePtr& A, const FNodePtr& B)
		{
			return A->Record->DisplayPath != B->Record->DisplayPath
				? A->Record->DisplayPath < B->Record->DisplayPath
				: A->Record->Tag.LexicalLess(B->Record->Tag);
		});
	}

	ResultTree->RequestTreeRefresh();
	const bool bExpand = ShownRecordCount <= AutoExpandRecordLimit;
	for (const FNodePtr& Group : ResultRoots)
	{
		ResultTree->SetItemExpansion(Group, bExpand);
	}

	CodeRows.Reset();
	for (FYSTagCodeRef& Ref : RawCode)
	{
		if (Ref.bDefinition)
		{
			continue;
		}
		const FString TagName = Ref.Tag.ToString();
		if (!TextFilter.IsEmpty() && !MatchesText(TextFilter, { &Ref.File, &Ref.Snippet, &TagName }))
		{
			continue;
		}
		CodeRows.Add(MakeShared<FYSTagCodeRef>(MoveTemp(Ref)));
	}
	CodeRows.Sort([](const FCodePtr& A, const FCodePtr& B)
	{
		return A->File != B->File ? A->File < B->File : A->Line < B->Line;
	});
	CodeList->RequestListRefresh();
}

void SYSTagUsageTab::RefreshSummary(const TArray<FYSTagCodeRef>& RawCode)
{
	if (IsAssetMode())
	{
		SummaryDetail = LOCTEXT("AssetSummary", "선택한 애셋이 필드에 세팅한 태그");
		return;
	}
	if (SelectedTag.IsNone())
	{
		SummaryDetail = FText::GetEmpty();
		return;
	}

	TArray<FString> Parts;

	FString Comment;
	FName FirstSource;
	bool bExplicit = false, bRestricted = false, bAllowNonRestrictedChildren = false;
	if (UGameplayTagsManager::Get().GetTagEditorData(SelectedTag, Comment, FirstSource, bExplicit, bRestricted, bAllowNonRestrictedChildren)
		&& !Comment.IsEmpty())
	{
		Parts.Add(Comment);
	}

	const FYSTagCodeRef* Definition = RawCode.FindByPredicate([this](const FYSTagCodeRef& Ref)
	{
		return Ref.bDefinition && Ref.Tag == SelectedTag;
	});
	if (Definition)
	{
		Parts.Add(FString::Printf(TEXT("%s:%d (C++ 정의)"), *FPaths::GetCleanFilename(Definition->File), Definition->Line));
	}
	else if (!FirstSource.IsNone())
	{
		Parts.Add(FString::Printf(TEXT("%s 에 정의"), *FirstSource.ToString()));
	}

	SummaryDetail = FText::FromString(FString::Join(Parts, TEXT("  ·  ")));
}

void SYSTagUsageTab::RebuildTypeOptions(const TSet<FString>& Types)
{
	if (!TypeFilter.IsEmpty() && !Types.Contains(TypeFilter))
	{
		TypeFilter.Reset();
	}

	TArray<FString> Sorted = Types.Array();
	Sorted.Sort();
	TypeOptions.Reset();
	TypeOptions.Add(MakeShared<FString>());
	for (const FString& Type : Sorted)
	{
		TypeOptions.Add(MakeShared<FString>(Type));
	}
	TypeCombo->RefreshOptions();
}

TSharedRef<ITableRow> SYSTagUsageTab::OnGenerateResultRow(FNodePtr Node, const TSharedRef<STableViewBase>& Owner)
{
	TSharedRef<STableRow<FNodePtr>> Row = SNew(STableRow<FNodePtr>, Owner);
	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SExpanderArrow, Row)
		];

	if (!Node->Record.IsValid())
	{
		Box->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 3.f)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
				.Text(FText::FromString(Node->AssetName))
			];
		Box->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.f, 0.f, 0.f, 0.f)
			[
				MakeNeutralChip(FText::FromString(Node->TypeName))
			];
		Box->AddSlot()
			.FillWidth(1.f)
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(STextBlock)
				.Text(FText::AsNumber(Node->Count))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
		Row->SetToolTipText(LOCTEXT("GroupTip", "더블클릭: 애셋 열기"));
	}
	else
	{
		const FYSTagUsageRecord& Record = *Node->Record;
		Box->AddSlot()
			.FillWidth(1.f)
			.VAlign(VAlign_Center)
			.Padding(0.f, 2.f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Record.DisplayPath))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.ColorAndOpacity(Record.bInherited ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground())
			];
		if (ShowsTagOnRows())
		{
			Box->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.f, 0.f, 0.f, 0.f)
				[
					MakeNeutralChip(FText::FromName(Record.Tag))
				];
		}
		if (Record.bInherited)
		{
			Box->AddSlot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(6.f, 0.f, 0.f, 0.f)
				[
					MakeNeutralChip(LOCTEXT("InheritedChip", "상속"))
				];
		}
		Row->SetToolTipText(FText::FromString(FString::Printf(TEXT("%s\n%s\n더블클릭: 애셋을 열고 이 필드로 이동"),
			*Record.Asset.ToString(), *Record.DisplayPath)));
	}

	Row->SetContent(Box);
	return Row;
}

void SYSTagUsageTab::OnGetResultChildren(FNodePtr Node, TArray<FNodePtr>& OutChildren)
{
	OutChildren = Node->Children;
}

void SYSTagUsageTab::OnResultDoubleClicked(FNodePtr Node)
{
	if (!Node.IsValid())
	{
		return;
	}
	// 묶음이면 첫 필드로 연다.
	const FNodePtr Target = Node->Record.IsValid() ? Node : (Node->Children.IsEmpty() ? nullptr : Node->Children[0]);
	if (Target.IsValid())
	{
		YSTagUsage::OpenAndFocus(*Target->Record);
	}
}

TSharedRef<ITableRow> SYSTagUsageTab::OnGenerateCodeRow(FCodePtr Ref, const TSharedRef<STableViewBase>& Owner)
{
	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.f, 3.f)
		[
			SNew(SBox)
			.WidthOverride(200.f)
			[
				SNew(STextBlock)
				.Font(MonoFont())
				.Text(FText::FromString(FString::Printf(TEXT("%s:%d"), *FPaths::GetCleanFilename(Ref->File), Ref->Line)))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
				.ColorAndOpacity(Palette::Location())
			]
		];

	if (Ref->bConfig)
	{
		Box->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				MakeChip(LOCTEXT("ConfigRowChip", "ini"), Palette::ConfigFill(), Palette::ConfigText())
			];
	}
	if (ShowsTagOnRows())
	{
		Box->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				MakeNeutralChip(FText::FromName(Ref->Tag))
			];
	}

	Box->AddSlot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(MonoFont())
			.Text(FText::FromString(Ref->Snippet))
			.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];

	return SNew(STableRow<FCodePtr>, Owner)
		.ToolTipText(FText::FromString(FString::Printf(TEXT("%s:%d\n더블클릭: IDE 에서 열기"), *Ref->File, Ref->Line)))
		[
			Box
		];
}

void SYSTagUsageTab::OnCodeDoubleClicked(FCodePtr Ref)
{
	if (Ref.IsValid())
	{
		FSourceCodeNavigation::OpenSourceFile(Ref->File, Ref->Line);
	}
}

FText SYSTagUsageTab::GetStatusText() const
{
	const FYSTagUsageIndex* Index = GetIndex();
	if (!Index)
	{
		return FText::GetEmpty();
	}
	if (Index->IsScanning())
	{
		return FText::Format(LOCTEXT("Scanning", "스캔 중 {0} / {1} · 끝나기 전 결과는 불완전합니다"),
			Index->GetScanDone(), Index->GetScanTotal());
	}
	return LOCTEXT("ScanDone", "스캔 완료 · 애셋을 저장하면 자동 갱신");
}

#undef LOCTEXT_NAMESPACE

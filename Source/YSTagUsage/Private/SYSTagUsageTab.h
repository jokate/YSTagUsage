// Copyright Jokate. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STreeView.h"
#include "YSTagCodeScanner.h"
#include "YSTagUsageTypes.h"

class FYSTagUsageIndex;
struct FYSTagTreeItem;
struct FYSResultNode;
template <typename OptionType> class SComboBox;

enum class EYSTagListMode : uint8
{
	Used,
	DefinedOnly,
	Unused,
};

class SYSTagUsageTab : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SYSTagUsageTab) {}
		SLATE_ARGUMENT(TWeakPtr<FYSTagUsageIndex>, Index)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SYSTagUsageTab() override;

	// 콘텐츠 브라우저에서 들어올 때: 이 패키지들이 쓰는 태그를 보여준다.
	void ShowAssets(const TArray<FName>& PackageNames, const FText& Label);

private:
	using FItemPtr = TSharedPtr<FYSTagTreeItem>;
	using FNodePtr = TSharedPtr<FYSResultNode>;
	using FCodePtr = TSharedPtr<FYSTagCodeRef>;

	FYSTagUsageIndex* GetIndex() const;

	TSharedRef<SWidget> BuildToolbar();
	TSharedRef<SWidget> BuildTagPanel();
	TSharedRef<SWidget> BuildResultPanel();
	TSharedRef<SWidget> BuildFilterBar();

	void RebuildTree();
	void ApplyListMode();
	TSharedRef<ITableRow> OnGenerateTagRow(FItemPtr Item, const TSharedRef<STableViewBase>& Owner);
	void OnGetTagChildren(FItemPtr Item, TArray<FItemPtr>& OutChildren);
	void OnTagSelectionChanged(FItemPtr Item, ESelectInfo::Type SelectInfo);
	void OnTagExpansionChanged(FItemPtr Item, bool bExpanded);
	TSharedPtr<SWidget> OnTagContextMenu();

	// 태그 추가·삭제
	void OpenCreateDialog(const FString& InitialTag);
	void DeleteTag(FName TagName);
	// 트리에서 이 태그를 보이는 분류로 옮겨 선택한다.
	void SelectTag(FName TagName);

	void RefreshResults();
	void RefreshSummary(const TArray<FYSTagCodeRef>& RawCode);
	void RebuildTypeOptions(const TSet<FString>& Types);
	TSharedRef<ITableRow> OnGenerateResultRow(FNodePtr Node, const TSharedRef<STableViewBase>& Owner);
	void OnGetResultChildren(FNodePtr Node, TArray<FNodePtr>& OutChildren);
	void OnResultDoubleClicked(FNodePtr Node);
	TSharedRef<ITableRow> OnGenerateCodeRow(FCodePtr Ref, const TSharedRef<STableViewBase>& Owner);
	void OnCodeDoubleClicked(FCodePtr Ref);

	bool IsAssetMode() const { return !SelectedPackages.IsEmpty(); }
	bool HasSelection() const { return IsAssetMode() || !SelectedTag.IsNone(); }
	// 여러 태그가 섞여 보일 때만 행마다 태그를 붙인다.
	bool ShowsTagOnRows() const { return IsAssetMode() || bIncludeChildren; }
	FText GetStatusText() const;
	void OnIndexChanged();

	TWeakPtr<FYSTagUsageIndex> WeakIndex;

	EYSTagListMode ListMode = EYSTagListMode::Used;
	TArray<FItemPtr> AllRoots;
	TArray<FItemPtr> VisibleRoots;
	TMap<FName, FItemPtr> ItemsByName;
	TSet<FName> ExpandedTags;
	bool bFirstBuild = true;
	bool bRestoringTreeState = false;
	int32 UsedCount = 0;
	int32 DefinedOnlyCount = 0;
	int32 UnusedCount = 0;
	TSharedPtr<STreeView<FItemPtr>> TagTree;

	FName SelectedTag;
	TArray<FName> SelectedPackages;
	FText SelectedPackagesLabel;
	bool bIncludeChildren = false;
	bool bShowInherited = true;
	FString TypeFilter;
	FString TextFilter;

	FText SummaryDetail;
	int32 AssetTotal = 0;
	int32 CodeTotal = 0;
	int32 ConfigTotal = 0;
	int32 InheritedTotal = 0;

	TArray<FNodePtr> ResultRoots;
	int32 ShownRecordCount = 0;
	TSharedPtr<STreeView<FNodePtr>> ResultTree;
	TArray<FCodePtr> CodeRows;
	TSharedPtr<SListView<FCodePtr>> CodeList;
	TArray<TSharedPtr<FString>> TypeOptions;
	TSharedPtr<SComboBox<TSharedPtr<FString>>> TypeCombo;

	FDelegateHandle IndexChangedHandle;
	FDelegateHandle TagTreeChangedHandle;
};

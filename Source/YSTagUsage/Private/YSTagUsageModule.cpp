// Copyright Jokate. All Rights Reserved.

#include "AssetRegistry/AssetData.h"
#include "ContentBrowserMenuContexts.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Modules/ModuleManager.h"
#include "Styling/AppStyle.h"
#include "SYSTagUsageTab.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "YSTagUsageIndex.h"

#define LOCTEXT_NAMESPACE "FYSTagUsageModule"

class FYSTagUsageModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		// 탭을 열지 않아도 에디터 시작부터 스캔해 둔다.
		Index = MakeShared<FYSTagUsageIndex>();

		FGlobalTabmanager::Get()
			->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateRaw(this, &FYSTagUsageModule::SpawnTab))
			.SetDisplayName(LOCTEXT("TabTitle", "태그 사용처"))
			.SetTooltipText(LOCTEXT("TabTooltip", "게임플레이 태그가 어느 애셋의 어느 필드에서 쓰이는지 봅니다."))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Search")));

		UToolMenus::RegisterStartupCallback(
			FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FYSTagUsageModule::RegisterMenus));
	}

	virtual void ShutdownModule() override
	{
		UToolMenus::UnRegisterStartupCallback(this);
		UToolMenus::UnregisterOwner(this);

		if (FSlateApplication::IsInitialized())
		{
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
		}
		Index.Reset();
	}

private:
	static const FName TabName;

	TSharedPtr<FYSTagUsageIndex> Index;
	TWeakPtr<SYSTagUsageTab> TabWidget;

	void RegisterMenus()
	{
		FToolMenuOwnerScoped OwnerScoped(this);

		if (UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools")))
		{
			FToolMenuSection& Section = ToolsMenu->FindOrAddSection(TEXT("YS"), LOCTEXT("YSSection", "YS"));
			Section.AddMenuEntry(
				TEXT("OpenYSTagUsage"),
				LOCTEXT("OpenTagUsage", "태그 사용처"),
				LOCTEXT("OpenTagUsageTip", "게임플레이 태그 사용처 탭을 엽니다."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Search")),
				FUIAction(FExecuteAction::CreateLambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(TabName); })));
		}

		// 역방향: 애셋 → 이 애셋이 쓰는 태그
		if (UToolMenu* AssetMenu = UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu")))
		{
			FToolMenuSection& Section = AssetMenu->FindOrAddSection(TEXT("YS"), LOCTEXT("YSSection", "YS"));
			Section.AddDynamicEntry(TEXT("YSTagUsage"), FNewToolMenuSectionDelegate::CreateLambda([this](FToolMenuSection& InSection)
			{
				const UContentBrowserAssetContextMenuContext* Context = InSection.FindContext<UContentBrowserAssetContextMenuContext>();
				if (!Context || Context->SelectedAssets.IsEmpty())
				{
					return;
				}

				TArray<FAssetData> Assets = Context->SelectedAssets;
				InSection.AddMenuEntry(
					TEXT("ShowYSTagUsage"),
					LOCTEXT("ShowAssetTags", "이 애셋의 태그 사용처"),
					LOCTEXT("ShowAssetTagsTip", "선택한 애셋이 어느 필드에 어떤 태그를 세팅했는지 봅니다."),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Search")),
					FUIAction(FExecuteAction::CreateLambda([this, Assets]() { OpenForAssets(Assets); })));
			}));
		}
	}

	void OpenForAssets(const TArray<FAssetData>& Assets)
	{
		FGlobalTabmanager::Get()->TryInvokeTab(TabName);
		TSharedPtr<SYSTagUsageTab> Widget = TabWidget.Pin();
		if (!Widget.IsValid())
		{
			return;
		}

		TArray<FName> PackageNames;
		for (const FAssetData& Asset : Assets)
		{
			PackageNames.AddUnique(Asset.PackageName);
		}
		const FText Label = Assets.Num() == 1
			? FText::FromName(Assets[0].AssetName)
			: FText::Format(LOCTEXT("MultipleAssets", "{0}개"), Assets.Num());
		Widget->ShowAssets(PackageNames, Label);
	}

	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs& /*Args*/)
	{
		TSharedRef<SYSTagUsageTab> Widget = SNew(SYSTagUsageTab).Index(Index);
		TabWidget = Widget;
		return SNew(SDockTab)
			.TabRole(ETabRole::NomadTab)
			.Label(LOCTEXT("TabLabel", "태그 사용처"))
			[
				Widget
			];
	}
};

const FName FYSTagUsageModule::TabName(TEXT("YSTagUsage"));

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FYSTagUsageModule, YSTagUsage)

// Copyright Jokate. All Rights Reserved.

#include "SYSTagCreateDialog.h"

#include "Framework/Application/SlateApplication.h"
#include "GameplayTagsEditorModule.h"
#include "GameplayTagsManager.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SYSTagCreateDialog"

namespace
{
	TSharedRef<SWidget> MakeFieldLabel(const FText& Text)
	{
		return SNew(STextBlock)
			.Text(Text)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground());
	}

	FLinearColor WarningColor() { return FLinearColor(FColor::FromHex(TEXT("FAC775"))); }
	FLinearColor ErrorColor() { return FLinearColor(FColor::FromHex(TEXT("F09595"))); }
}

void SYSTagCreateDialog::OpenModal(const FString& InitialTag, const TSharedPtr<SWidget>& Parent, FOnTagCreated InOnTagCreated)
{
	TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "게임플레이 태그 추가"))
		.SizingRule(ESizingRule::Autosized)
		.SupportsMinimize(false)
		.SupportsMaximize(false);

	TSharedRef<SYSTagCreateDialog> Dialog = SNew(SYSTagCreateDialog)
		.InitialTag(InitialTag)
		.OnTagCreated(InOnTagCreated);
	Dialog->OwnerWindow = Window;

	Window->SetContent(Dialog);
	Window->SetWidgetToFocusOnActivate(Dialog->TagBox);
	FSlateApplication::Get().AddModalWindow(Window, Parent);
}

void SYSTagCreateDialog::Construct(const FArguments& InArgs)
{
	OnTagCreated = InArgs._OnTagCreated;

	TArray<FYSNativeTagFile> Files;
	YSTagUsage::FindNativeTagFiles(Files);
	for (FYSNativeTagFile& File : Files)
	{
		CppFiles.Add(MakeShared<FYSNativeTagFile>(MoveTemp(File)));
	}
	SelectedCpp = CppFiles.IsEmpty() ? nullptr : CppFiles[0];

	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();
	for (const EGameplayTagSourceType Type : { EGameplayTagSourceType::DefaultTagList, EGameplayTagSourceType::TagList })
	{
		TArray<const FGameplayTagSource*> Sources;
		Manager.FindTagSourcesWithType(Type, Sources);
		for (const FGameplayTagSource* Source : Sources)
		{
			IniSources.Add(MakeShared<FName>(Source->SourceName));
		}
	}
	SelectedIni = IniSources.IsEmpty() ? nullptr : IniSources[0];

	ChildSlot
	[
		SNew(SBox)
		.WidthOverride(480.f)
		.Padding(16.f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				MakeFieldLabel(LOCTEXT("TagLabel", "태그 이름"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
			[
				SAssignNew(TagBox, SEditableTextBox)
				.Text(FText::FromString(InArgs._InitialTag))
				.HintText(LOCTEXT("TagHint", "State.Resource.NotFull"))
				.OnTextChanged(this, &SYSTagCreateDialog::OnTagTextChanged)
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				MakeFieldLabel(LOCTEXT("CommentLabel", "설명 (선택)"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
			[
				SAssignNew(CommentBox, SEditableTextBox)
				.HintText(LOCTEXT("CommentHint", "자원이 가득 차지 않았을 때"))
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				MakeFieldLabel(LOCTEXT("TargetLabel", "정의 위치"))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f).HAlign(HAlign_Left)
			[
				SNew(SSegmentedControl<ETarget>)
				.Value_Lambda([this]() { return Target; })
				.OnValueChanged_Lambda([this](ETarget NewTarget) { Target = NewTarget; ResultError = FText::GetEmpty(); })
				+ SSegmentedControl<ETarget>::Slot(ETarget::Cpp)
				.Text(LOCTEXT("TargetCpp", "C++ 파일"))
				+ SSegmentedControl<ETarget>::Slot(ETarget::Ini)
				.Text(LOCTEXT("TargetIni", "ini"))
			]

			// C++
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SVerticalBox)
				.Visibility_Lambda([this]() { return Target == ETarget::Cpp ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					MakeFieldLabel(LOCTEXT("CppFileLabel", "C++ 파일"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					SNew(SComboBox<TSharedPtr<FYSNativeTagFile>>)
					.OptionsSource(&CppFiles)
					.InitiallySelectedItem(SelectedCpp)
					.OnGenerateWidget_Lambda([](TSharedPtr<FYSNativeTagFile> File)
					{
						return SNew(STextBlock).Text(FText::FromString(File->GetLabel()));
					})
					.OnSelectionChanged_Lambda([this](TSharedPtr<FYSNativeTagFile> File, ESelectInfo::Type)
					{
						SelectedCpp = File;
						ResultError = FText::GetEmpty();
					})
					[
						SNew(STextBlock)
						.Text_Lambda([this]() { return SelectedCpp.IsValid() ? FText::FromString(SelectedCpp->GetLabel()) : LOCTEXT("NoCppFile", "태그 정의 파일이 없습니다"); })
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Visibility_Lambda([this]() { return SelectedCpp.IsValid() && SelectedCpp->bGenerated ? EVisibility::Visible : EVisibility::Collapsed; })
					.Text(LOCTEXT("GeneratedWarning", "생성기 파일입니다. GenerateGameplayTags 를 다시 돌리면 여기 넣은 태그는 사라집니다."))
					.ColorAndOpacity(WarningColor())
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					MakeFieldLabel(LOCTEXT("VarLabel", "C++ 변수 이름"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SAssignNew(VarBox, SEditableTextBox)
					.OnTextChanged_Lambda([this](const FText&)
					{
						if (!bSettingVar)
						{
							bVarEdited = true;
							ResultError = FText::GetEmpty();
						}
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Text(LOCTEXT("CppNote", "정의(.cpp)와 선언(같은 이름의 .h)을 한 줄씩 추가합니다. 빌드하기 전까지는 에디터 임시 태그로 보입니다."))
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				]
			]

			// ini
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SVerticalBox)
				.Visibility_Lambda([this]() { return Target == ETarget::Ini ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				[
					MakeFieldLabel(LOCTEXT("IniLabel", "ini 파일"))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
				[
					SNew(SComboBox<TSharedPtr<FName>>)
					.OptionsSource(&IniSources)
					.InitiallySelectedItem(SelectedIni)
					.OnGenerateWidget_Lambda([](TSharedPtr<FName> Source)
					{
						return SNew(STextBlock).Text(FText::FromName(*Source));
					})
					.OnSelectionChanged_Lambda([this](TSharedPtr<FName> Source, ESelectInfo::Type)
					{
						SelectedIni = Source;
						ResultError = FText::GetEmpty();
					})
					[
						SNew(STextBlock)
						.Text_Lambda([this]() { return SelectedIni.IsValid() ? FText::FromName(*SelectedIni) : LOCTEXT("NoIni", "태그 ini 가 없습니다"); })
					]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text_Lambda([this]() { return ResultError.IsEmpty() ? Validate() : ResultError; })
				.ColorAndOpacity(ErrorColor())
			]

			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 8.f, 0.f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), TEXT("PrimaryButton"))
					.Text(LOCTEXT("Create", "만들기"))
					.IsEnabled(this, &SYSTagCreateDialog::CanCreate)
					.OnClicked(this, &SYSTagCreateDialog::OnCreateClicked)
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "취소"))
					.OnClicked_Lambda([this]() { Close(); return FReply::Handled(); })
				]
			]
		]
	];

	OnTagTextChanged(TagBox->GetText());
}

FString SYSTagCreateDialog::GetTagString() const
{
	return TagBox.IsValid() ? TagBox->GetText().ToString().TrimStartAndEnd() : FString();
}

FText SYSTagCreateDialog::Validate() const
{
	const FString TagString = GetTagString();
	if (TagString.IsEmpty() || TagString.EndsWith(TEXT(".")))
	{
		// 입력 중에는 에러를 띄우지 않는다. 만들기 버튼은 CanCreate 가 막는다.
		return FText::GetEmpty();
	}

	FText Error;
	FString Fixed;
	if (!UGameplayTagsManager::Get().IsValidGameplayTagString(TagString, &Error, &Fixed))
	{
		return Error;
	}
	const TSharedPtr<FGameplayTagNode> Node = UGameplayTagsManager::Get().FindTagNode(FName(*TagString));
	if (Node.IsValid() && Node->IsExplicitTag())
	{
		return LOCTEXT("TagExists", "이미 있는 태그입니다.");
	}

	if (Target == ETarget::Cpp)
	{
		if (!SelectedCpp.IsValid())
		{
			return LOCTEXT("PickCpp", "C++ 파일을 고르세요.");
		}
		if (VarBox.IsValid() && VarBox->GetText().IsEmpty())
		{
			return LOCTEXT("EnterVar", "C++ 변수 이름을 입력하세요.");
		}
	}
	else if (!SelectedIni.IsValid())
	{
		return LOCTEXT("PickIni", "ini 파일을 고르세요.");
	}
	return FText::GetEmpty();
}

void SYSTagCreateDialog::OnTagTextChanged(const FText& Text)
{
	ResultError = FText::GetEmpty();
	if (!bVarEdited && VarBox.IsValid())
	{
		TGuardValue<bool> Guard(bSettingVar, true);
		VarBox->SetText(FText::FromString(YSTagUsage::MakeVarName(Text.ToString().TrimStartAndEnd())));
	}
}

bool SYSTagCreateDialog::CanCreate() const
{
	const FString TagString = GetTagString();
	return !TagString.IsEmpty() && !TagString.EndsWith(TEXT(".")) && Validate().IsEmpty();
}

FReply SYSTagCreateDialog::OnCreateClicked()
{
	if (!CanCreate())
	{
		return FReply::Handled();
	}

	const FString TagString = GetTagString();
	const FString Comment = CommentBox->GetText().ToString().TrimStartAndEnd();
	IGameplayTagsEditorModule& TagsEditor = IGameplayTagsEditorModule::Get();

	if (Target == ETarget::Ini)
	{
		if (!TagsEditor.AddNewGameplayTagToINI(TagString, Comment, *SelectedIni))
		{
			ResultError = LOCTEXT("IniFailed", "ini 에 추가하지 못했습니다. 에디터 알림을 확인하세요.");
			return FReply::Handled();
		}
	}
	else
	{
		FText Error;
		if (!YSTagUsage::AddNativeTag(*SelectedCpp, VarBox->GetText().ToString().TrimStartAndEnd(), TagString, Comment, Error))
		{
			ResultError = Error;
			return FReply::Handled();
		}
		// 빌드 전까지 목록에서 보이도록 임시 태그로 올린다.
		TagsEditor.AddTransientEditorGameplayTag(TagString);
	}

	OnTagCreated.ExecuteIfBound(FName(*TagString));
	Close();
	return FReply::Handled();
}

void SYSTagCreateDialog::Close()
{
	if (const TSharedPtr<SWindow> Window = OwnerWindow.Pin())
	{
		Window->RequestDestroyWindow();
	}
}

#undef LOCTEXT_NAMESPACE

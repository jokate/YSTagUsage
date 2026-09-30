// Copyright Jokate. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "YSTagSourceEditor.h"

class SEditableTextBox;
class SWindow;

class SYSTagCreateDialog : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnTagCreated, FName /*Tag*/);

	SLATE_BEGIN_ARGS(SYSTagCreateDialog) {}
		SLATE_ARGUMENT(FString, InitialTag)
		SLATE_EVENT(FOnTagCreated, OnTagCreated)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// 모달 창으로 띄운다. 창이 닫힐 때까지 돌아오지 않는다.
	static void OpenModal(const FString& InitialTag, const TSharedPtr<SWidget>& Parent, FOnTagCreated OnTagCreated);

private:
	enum class ETarget : uint8
	{
		Cpp,
		Ini,
	};

	FString GetTagString() const;
	FText Validate() const;
	bool CanCreate() const;
	void OnTagTextChanged(const FText& Text);
	FReply OnCreateClicked();
	void Close();

	FOnTagCreated OnTagCreated;
	TWeakPtr<SWindow> OwnerWindow;
	ETarget Target = ETarget::Cpp;

	TSharedPtr<SEditableTextBox> TagBox;
	TSharedPtr<SEditableTextBox> CommentBox;
	TSharedPtr<SEditableTextBox> VarBox;
	// 변수 이름을 손으로 고쳤으면 태그 이름을 따라 바꾸지 않는다.
	bool bVarEdited = false;
	bool bSettingVar = false;

	TArray<TSharedPtr<FYSNativeTagFile>> CppFiles;
	TSharedPtr<FYSNativeTagFile> SelectedCpp;
	TArray<TSharedPtr<FName>> IniSources;
	TSharedPtr<FName> SelectedIni;

	FText ResultError;
};

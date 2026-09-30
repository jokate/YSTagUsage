// Copyright Jokate. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "YSTagCodeScanner.h"

// UE_DEFINE_GAMEPLAY_TAG 를 모아 둔 .cpp 와 짝 헤더
struct FYSNativeTagFile
{
	FString CppPath;
	// 같은 이름의 헤더. 없으면 비어 있다.
	FString HeaderPath;
	FString Namespace;
	// GenerateGameplayTags 가 만드는 파일. 손으로 고치면 다음 생성 때 덮인다.
	bool bGenerated = false;

	FString GetLabel() const;
};

namespace YSTagUsage
{
	void FindNativeTagFiles(TArray<FYSNativeTagFile>& OutFiles);

	// "State.Resource.NotFull" → "State_Resource_NotFull"
	FString MakeVarName(const FString& Tag);

	bool AddNativeTag(const FYSNativeTagFile& File, const FString& VarName, const FString& Tag, const FString& Comment, FText& OutError);

	// 태그의 정의·선언 줄. 지우기 전에 확인창에 보여줄 때 쓴다.
	void FindNativeTagLines(FName Tag, TArray<FYSTagCodeRef>& OutLines);

	bool RemoveNativeTag(FName Tag, int32& OutRemovedLines, FText& OutError);
}

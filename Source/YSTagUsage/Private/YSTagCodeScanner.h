// Copyright Jokate. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FYSTagCodeRef
{
	FName Tag;
	FString File;
	int32 Line = 0;
	FString Snippet;
	// UE_DEFINE_GAMEPLAY_TAG 줄. 사용처로 세지 않는다.
	bool bDefinition = false;
	// Config/*.ini 의 설정값
	bool bConfig = false;
};

namespace YSTagUsage
{
	// 프로젝트 Source/ 를 텍스트로 훑는다.
	// 사용처 = "네임스페이스::태그변수" 참조, using namespace 한 파일의 맨 변수 참조, 알려진 태그 이름 문자열.
	// 문자열 조합·매크로로 만든 태그, 부모 태그 매칭은 못 잡는다.
	void ScanSource(const TSet<FName>& KnownTags, TArray<FYSTagCodeRef>& OutRefs);

	// 프로젝트 Config/*.ini 에서 태그 이름이 들어간 설정값을 찾는다. 태그 정의 섹션은 뺀다.
	void ScanConfig(const TSet<FName>& KnownTags, TArray<FYSTagCodeRef>& OutRefs);
}

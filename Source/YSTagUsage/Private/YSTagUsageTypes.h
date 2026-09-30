// Copyright Jokate. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

enum class EYSTagPathKind : uint8
{
	// 현재 구조체·오브젝트의 프로퍼티. Index 는 고정 배열(ArrayDim > 1)일 때만 쓴다.
	Field,
	// 배열·셋 원소, 맵 값
	Element,
	// 맵 키
	MapKey,
};

struct FYSTagPathSegment
{
	EYSTagPathKind Kind = EYSTagPathKind::Field;
	FName Property;
	int32 Index = INDEX_NONE;
};

struct FYSTagUsageRecord
{
	FName Tag;
	FName PackageName;

	// 콘텐츠 브라우저에 보이는 애셋. 더블클릭하면 이걸 연다.
	FSoftObjectPath Asset;

	// 경로가 시작되는 오브젝트. BP 는 CDO, 그 외는 애셋 자신.
	// 어디에도 매달리지 않은 서브오브젝트(SCS 컴포넌트 템플릿 등)는 그 오브젝트 자신이다.
	FSoftObjectPath Root;

	FString TypeName;
	TArray<FYSTagPathSegment> Path;
	FString DisplayPath;

	// 같은 경로에서 아키타입(부모 CDO 등)과 같은 태그를 들고 있으면 상속으로 본다.
	bool bInherited = false;
};

struct FYSPackageUsage
{
	// 패키지 저장 해시. 달라지면 다시 스캔한다.
	FString SavedKey;
	TArray<FYSTagUsageRecord> Records;
};

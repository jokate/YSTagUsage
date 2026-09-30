// Copyright Jokate. All Rights Reserved.

#pragma once

#include "YSTagUsageTypes.h"

class UPackage;

namespace YSTagUsage
{
	// 로드된 패키지에서 편집 가능한 태그 필드를 모두 뽑는다.
	void ExtractPackage(UPackage* Package, TArray<FYSTagUsageRecord>& OutRecords);
}

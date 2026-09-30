// Copyright Jokate. All Rights Reserved.

using UnrealBuildTool;

public class YSTagUsage : ModuleRules
{
	public YSTagUsage(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Slate",
			"SlateCore",
			"UnrealEd",
			"ToolMenus",
			"WorkspaceMenuStructure",
			"AssetRegistry",
			"GameplayTags",
			// 태그 ini 추가·삭제
			"GameplayTagsEditor",
			"Json",
			"PropertyEditor",
			"ContentBrowser",
			// 블루프린트 에디터의 클래스 디폴트 패널을 여는 데 사용
			"Kismet",
			// 태그 이름 클립보드 복사
			"ApplicationCore",
		});
	}
}

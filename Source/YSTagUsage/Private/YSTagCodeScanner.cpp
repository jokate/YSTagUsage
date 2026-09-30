// Copyright Jokate. All Rights Reserved.

#include "YSTagCodeScanner.h"

#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace YSTagUsage
{
namespace
{
	constexpr int32 MaxSnippetLen = 160;

	struct FTagVar
	{
		FName Tag;
		FString Namespace;
	};

	struct FSourceFile
	{
		FString Path;
		TArray<FString> Lines;
	};

	FString MakeSnippet(const FString& Line)
	{
		FString Snippet = Line.TrimStartAndEnd();
		if (Snippet.Len() > MaxSnippetLen)
		{
			Snippet = Snippet.Left(MaxSnippetLen) + TEXT("…");
		}
		return Snippet;
	}

	// 줄 주석과 블록 주석 연속 줄은 버린다. 문자열 안의 // 는 드물어서 신경 쓰지 않는다.
	FString StripComment(const FString& Line)
	{
		const FString Trimmed = Line.TrimStart();
		if (Trimmed.StartsWith(TEXT("*")) || Trimmed.StartsWith(TEXT("/*")))
		{
			return FString();
		}
		const int32 CommentStart = Line.Find(TEXT("//"));
		return CommentStart == INDEX_NONE ? Line : Line.Left(CommentStart);
	}

	bool IsTagMacroLine(const FString& Line)
	{
		return Line.Contains(TEXT("UE_DEFINE_GAMEPLAY_TAG")) || Line.Contains(TEXT("UE_DECLARE_GAMEPLAY_TAG"));
	}
}

void ScanSource(const TSet<FName>& KnownTags, TArray<FYSTagCodeRef>& OutRefs)
{
	const FString SourceDir = FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir());
	TArray<FString> Paths;
	IFileManager::Get().FindFilesRecursive(Paths, *SourceDir, TEXT("*.h"), true, false);
	IFileManager::Get().FindFilesRecursive(Paths, *SourceDir, TEXT("*.cpp"), true, false, false);

	TArray<FSourceFile> Files;
	for (const FString& Path : Paths)
	{
		FSourceFile& File = Files.AddDefaulted_GetRef();
		File.Path = Path;
		FFileHelper::LoadFileToStringArray(File.Lines, *Path);
	}

	// 1) 태그 변수 정의
	const FRegexPattern DefinePattern(TEXT("UE_DEFINE_GAMEPLAY_TAG(?:_COMMENT|_STATIC)?\\(\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*,\\s*\"([^\"]+)\""));
	const FRegexPattern NamespacePattern(TEXT("^\\s*namespace\\s+([A-Za-z_][A-Za-z0-9_]*)"));

	TMap<FString, FName> QualifiedVars;
	TMap<FString, TArray<FTagVar>> VarsByName;
	for (const FSourceFile& File : Files)
	{
		FString CurrentNamespace;
		for (int32 Index = 0; Index < File.Lines.Num(); ++Index)
		{
			const FString& Line = File.Lines[Index];
			FRegexMatcher NamespaceMatcher(NamespacePattern, Line);
			if (NamespaceMatcher.FindNext())
			{
				CurrentNamespace = NamespaceMatcher.GetCaptureGroup(1);
			}

			FRegexMatcher DefineMatcher(DefinePattern, Line);
			if (!DefineMatcher.FindNext())
			{
				continue;
			}
			const FString Var = DefineMatcher.GetCaptureGroup(1);
			const FName Tag(*DefineMatcher.GetCaptureGroup(2));
			VarsByName.FindOrAdd(Var).Add({ Tag, CurrentNamespace });
			if (!CurrentNamespace.IsEmpty())
			{
				QualifiedVars.Add(CurrentNamespace + TEXT("::") + Var, Tag);
			}
			OutRefs.Add({ Tag, File.Path, Index + 1, MakeSnippet(Line), true });
		}
	}

	// 2) 사용처
	// A::B::C 에서 B::C 도 보려고 뒤쪽 이름은 전방탐색으로 잡는다.
	const FRegexPattern QualifiedPattern(TEXT("([A-Za-z_][A-Za-z0-9_]*)::(?=([A-Za-z_][A-Za-z0-9_]*))"));
	const FRegexPattern IdentifierPattern(TEXT("[A-Za-z_][A-Za-z0-9_]*"));
	const FRegexPattern StringPattern(TEXT("\"([A-Za-z0-9_.]+)\""));
	const FRegexPattern UsingPattern(TEXT("using\\s+namespace\\s+([A-Za-z_][A-Za-z0-9_]*)"));

	for (const FSourceFile& File : Files)
	{
		TSet<FString> OpenNamespaces;
		for (const FString& Line : File.Lines)
		{
			FRegexMatcher UsingMatcher(UsingPattern, Line);
			if (UsingMatcher.FindNext())
			{
				OpenNamespaces.Add(UsingMatcher.GetCaptureGroup(1));
			}
		}

		for (int32 Index = 0; Index < File.Lines.Num(); ++Index)
		{
			const FString& RawLine = File.Lines[Index];
			if (IsTagMacroLine(RawLine))
			{
				continue;
			}
			const FString Line = StripComment(RawLine);
			if (Line.IsEmpty())
			{
				continue;
			}

			TSet<FName> LineTags;

			FRegexMatcher QualifiedMatcher(QualifiedPattern, Line);
			while (QualifiedMatcher.FindNext())
			{
				const FString Key = QualifiedMatcher.GetCaptureGroup(1) + TEXT("::") + QualifiedMatcher.GetCaptureGroup(2);
				if (const FName* Tag = QualifiedVars.Find(Key))
				{
					LineTags.Add(*Tag);
				}
			}

			if (!OpenNamespaces.IsEmpty())
			{
				FRegexMatcher IdentifierMatcher(IdentifierPattern, Line);
				while (IdentifierMatcher.FindNext())
				{
					if (const TArray<FTagVar>* Vars = VarsByName.Find(IdentifierMatcher.GetCaptureGroup(0)))
					{
						for (const FTagVar& Var : *Vars)
						{
							if (OpenNamespaces.Contains(Var.Namespace))
							{
								LineTags.Add(Var.Tag);
							}
						}
					}
				}
			}

			FRegexMatcher StringMatcher(StringPattern, Line);
			while (StringMatcher.FindNext())
			{
				const FName Candidate(*StringMatcher.GetCaptureGroup(1), FNAME_Find);
				if (!Candidate.IsNone() && KnownTags.Contains(Candidate))
				{
					LineTags.Add(Candidate);
				}
			}

			for (const FName Tag : LineTags)
			{
				OutRefs.Add({ Tag, File.Path, Index + 1, MakeSnippet(RawLine), false });
			}
		}
	}
}

void ScanConfig(const TSet<FName>& KnownTags, TArray<FYSTagCodeRef>& OutRefs)
{
	TArray<FString> Paths;
	IFileManager::Get().FindFilesRecursive(Paths, *FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir()), TEXT("*.ini"), true, false);

	// 점으로 이은 이름은 어디에 있든, 한 마디 태그는 TagName= 뒤에 올 때만 태그로 본다.
	const FRegexPattern DottedPattern(TEXT("[A-Za-z0-9_]+(?:\\.[A-Za-z0-9_]+)+"));
	const FRegexPattern TagNamePattern(TEXT("TagName\\s*=\\s*\"?([A-Za-z0-9_]+)"));

	for (const FString& Path : Paths)
	{
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *Path);

		bool bTagDefinitionSection = false;
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			const FString Line = Lines[Index].TrimStartAndEnd();
			if (Line.StartsWith(TEXT("[")))
			{
				bTagDefinitionSection = Line.StartsWith(TEXT("[/Script/GameplayTags.GameplayTagsSettings]"))
					|| Line.StartsWith(TEXT("[/Script/GameplayTags.GameplayTagsList]"));
				continue;
			}
			if (bTagDefinitionSection || Line.IsEmpty() || Line.StartsWith(TEXT(";")))
			{
				continue;
			}

			TSet<FName> LineTags;
			for (const FRegexPattern* Pattern : { &DottedPattern, &TagNamePattern })
			{
				FRegexMatcher Matcher(*Pattern, Line);
				const int32 Group = Pattern == &DottedPattern ? 0 : 1;
				while (Matcher.FindNext())
				{
					const FName Candidate(*Matcher.GetCaptureGroup(Group), FNAME_Find);
					if (!Candidate.IsNone() && KnownTags.Contains(Candidate))
					{
						LineTags.Add(Candidate);
					}
				}
			}

			for (const FName Tag : LineTags)
			{
				FYSTagCodeRef& Ref = OutRefs.Add_GetRef({ Tag, Path, Index + 1, MakeSnippet(Line), false });
				Ref.bConfig = true;
			}
		}
	}
}
}

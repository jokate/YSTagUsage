// Copyright Jokate. All Rights Reserved.

#include "YSTagSourceEditor.h"

#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "YSTagSourceEditor"

namespace YSTagUsage
{
namespace
{
	const TCHAR* DefinePatternText = TEXT("UE_DEFINE_GAMEPLAY_TAG(?:_COMMENT|_STATIC)?\\(\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*,\\s*\"([^\"]+)\"");
	const TCHAR* NamespacePatternText = TEXT("^\\s*namespace\\s+([A-Za-z_][A-Za-z0-9_]*)");

	// 인코딩(BOM)·줄바꿈을 그대로 되돌려 쓰려고 원래 모양을 기억해 둔다.
	struct FTextFile
	{
		FString Path;
		bool bBom = false;
		bool bTrailingEol = false;
		FString Eol = TEXT("\r\n");
		TArray<FString> Lines;
	};

	bool LoadTextFile(const FString& Path, FTextFile& Out)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path))
		{
			return false;
		}
		Out.Path = Path;
		Out.bBom = Bytes.Num() >= 3 && Bytes[0] == 0xEF && Bytes[1] == 0xBB && Bytes[2] == 0xBF;

		FString Text;
		FFileHelper::BufferToString(Text, Bytes.GetData(), Bytes.Num());
		Out.Eol = Text.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
		Out.bTrailingEol = Text.EndsWith(TEXT("\n"));
		Text.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
		Text.ParseIntoArray(Out.Lines, TEXT("\n"), false);
		if (Out.bTrailingEol && !Out.Lines.IsEmpty() && Out.Lines.Last().IsEmpty())
		{
			Out.Lines.Pop();
		}
		return true;
	}

	bool CheckWritable(const FString& Path, FText& OutError)
	{
		if (IFileManager::Get().IsReadOnly(*Path))
		{
			OutError = FText::Format(LOCTEXT("ReadOnly", "{0} 이(가) 읽기 전용입니다."), FText::FromString(FPaths::GetCleanFilename(Path)));
			return false;
		}
		return true;
	}

	bool SaveTextFile(const FTextFile& File, FText& OutError)
	{
		FString Text = FString::Join(File.Lines, *File.Eol);
		if (File.bTrailingEol)
		{
			Text += File.Eol;
		}
		const FFileHelper::EEncodingOptions Encoding = File.bBom
			? FFileHelper::EEncodingOptions::ForceUTF8
			: FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM;
		if (!FFileHelper::SaveStringToFile(Text, *File.Path, Encoding))
		{
			OutError = FText::Format(LOCTEXT("SaveFailed", "{0} 저장에 실패했습니다."), FText::FromString(FPaths::GetCleanFilename(File.Path)));
			return false;
		}
		return true;
	}

	TArray<FString> FindSourceFiles(const TCHAR* Wildcard)
	{
		TArray<FString> Paths;
		IFileManager::Get().FindFilesRecursive(Paths, *FPaths::ConvertRelativePathToFull(FPaths::GameSourceDir()), Wildcard, true, false);
		return Paths;
	}

	FString ReadNamespace(const FString& Line, const FString& Current)
	{
		const FRegexPattern Pattern(NamespacePatternText);
		FRegexMatcher Matcher(Pattern, Line);
		return Matcher.FindNext() ? Matcher.GetCaptureGroup(1) : Current;
	}

	int32 FindLastLine(const TArray<FString>& Lines, const TCHAR* Needle)
	{
		for (int32 Index = Lines.Num() - 1; Index >= 0; --Index)
		{
			if (Lines[Index].Contains(Needle))
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	FString LeadingWhitespace(const FString& Line)
	{
		int32 Count = 0;
		while (Count < Line.Len() && FChar::IsWhitespace(Line[Count]))
		{
			++Count;
		}
		return Line.Left(Count);
	}

	bool IsIdentifier(const FString& Name)
	{
		if (Name.IsEmpty() || FChar::IsDigit(Name[0]))
		{
			return false;
		}
		for (const TCHAR Char : Name)
		{
			if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}

	// 태그를 정의·선언한 줄. 파일 → 줄 인덱스
	TMap<FString, TArray<int32>> FindTagLines(FName Tag)
	{
		const FString TagString = Tag.ToString();
		const FRegexPattern DefinePattern(DefinePatternText);

		struct FDefinition
		{
			FString Var;
			FString Namespace;
		};
		TArray<FDefinition> Definitions;
		TMap<FString, TArray<int32>> Result;

		for (const FString& Path : FindSourceFiles(TEXT("*.cpp")))
		{
			FTextFile File;
			if (!LoadTextFile(Path, File))
			{
				continue;
			}
			FString Namespace;
			for (int32 Index = 0; Index < File.Lines.Num(); ++Index)
			{
				Namespace = ReadNamespace(File.Lines[Index], Namespace);
				FRegexMatcher Matcher(DefinePattern, File.Lines[Index]);
				if (Matcher.FindNext() && Matcher.GetCaptureGroup(2) == TagString)
				{
					Definitions.Add({ Matcher.GetCaptureGroup(1), Namespace });
					Result.FindOrAdd(Path).Add(Index);
				}
			}
		}

		if (Definitions.IsEmpty())
		{
			return Result;
		}

		for (const FString& Path : FindSourceFiles(TEXT("*.h")))
		{
			FTextFile File;
			if (!LoadTextFile(Path, File))
			{
				continue;
			}
			FString Namespace;
			for (int32 Index = 0; Index < File.Lines.Num(); ++Index)
			{
				Namespace = ReadNamespace(File.Lines[Index], Namespace);
				for (const FDefinition& Definition : Definitions)
				{
					if (Definition.Namespace != Namespace)
					{
						continue;
					}
					const FRegexPattern DeclarePattern(FString::Printf(TEXT("UE_DECLARE_GAMEPLAY_TAG_EXTERN\\(\\s*%s\\s*\\)"), *Definition.Var));
					FRegexMatcher Matcher(DeclarePattern, File.Lines[Index]);
					if (Matcher.FindNext())
					{
						Result.FindOrAdd(Path).Add(Index);
					}
				}
			}
		}
		return Result;
	}
}

void FindNativeTagFiles(TArray<FYSNativeTagFile>& OutFiles)
{
	TMap<FString, FString> HeadersByBase;
	for (const FString& Path : FindSourceFiles(TEXT("*.h")))
	{
		HeadersByBase.Add(FPaths::GetBaseFilename(Path), Path);
	}

	for (const FString& Path : FindSourceFiles(TEXT("*.cpp")))
	{
		FTextFile File;
		if (!LoadTextFile(Path, File) || FindLastLine(File.Lines, TEXT("UE_DEFINE_GAMEPLAY_TAG")) == INDEX_NONE)
		{
			continue;
		}

		FYSNativeTagFile& Entry = OutFiles.AddDefaulted_GetRef();
		Entry.CppPath = Path;
		const FString Base = FPaths::GetBaseFilename(Path);
		Entry.HeaderPath = HeadersByBase.FindRef(Base);
		Entry.bGenerated = Base.Contains(TEXT("Generated"));
		for (const FString& Line : File.Lines)
		{
			Entry.Namespace = ReadNamespace(Line, Entry.Namespace);
			if (!Entry.Namespace.IsEmpty())
			{
				break;
			}
		}
	}

	OutFiles.Sort([](const FYSNativeTagFile& A, const FYSNativeTagFile& B)
	{
		// 생성기 파일은 뒤로
		return A.bGenerated != B.bGenerated ? !A.bGenerated : A.GetLabel() < B.GetLabel();
	});
}

FString MakeVarName(const FString& Tag)
{
	FString Var;
	for (const TCHAR Char : Tag)
	{
		Var.AppendChar(FChar::IsAlnum(Char) ? Char : TEXT('_'));
	}
	if (!Var.IsEmpty() && FChar::IsDigit(Var[0]))
	{
		Var.InsertAt(0, TEXT('_'));
	}
	return Var;
}

bool AddNativeTag(const FYSNativeTagFile& File, const FString& VarName, const FString& Tag, const FString& Comment, FText& OutError)
{
	if (!IsIdentifier(VarName))
	{
		OutError = LOCTEXT("BadVarName", "C++ 변수 이름은 영문·숫자·밑줄만 쓸 수 있고 숫자로 시작할 수 없습니다.");
		return false;
	}

	FTextFile Cpp;
	if (!LoadTextFile(File.CppPath, Cpp))
	{
		OutError = LOCTEXT("CppLoadFailed", "C++ 파일을 읽지 못했습니다.");
		return false;
	}

	const FRegexPattern SameVarPattern(FString::Printf(TEXT("UE_DEFINE_GAMEPLAY_TAG\\w*\\(\\s*%s\\s*,"), *VarName));
	for (const FString& Line : Cpp.Lines)
	{
		FRegexMatcher Matcher(SameVarPattern, Line);
		if (Matcher.FindNext())
		{
			OutError = FText::Format(LOCTEXT("VarExists", "{0} 은(는) 이 파일에 이미 있는 변수입니다."), FText::FromString(VarName));
			return false;
		}
	}

	const int32 LastDefine = FindLastLine(Cpp.Lines, TEXT("UE_DEFINE_GAMEPLAY_TAG"));
	if (LastDefine == INDEX_NONE)
	{
		OutError = LOCTEXT("NoDefine", "이 파일에서 태그 정의 위치를 찾지 못했습니다.");
		return false;
	}

	FTextFile Header;
	int32 LastDeclare = INDEX_NONE;
	const bool bHasHeader = !File.HeaderPath.IsEmpty() && LoadTextFile(File.HeaderPath, Header);
	if (bHasHeader)
	{
		LastDeclare = FindLastLine(Header.Lines, TEXT("UE_DECLARE_GAMEPLAY_TAG_EXTERN"));
	}

	if (!CheckWritable(Cpp.Path, OutError) || (LastDeclare != INDEX_NONE && !CheckWritable(Header.Path, OutError)))
	{
		return false;
	}

	const FString Indent = LeadingWhitespace(Cpp.Lines[LastDefine]);
	const FString DefineLine = Comment.IsEmpty()
		? FString::Printf(TEXT("%sUE_DEFINE_GAMEPLAY_TAG(%s, \"%s\");"), *Indent, *VarName, *Tag)
		: FString::Printf(TEXT("%sUE_DEFINE_GAMEPLAY_TAG_COMMENT(%s, \"%s\", \"%s\");"), *Indent, *VarName, *Tag, *Comment.ReplaceCharWithEscapedChar());
	Cpp.Lines.Insert(DefineLine, LastDefine + 1);

	if (LastDeclare != INDEX_NONE)
	{
		const FString HeaderIndent = LeadingWhitespace(Header.Lines[LastDeclare]);
		Header.Lines.Insert(FString::Printf(TEXT("%sUE_DECLARE_GAMEPLAY_TAG_EXTERN(%s);"), *HeaderIndent, *VarName), LastDeclare + 1);
		if (!SaveTextFile(Header, OutError))
		{
			return false;
		}
	}
	return SaveTextFile(Cpp, OutError);
}

void FindNativeTagLines(FName Tag, TArray<FYSTagCodeRef>& OutLines)
{
	for (const TPair<FString, TArray<int32>>& Pair : FindTagLines(Tag))
	{
		FTextFile File;
		if (!LoadTextFile(Pair.Key, File))
		{
			continue;
		}
		for (const int32 Index : Pair.Value)
		{
			FYSTagCodeRef& Ref = OutLines.AddDefaulted_GetRef();
			Ref.Tag = Tag;
			Ref.File = Pair.Key;
			Ref.Line = Index + 1;
			Ref.Snippet = File.Lines[Index].TrimStartAndEnd();
			Ref.bDefinition = true;
		}
	}
}

bool RemoveNativeTag(FName Tag, int32& OutRemovedLines, FText& OutError)
{
	OutRemovedLines = 0;
	const TMap<FString, TArray<int32>> TagLines = FindTagLines(Tag);
	if (TagLines.IsEmpty())
	{
		OutError = LOCTEXT("NoNativeDefinition", "프로젝트 Source/ 에서 이 태그의 C++ 정의를 찾지 못했습니다.");
		return false;
	}

	for (const TPair<FString, TArray<int32>>& Pair : TagLines)
	{
		if (!CheckWritable(Pair.Key, OutError))
		{
			return false;
		}
	}

	for (const TPair<FString, TArray<int32>>& Pair : TagLines)
	{
		FTextFile File;
		if (!LoadTextFile(Pair.Key, File))
		{
			OutError = FText::Format(LOCTEXT("LoadFailed", "{0} 을(를) 읽지 못했습니다."), FText::FromString(FPaths::GetCleanFilename(Pair.Key)));
			return false;
		}

		TArray<int32> Indices = Pair.Value;
		// 뒤에서부터 지워야 앞쪽 인덱스가 밀리지 않는다.
		Indices.Sort([](int32 A, int32 B) { return A > B; });
		for (const int32 Index : Indices)
		{
			File.Lines.RemoveAt(Index);
			++OutRemovedLines;
		}
		if (!SaveTextFile(File, OutError))
		{
			return false;
		}
	}
	return true;
}
}

FString FYSNativeTagFile::GetLabel() const
{
	FString Label = FPaths::GetCleanFilename(CppPath);
	if (!Namespace.IsEmpty())
	{
		Label += FString::Printf(TEXT("  (%s)"), *Namespace);
	}
	if (bGenerated)
	{
		Label += TEXT("  · 생성기 파일");
	}
	return Label;
}

#undef LOCTEXT_NAMESPACE

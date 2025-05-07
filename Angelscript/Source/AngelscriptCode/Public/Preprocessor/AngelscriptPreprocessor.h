#pragma once

#include "CoreMinimal.h"

#include "AngelscriptManager.h"
#include "AngelscriptSettings.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FOnAngelscriptPreprocessHook, struct FAngelscriptPreprocessor&);

struct ANGELSCRIPTCODE_API FAngelscriptPreprocessor
{
	FAngelscriptPreprocessor();

	/* Add a file to be preprocessed. */
	void AddFile(const FString& ScriptRelativePath, const FString& ScriptAbsoluteFilename, bool bLoadAsynchronous = false, bool bTreatAsDeleted = false);

	/* Perform preprocessing on all files. Returns true if successful. */
	bool Preprocess();

	/* Retrieve preprocessed modules to pass into compilation. */
	TArray<TSharedRef<FAngelscriptModuleDesc>> GetModulesToCompile();

	/* List of preprocessor flags that can be used in #if statements. */
	TMap<FString, bool> PreprocessorFlags;

	/* Convert a file path to an angelscript module name. */
	FString FilenameToModuleName(const FString& Filename);

	static FOnAngelscriptPreprocessHook OnProcessChunks;
	static FOnAngelscriptPreprocessHook OnPostProcessCode;

	enum class EMacroType : uint8
	{
		Property,
		Function,

		Class,
		Enum,
		EnumValue,
		EnumMeta
	};

	struct FMacro
	{
		EMacroType Type;
		FString Comment;
		FString Arguments;
		FString Name;
		FString SubjectType;
		int32 NameStartPos = -1;
		int32 NameEndPos = -1;
		int32 MacroStartPos = -1;
		int32 MacroEndPos = -1;
		int32 FileLineNumber = -1;
		int32 SubjectIndex = -1;
		bool bEditorOnly = false;
	};

	struct FStreamReplace
	{
		int32 StartPos;
		int32 EndPos;
		FString Replacement;

		bool operator<(const FStreamReplace& Other) const
		{
			return StartPos < Other.StartPos;
		}
	};

	struct FDefaultsCode
	{
		int32 StartPos;
		int32 NewStartPos;
	};

	enum class EChunkType : uint8
	{
		Global,
		Class,
		Struct,
		Enum,
	};

	struct FChunk
	{
		EChunkType Type;
		FString Content;
		FString Comment;
		TArray<FDefaultsCode> Defaults;
		TArray<FMacro> Macros;
		TArray<FStreamReplace> Replacements;
		TSharedPtr<FAngelscriptClassDesc> ClassDesc;
		TOptional<FString> Namespace;
		int32 FileLineNumber = -1;
		int32 ChunkStartPos = -1;
		int32 ChunkEndPos = -1;
	};

	struct FImport
	{
		FString ModuleName;
		int32 ChunkIndex;
		int32 StartPosInChunk;
		int32 EndPosInChunk;
		int32 FileLineNumber = -1;
	};

	struct FDelegateDesc
	{
		bool bIsMulticast;
		int32 ChunkIndex;
		int32 StartPosInChunk;
		int32 EndPosInChunk;
		int32 BracketPos;
		int32 FileLineNumber = -1;
	};

	struct FFile
	{
		// Final data structure after preprocessing is done
		TSharedPtr<FAngelscriptModuleDesc> Module;

		// Statics class generated for global functions
		TSharedPtr<FAngelscriptClassDesc> StaticsClass;

		// Filenames of the code read into this file
		FString AbsoluteFilename;
		FString RelativeFilename;

		// Raw code read directly from the module file
		FString RawCode;

		// Chunked representation of raw code
		TChunkedArray<FChunk> ChunkedCode;

		// Fully processed code to be compiled
		FString ProcessedCode;

		// Generated code to be added to the module
		TArray<FString> GeneratedCode;

		// Imported modulse
		TArray<FImport> Imports;

		// Delegate declarations
		TArray<FDelegateDesc> Delegates;

		// Whether imports have been resolved for this module
		bool bImportsResolved = false;

		// Whether we're currently in the process of resolving imports
		bool bIsResolvingImports = false;

		// Any asynchronous load we're doing on the file contents
		volatile bool bLoadAsynchronous = false;
		class IAsyncReadFileHandle* AsyncReadHandle = nullptr;
		class IAsyncReadRequest* AsyncSizeRequest = nullptr;
		class IAsyncReadRequest* AsyncReadRequest = nullptr;
	};

	struct FImportChain
	{
		FFile* File;
		FImportChain* Previous;
	};

	bool bIsPreprocessed = false;
	bool bHasError = false;
	bool bLoadingAnyFilesAsynchronous = false;

	void PerformAsynchronousLoads();

	bool bDefaultFunctionBlueprintCallable;
	EAngelscriptPropertyEditSpecifier DefaultPropertyEditSpecifier;
	EAngelscriptPropertyEditSpecifier DefaultPropertyEditSpecifierForStructs;
	EAngelscriptPropertyBlueprintSpecifier DefaultPropertyBlueprintSpecifier;

	TArray<FFile> Files;
	TMap<FString, TSharedPtr<FAngelscriptClassDesc>> PreprocessingClasses;

	bool IsPreprocessingModule(const FString& ModuleName);
	TSharedPtr<FAngelscriptClassDesc> GetClassDescFor(const FString& ClassName);
	TSharedPtr<FAngelscriptClassDesc> GetOrCreateStaticsClass(FFile& File);

	void ResolveSuperClass(TSharedPtr<FAngelscriptClassDesc> ClassDesc, bool bShowError = true);
	TSet<TSharedPtr<FAngelscriptClassDesc>> ResolvingClasses;

	void ParseIntoChunks(FFile& File);
	void ProcessReplacements(FFile& File, FChunk& Chunk);
	void CondenseFromChunks(FFile& File);

	void PostProcessRangeBasedFor(FFile& file);
	void PostProcessLiteralAssets(FFile& file);

	void DetectClasses(FFile& File);
	void DetectClasses(FFile& File, FChunk& Chunk);
	void DetectEnum(FFile& File, FChunk& Chunk);

	void ReplaceWithBlank(FChunk& Chunk, int32 StartPos, int32 EndPos);
	void ReplaceInChunk(FChunk& Chunk, int32 StartPos, int32 EndPos, const FString& Replacement);

	void AnalyzeClasses(FFile& File);
	void AnalyzeClasses(FFile& File, FChunk& Chunk);
	void AnalyzeStructs(FFile& File, FChunk& Chunk);

	int32 FindScopeCloseBracket(const FString& InString, int32 OpenBracketPos);
	int32 FindSemicolonDirectlyAfter(const FString& InString, int32 OpenBracketPos);

	void ProcessMacros(FFile& File);
	void ProcessFunctionMacro(FFile& File, FChunk& Chunk, FMacro& Macro);
	void ProcessPropertyMacro(FFile& File, FChunk& Chunk, FMacro& Macro);
	void ProcessClassMacro(FFile& File, FChunk& Chunk, FMacro& Macro);

	void ProcessDefaults(FFile& File, FChunk& Chunk);
	void ProcessImports(FFile& File, TArray<FFile>& OutSortedFiles, FImportChain* Chain);
	void ProcessDelegates(FFile& File);

	FString GenerateStaticName(FFile& File, const FString& Name);
	FString GenerateFormatString(FFile& File, const FString& FormatStr);
	FString ParseFormatExpression(FFile& File, const FString& FormatExpr);
	FString MakeIdentifier(const FString& Str);

	FORCEINLINE bool IsWhitespace(TCHAR Char)
	{
		return Char == '\n' || Char == '\t' || Char == ' ' || Char == '\r';
	}

	FString ExtractArgumentList(FChunk& Chunk, FMacro& Macro, TArray<FString>& OutArgNames, TArray<FString>& OutArgTypes, bool& OutConst, bool &OutProperty);
	FString ExtractReturnType(FChunk& Chunk, FMacro& Macro, bool& OutIsConst, FString& OutAccessSpecifier);
	void GenerateBlueprintEventWrapper(FFile& File, FChunk& Chunk, FMacro& Macro, TSharedPtr<FAngelscriptFunctionDesc> FunctionDesc);

	void FileWideError(FFile& File, const FString& Message);
	void FileWideWarning(FFile& File, const FString& Message);
	void LineError(FFile& File, int32 Line, const FString& Message);
	void LineWarning(FFile& File, int32 Line, const FString& Message);
	void ChunkError(FFile& File, FChunk& Chunk, const FString& Message);
	void MacroError(FFile& File, FMacro& Macro, const FString& Message);

	FChunk& ResolveFilePos(FFile& File, int32 FilePos, int32& OutChunkPos);
	void ReplaceWithBlankFilePos(FFile& File, int32 StartPos, int32 EndPos);

	FFile* GetFileForClass(TSharedPtr<FAngelscriptClassDesc> ClassDesc);

	FString ReadIdentifier(FFile& File, int32 Pos);
	FString ReadUntilWhitespace(FFile& File, int32 Pos);
	bool ParsePreProc(FFile& File, int32 LineNumber, const FString& PreProc);
	void KillRawLine(FFile& File, int32 FromPos);

	void StripCommentsFromLine(FString& Line);

	FString GetPushArgumentSuffix(const FString& Type);

	UAngelscriptSettings* ConfigSettings;
};

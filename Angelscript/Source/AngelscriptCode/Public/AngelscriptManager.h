#pragma once

#include "CoreMinimal.h"
#include "Tickable.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/CoreNetTypes.h"
#include "ClassGenerator/AngelscriptAdditionalCompileChecks.h"

#include "AngelscriptType.h"

#define AS_CAN_HOTRELOAD (PLATFORM_DESKTOP)
#define AS_MAX_POOLED_CONTEXTS 10
#define AS_PRINT_STATS (!UE_BUILD_SHIPPING)
#define AS_PRECOMPILED_STATS 1
#define WITH_AS_DEBUGSERVER (!UE_BUILD_TEST && !UE_BUILD_SHIPPING)
#define AS_USE_BIND_DB (!WITH_EDITOR)
#define AS_ITERATOR_DEBUGGING (WITH_EDITOR)
#define AS_REFERENCE_DEBUGGING (WITH_EDITOR)
#define WITH_AS_COVERAGE WITH_AS_DEBUGSERVER

#ifndef AS_PROPERTY_ACCESSOR_MODE
#define AS_PROPERTY_ACCESSOR_MODE 3
#endif
#ifndef AS_ENFORCE_SERVER_RPC_VALIDATION
#define AS_ENFORCE_SERVER_RPC_VALIDATION 0
#endif

#ifndef WITH_ANGELSCRIPT_HAZE
#define WITH_ANGELSCRIPT_HAZE 0
#endif

#define AS_LLM_SCOPE LLM_SCOPE(ELLMTag::CsvProfiler)

ANGELSCRIPTCODE_API DECLARE_LOG_CATEGORY_EXTERN(Angelscript, Log, All);

class asIScriptEngine;
class asIScriptContext;
class asIScriptModule;
class asIScriptFunction;
class asIScriptObject;
class asITypeInfo;
class asCContext;

class FHotReloadTestRunner;

struct FAngelscriptCodeCoverage;

enum class ECompileType : uint8
{
	Initial,
	SoftReloadOnly,
	FullReload,
};

enum class ECompileResult : uint8
{
	Error,
	ErrorNeedFullReload,
	PartiallyHandled,
	FullyHandled,
};

struct ANGELSCRIPTCODE_API FAngelscriptManager : FTickableGameObject
{
	static FAngelscriptManager& Get();
	static bool IsInitialized();
	static FString GetScriptRootDirectory();
	static UPackage* GetPackage();
	static UObject* CurrentWorldContext;
	static class asCThreadLocalData* GameThreadTLD;
	static bool bSimulateCooked;
	static bool bUseEditorScripts;
	static bool bTestErrors;
	static bool bIsHotReloading;
	static bool bForcePreprocessEditorCode;
	static bool bGeneratePrecompiledData;
	static bool bStaticJITTranspiledCodeLoaded;

	static bool bUseScriptNameForBlueprintLibraryNamespaces;
	static TArray<FString> BlueprintLibraryNamespacePrefixesToStrip;
	static TArray<FString> BlueprintLibraryNamespaceSuffixesToStrip;

	void Initialize();

	/* Initially bind all engine types to angelscript. */
	void BindScriptTypes();

	/* Initially compile all script files in global folders. */
	void InitialCompile();

	/* Add the listed set of modules into the angelscript engine. 
	 * Modules should already be pre-processed.
	 * Modules array should already be sorted in dependency order. */
	ECompileResult CompileModules(ECompileType CompileType, const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& Modules, TArray<TSharedRef<struct FAngelscriptModuleDesc>>& OutCompiledModules);
	void CompileModule_Types_Stage1(ECompileType CompileType, TSharedRef<struct FAngelscriptModuleDesc> Module, const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& ImportedModules);
	void CompileModule_Functions_Stage2(ECompileType CompileType, TSharedRef<struct FAngelscriptModuleDesc> Module);
	void CompileModule_Code_Stage3(ECompileType CompileType, TSharedRef<struct FAngelscriptModuleDesc> Module);
	void CompileModule_Globals_Stage4(ECompileType CompileType, TSharedRef<struct FAngelscriptModuleDesc> Module);

	/* Perform a hot reload of the specified type if necessary. */
	void CheckForHotReload(ECompileType CompileType);

	/* Verify Unreal Property specifiers in a module. */
	bool VerifyPropertySpecifiers(const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& Modules);

	bool VerifyRepFunc(FString* FuncDesc, const TSharedRef<struct FAngelscriptPropertyDesc>& Property,
		const TSharedRef<struct FAngelscriptClassDesc>& Class,
		const TSharedRef<struct FAngelscriptModuleDesc>& Module);

	bool VerifyBlueprintSetFunc(FString* FuncDesc, const TSharedRef<struct FAngelscriptPropertyDesc>& Property,
		const TSharedRef<struct FAngelscriptClassDesc>& Class, const TSharedRef<struct FAngelscriptModuleDesc>& Module);

	bool VerifyBlueprintGetFunc(FString* FuncDesc, const TSharedRef<struct FAngelscriptPropertyDesc>& Property,
		const TSharedRef<struct FAngelscriptClassDesc>& Class, const TSharedRef<struct FAngelscriptModuleDesc>& Module);


	/* Manager ticks to detect hot reloads*/
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }
	virtual bool IsTickableWhenPaused() const override { return true; }

	/* Functions can have user data specified at bind-time that can be looked up here. */
	template<typename T>
	static T* GetCurrentFunctionUserData()
	{
		return (T*)GetCurrentFunctionUserDataPtr();
	}

	static void* GetCurrentFunctionUserDataPtr();
	static asITypeInfo* GetCurrentFunctionObjectType();
	static class asCContext* GetCurrentScriptContext();
	static class asCContext* GetPreviousScriptContext();

	/* Outdated flag marking for modules and functions. */
	bool IsOutdated(asIScriptFunction* Function);

	/* Converting from angelscript-visible pointers to uobject pointers. */
	static FORCEINLINE UObject* AngelscriptToUObject(asIScriptObject* Object)
	{
		return (UObject*)Object;
	}

	static FORCEINLINE asIScriptObject* UObjectToAngelscript(UObject* Object)
	{
		return (asIScriptObject*)Object;
	}

	static FORCEINLINE bool CanUseGameThreadData()
	{
		return IsInGameThread() || !bIsInitialCompileFinished;
	}

	/* Throw an exception to the angelscript VM. */
	static void Throw(const ANSICHAR* Exception);

	/* Show an error with a script stack trace, but continue the script flow without throwing an exception. */
	static void TraceError(const ANSICHAR* Error);

	/* Get string representations of all levels of the current angelscript callstack. */
	static TArray<FString> GetAngelscriptCallstack();

	/* Get a string representation of the current angelscript callstack. */
	static FString FormatAngelscriptCallstack();

	/* Get a string representation of the current location of angelscript execution (ie the top most stack frame's position). */
	static FString GetAngelscriptExecutionPosition();

	/* Get the file and line number of the current location of angelscript execution (ie the top most stack frame's position). */
	static void GetAngelscriptExecutionFileAndLine(FString& OutFilename, int& OutLineNumber);

	/* Get the UObject that the current angelscript callstack is operating on. */
	static UObject* GetAngelscriptExecutionThisObject(int32 StackFrame = 0);

	/* If the angelscript debugger is attached, do an angelscript breakpoint. Returns whether we broke in AS debugging. */
	static bool TryBreakpointAngelscriptDebugging(const TCHAR* Message = nullptr);

	/* Checks if the character is a valid alphanumeric character or an underscore. */
	FORCEINLINE static bool IsValidIdentifierCharacter(TCHAR Character)
	{
		return (Character >= 'A' && Character <= 'Z')
				|| (Character >= 'a' && Character <= 'z')
				|| (Character >= '0' && Character <= '9')
				|| Character == '_';
	}

	/* The root angelscript UPackage everything should belong to. */
	UPackage* AngelscriptPackage = nullptr;
	/* The package that all literal assets are put into. */
	UPackage* AssetsPackage = nullptr;

	/* Root paths where all scripts are loaded from. */
	TArray<FString> AllRootPaths;

	/* Internal script data. */
	class asCScriptEngine* Engine = nullptr;

	asIScriptEngine* GetScriptEngine() const
	{
		return (asIScriptEngine*)Engine;
	}

	TSharedPtr<struct FAngelscriptModuleDesc> GetModule(const FString& ModuleName)
	{
		auto* ModRef = ActiveModules.Find(ModuleName);
		if (ModRef == nullptr)
			return nullptr;
		else
			return *ModRef;
	}

	TArray<TSharedRef<struct FAngelscriptModuleDesc>> GetActiveModules() const
	{
		TArray<TSharedRef<struct FAngelscriptModuleDesc>> Result;
		for (auto It : ActiveModules)
		{
			Result.Add(It.Value);
		}
		return Result;
	}

	TSharedPtr<struct FAngelscriptModuleDesc> GetModule(asIScriptModule* Module);
	TSharedPtr<struct FAngelscriptModuleDesc> GetModuleByFilename(const FString& Filename);

	TSharedPtr<struct FAngelscriptClassDesc> GetClass(const FString& ClassName, TSharedPtr<struct FAngelscriptModuleDesc>* FoundInModule = nullptr);
	TSharedPtr<struct FAngelscriptEnumDesc> GetEnum(const FString& EnumName, TSharedPtr<struct FAngelscriptModuleDesc>* FoundInModule = nullptr);
	TSharedPtr<struct FAngelscriptDelegateDesc> GetDelegate(const FString& DelegateName, TSharedPtr<struct FAngelscriptModuleDesc>* FoundInModule = nullptr);

	TSharedPtr<struct FAngelscriptModuleDesc> GetModuleByModuleName(const FString& ModuleName);

	TSharedPtr<struct FAngelscriptModuleDesc> GetModuleByFilenameOrModuleName(const FString& Filename, const FString& ModuleName);

	// Captured diagnostic messages during compilation
	struct FDiagnostic
	{
		FString Message;
		int32 Row;
		int32 Column;
		bool bIsError;
		bool bIsInfo;
	};

	void ScriptCompileError(const FString& AbsoluteFilename, const FDiagnostic& Diagnostic);
	void ScriptCompileError(TSharedPtr<FAngelscriptModuleDesc> Module, int32 LineNumber, const FString& Message, bool bIsError = true);
	void ScriptCompileError(UClass* InsideClass, const FString& FunctionName, const FString& Message, bool bIsError = true);

	UStruct* GetUnrealStructFromAngelscriptTypeId(int TypeId);

	// Can be filled by the game module to provide additional compile checks in editor
	// depending on what code class is being compiled.
	TMap<UClass*, TSharedPtr<FAngelscriptAdditionalCompileChecks>> AdditionalCompileChecks;

	struct FFilenamePair
	{
		FString AbsolutePath;
		FString RelativePath;
	};

private:
	bool ShouldInitializeThreaded();
	void PreInitialize_GameThread();
	void Initialize_AnyThread();
	void PostInitialize_GameThread();

	void SetOutdated(asIScriptModule* OldModule);

	/* Internal state of active modules. */
	TMap<FString, TSharedRef<struct FAngelscriptModuleDesc>> ActiveModules;
	TMap<asIScriptModule*, TSharedPtr<struct FAngelscriptModuleDesc>> ModulesByScriptModule;

#if AS_CAN_HOTRELOAD
	TMap<FStringView, TPair<TSharedPtr<struct FAngelscriptModuleDesc>, TSharedPtr<struct FAngelscriptClassDesc>>> ActiveClassesByName;
	TMap<FStringView, TPair<TSharedPtr<struct FAngelscriptModuleDesc>, TSharedPtr<struct FAngelscriptEnumDesc>>> ActiveEnumsByName;
	TMap<FStringView, TPair<TSharedPtr<struct FAngelscriptModuleDesc>, TSharedPtr<struct FAngelscriptDelegateDesc>>> ActiveDelegatesByName;
#endif

	/* Global context pool of contexts that don't belong to a thread right now. */
	friend struct FAngelscriptPooledContextBase;
	friend struct FAngelscriptContextPool;
	TArray<asCContext*> GlobalContextPool;
	FCriticalSection GlobalContextPoolLock;

	/* Hot reload watch state, maps script files to required data for detecting reloads. */
	struct FHotReloadState
	{
		FDateTime LastChange;
	};

	bool bUseHotReloadCheckerThread = false;

	TMap<FString, FHotReloadState> FileHotReloadState;

	volatile bool bWaitingForHotReloadResults = false;

	/* Prepares and holds the execution status of unit tests on hot reload. */
	FHotReloadTestRunner* HotReloadTestRunner = nullptr;

	/* Files that we tried to reload before, but failed to compile, that we should retry later. */
	TSet<FFilenamePair> PreviouslyFailedReloadFiles;

	/* Files that we soft reloaded but that we need to do a full reload on once we are able. */
	TSet<FFilenamePair> QueuedFullReloadFiles;

	double NextHotReloadCheck = -1.0;

	void DiscoverTests();
	bool PerformHotReload(ECompileType CompileType, const TArray<FFilenamePair>& FileList);
	void CheckForFileChanges();

	void ImportIntoModule(class asIScriptModule* IntoModule, class asIScriptModule* FromModule);

	bool CheckFunctionImportsForNewModules(const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& Modules);
	void UpdateScriptReferencesInUnrealData(struct asModuleReferenceUpdateMap& UpdateMap, TSharedRef<FAngelscriptModuleDesc> Module);

	void ResolveAllDeclaredImports();
	void ResolveDeclaredImports(class asIScriptModule* Module);

#if WITH_EDITOR
	void CheckUsageRestrictions(const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& Modules);
#endif

	void SwapInModules(const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& Modules, TArray<TSharedRef<struct FAngelscriptModuleDesc>>& DiscardedModules);

#if !UE_BUILD_SHIPPING
	void GetOnScreenMessages(TMultiMap<FCoreDelegates::EOnScreenMessageSeverity, FText>& OutMessages);
#endif

	// Counter for temporary generated module names
	int32 TempNameIndex = 0;

	friend class UAngelscriptTestCommandlet;

	static FAngelscriptManager& GetOrCreate();
	friend class FAngelscriptCodeModule;

public:
	TArray<FFilenamePair> FileChangesDetectedForReload;
	TArray<FFilenamePair> FileDeletionsDetectedForReload;
	double LastFileChangeDetectedTime = -1.0;

	bool bDidInitialCompileSucceed = true;
	static bool bIsInitialCompileFinished;
	
	bool bCompletedAssetScan = false;

	struct FAngelscriptDebugFrame
	{
		const char* File = nullptr;
		const char* Function = nullptr;
		const char* Class = nullptr;
		int32 LineNumber = -1;
		UObject* This = nullptr;
		struct FDebugValues* Variables = nullptr;
		struct FDebugValuePrototype* Prototype = nullptr;
		asIScriptFunction* ScriptFunction = nullptr;

		~FAngelscriptDebugFrame();
	};

	struct FAngelscriptDebugStack
	{
		TArray<FAngelscriptDebugFrame> Frames;
	};

	struct FDiagnostics
	{
		FString Filename;
		TArray<FDiagnostic> Diagnostics;
		bool bHasEmittedAny = false;
		bool bIsCompiling = false;
	};

#if WITH_AS_DEBUGSERVER
	class FAngelscriptDebugServer* DebugServer = nullptr;
	bool IsEvaluatingDebuggerWatch();
#endif

	FCriticalSection CompilationLock;

	TMap<FString, FDiagnostics> Diagnostics;
	TMap<FString, FDiagnostics> LastEmittedDiagnostics;

	bool bDiagnosticsDirty = false;
	bool bIgnoreCompileErrorDiagnostics = false;

	FString FormatDiagnostics();
	void ResetDiagnostics();
	void EmitDiagnostics(class FSocket* Client = nullptr);
	void EmitDiagnostics(FDiagnostics& Diag, class FSocket* Client = nullptr);

	void FindAllScriptFilenames(TArray<FFilenamePair>& OutFilenames);

	bool HasAnyDebugServerClients();
	void ReplaceScriptAssetContent(FString AssetName, TArray<FString> AssetContent);

	friend struct FAngelscriptPrecompiledData;
	struct FAngelscriptPrecompiledData* PrecompiledData = nullptr;
	struct FAngelscriptStaticJIT* StaticJIT = nullptr;
	bool bUsePrecompiledData = false;
	bool bUsedPrecompiledDataForPreprocessor = false;
	static bool bScriptDevelopmentMode;
	static bool bUseAutomaticImportMethod;

	static bool IsGeneratingPrecompiledData();

	// Argument type specializations that were bound by Bind_BlueprintEvent.cpp,
	// the preprocessor uses this list to look up what push argument function to call
	TSet<FString> BoundBlueprintEventArgumentSpecializations;

	void StartHotReloadThread();
	bool bHotReloadThreadStarted = false;

	class UAngelscriptSettings* ConfigSettings = nullptr;

	static void HandleExceptionFromJIT(const ANSICHAR* ExceptionString);
	asCContext* CreateContext();
	void UpdateLineCallbackState();

	static TArray<FName> StaticNames;
	static TMap<FName, int32> StaticNamesByIndex;

	FORCEINLINE static const FName& GetStaticName(int32 Index)
	{
		return StaticNames[Index];
	}

	FORCEINLINE static void AssignWorldContext(UObject* NewWorldContext)
	{
		*(UObject* volatile*)&CurrentWorldContext = NewWorldContext;
		check(FAngelscriptManager::CanUseGameThreadData());

#if WITH_EDITOR
		extern ANGELSCRIPTCODE_API void SetAngelscriptWorldContextAvailable(bool bAvailable);
		SetAngelscriptWorldContextAvailable(
			(NewWorldContext != nullptr)
			&& !NewWorldContext->HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject)
			&& (NewWorldContext->GetWorld() != nullptr));
#endif
	}

	static void FindScriptFiles(
		IFileManager& FileManager,
		const FString& RelativeRoot,
		const FString& SearchDirectory,
		const TCHAR* Pattern,
		TArray<FFilenamePair>& OutFilenames,
		bool bSkipDevelopmentScripts,
		bool bSkipEditorScripts);

	static TArray<FString> MakeAllScriptRoots(bool bOnlyProjectRoot = false);

#if WITH_AS_COVERAGE
	FAngelscriptCodeCoverage* CodeCoverage = nullptr;
#endif
};

struct FAngelscriptContextPool
{
	TArray<asCContext*> FreeContexts;

	~FAngelscriptContextPool();
};

extern thread_local FAngelscriptContextPool GAngelscriptContextPool;
extern FAngelscriptManager::FAngelscriptDebugStack* GAngelscriptStack;

/* Automatically retrieves and manages an angelscript context for the scope of this struct. */
struct ANGELSCRIPTCODE_API FAngelscriptPooledContextBase
{
	FAngelscriptPooledContextBase();

	FORCEINLINE FAngelscriptPooledContextBase(class asCThreadLocalData* tld)
	{
		Init(tld);
	}

	~FAngelscriptPooledContextBase();

	void Init(class asCThreadLocalData* tld);

	FAngelscriptPooledContextBase(FAngelscriptPooledContextBase&& Other);

	FAngelscriptPooledContextBase(FAngelscriptPooledContextBase& Other) = delete;
	void operator=(FAngelscriptPooledContextBase& Other) = delete;

	bool operator==(asCContext* Ptr) const { return Context == Ptr; }
	bool operator!=(asCContext* Ptr) const { return Context != Ptr; }

	void PrepareExternal(class asIScriptFunction* Function);
	void ExecuteExternal();

	FORCEINLINE asCContext* operator->() const
	{
		return Context;
	}

	operator asCContext*() const
	{
		return Context;
	}

private:
	asCContext* Context;
	bool bWasNested;
};

struct FAngelscriptGameThreadScopeWorldContext
{
	FAngelscriptGameThreadScopeWorldContext(UObject* WorldContext)
	{
		PreviousWorldContext = FAngelscriptManager::CurrentWorldContext;
		FAngelscriptManager::AssignWorldContext(WorldContext);
	}

	~FAngelscriptGameThreadScopeWorldContext()
	{
		FAngelscriptManager::AssignWorldContext(PreviousWorldContext);
	}
private:
	UObject* PreviousWorldContext;
};

struct FAngelscriptContext : public FAngelscriptPooledContextBase
{
	FAngelscriptContext()
	{
		bChangedWorldContext = false;
	}

	FAngelscriptContext(UObject* WorldContext)
	{
		if (FAngelscriptManager::CanUseGameThreadData())
		{
			PreviousWorldContext = FAngelscriptManager::CurrentWorldContext;
			FAngelscriptManager::AssignWorldContext(WorldContext);
			bChangedWorldContext = true;
		}
		else
		{
			bChangedWorldContext = false;
		}
	}

	~FAngelscriptContext()
	{
		if (bChangedWorldContext)
		{
			FAngelscriptManager::AssignWorldContext(PreviousWorldContext);
		}
	}
private:
	UObject* PreviousWorldContext;
	bool bChangedWorldContext;
};

struct FAngelscriptGameThreadContext : public FAngelscriptPooledContextBase
{
	FAngelscriptGameThreadContext(UObject* WorldContext)
		: FAngelscriptPooledContextBase(FAngelscriptManager::GameThreadTLD)
	{
		PreviousWorldContext = FAngelscriptManager::CurrentWorldContext;
		FAngelscriptManager::AssignWorldContext(WorldContext);
	}

	~FAngelscriptGameThreadContext()
	{
		FAngelscriptManager::AssignWorldContext(PreviousWorldContext);
	}
private:
	UObject* PreviousWorldContext;
};

/**
 * Anything running within this scope is excluded from the angelscript loop detection timeout in editor.
 */
struct ANGELSCRIPTCODE_API FAngelscriptExcludeScopeFromLoopTimeout
{
#if WITH_EDITOR
	class asCContext* Context;
	double StartTime;

	FAngelscriptExcludeScopeFromLoopTimeout();
	~FAngelscriptExcludeScopeFromLoopTimeout();
#else
	FORCEINLINE FAngelscriptExcludeScopeFromLoopTimeout() {}
	FORCEINLINE ~FAngelscriptExcludeScopeFromLoopTimeout() {}
#endif
};

/**
 * Description of a script property that has been added as an unreal property.
 */
struct FAngelscriptPropertyDesc
{
	/* Name of the property. */
	FString PropertyName;

	/* Literal type in script of the property. */
	FString LiteralType;

	/* Resolved type of the property. */
	FAngelscriptTypeUsage PropertyType;

	/* Metadata for the property. */
	TMap<FName, FString> Meta;

	/* Whether the property can be read in blueprint. */
	bool bBlueprintReadable = false;

	/* Whether the property can be written in blueprint. */
	bool bBlueprintWritable = false;

	/* Whether the property can be edited on defaults. */
	bool bEditableOnDefaults = false;

	/* Whether the property can be edited on instances. */
	bool bEditableOnInstance = false;

	/* Whether the property is shown in details views but cannot be changed. */
	bool bEditConst = false;

	/* Whether the property is considered a component reference. */
	bool bInstancedReference = false;

	/* Whether the property is considered a persistent reference, an object referenced by the property is duplicated like a component. */
	bool bPersistentInstance = false;

	/* Whether the property should be marked as advanced display. */
	bool bAdvancedDisplay = false;

	/* Whether the property is transient. */
	bool bTransient = false;

	/* Whether an FProperty exists in the class for this property. */
	bool bHasUnrealProperty = false;

	/* Whether the property should be replicated. */
	bool bReplicated = false;

	/* Whether the property should be skipped when replicating. */
	bool bSkipReplication = false;

	/* Whether to skip during serialization. */
	bool bSkipSerialization = false;

	/* Whether property should be serialized for save games. */
	bool bSaveGame = false;

	/* Specified replication condition. */
	TEnumAsByte<ELifetimeCondition> ReplicationCondition = COND_None;

	/* Whether we should call a function when this is replicated. */
	bool bRepNotify = false;

	/* Whether this is a config property read from ini files. */
	bool bConfig = false;

	/* Whether the property is exposed for Matinee or Sequencer to modify. */
	bool bInterp = false;

	/* Whether the property should be searchable in the Asset Registry. */
	bool bAssetRegistrySearchable = false;

	/* Whether the property should not be clearable (disallow being set to None). */
	bool bNoClear = false;

	/* Whether the property is private in angelscript. */
	bool bIsPrivate = false;

	/* Whether the property is protected in angelscript. */
	bool bIsProtected = false;

	/* Angelscript internal data for the property. */
	int32 ScriptPropertyIndex = -1;
	SIZE_T ScriptPropertyOffset = 0;

	/* Line number in the file of the property. */
	int32 LineNumber = 1;

	bool IsDefinitionEquivalent(const FAngelscriptPropertyDesc& Other) const
	{
		return Other.bBlueprintReadable == bBlueprintReadable
			&& Other.bBlueprintWritable == bBlueprintWritable
			&& Other.bEditableOnDefaults == bEditableOnDefaults
			&& Other.bEditableOnInstance == bEditableOnInstance
			&& Other.bAdvancedDisplay == bAdvancedDisplay
			&& Other.bEditConst == bEditConst
			&& Other.bInstancedReference == bInstancedReference
			&& Other.bPersistentInstance == bPersistentInstance
			&& Other.bTransient == bTransient
			&& Other.bConfig == bConfig
			&& Other.bInterp == bInterp
			&& Other.bAssetRegistrySearchable == bAssetRegistrySearchable
			&& Other.bNoClear == bNoClear
			&& Other.bReplicated == bReplicated
			&& Other.ReplicationCondition == ReplicationCondition
			&& Other.bSkipReplication == bSkipReplication
			&& Other.bSkipSerialization == bSkipSerialization
			&& Other.bSaveGame == bSaveGame
			&& Other.bRepNotify == bRepNotify
			&& Other.bIsPrivate == bIsPrivate
			&& Other.bIsProtected == bIsProtected
			;
	}
};

/**
 * Description of an argument to an angelscript function.
 */
struct FAngelscriptArgumentDesc
{
	/* Name of the argument. */
	FString ArgumentName;

	/* Stringified default value of the argument. */
	FString DefaultValue;

	/* Angelscript type of the argument. */
	FAngelscriptTypeUsage Type;

	/* What kind of blueprint parameter to generate for this argument. */
	bool bBlueprintByValue = false;
	bool bBlueprintOutRef = false;
	bool bBlueprintInRef = false;

	/* If set, even an InRef parameter will have its value copied back. */
	bool bInRefForceCopyOut = false;

	bool IsDefinitionEquivalent(const FAngelscriptArgumentDesc& Other) const
	{
		return Other.bBlueprintByValue == bBlueprintByValue
			&& Other.bBlueprintOutRef == bBlueprintOutRef
			&& Other.bBlueprintInRef == bBlueprintInRef
			&& Other.bInRefForceCopyOut == bInRefForceCopyOut
			&& Other.Type == Type
			;
	}
};

/**
 * Description of a script function that should be bound as an unreal function.
 */
struct FAngelscriptFunctionDesc
{
	/* Name of the function in unreal. */
	FString FunctionName;

	/* Original name of the function as it was declared if FunctionName has been changed, empty otherwise. */
	FString OriginalFunctionName;

	/* Name of the angelscript function to bind. */
	FString ScriptFunctionName;

	/* Metadata for the function. */
	TMap<FName, FString> Meta;

	/* Return type of the function. */
	FAngelscriptTypeUsage ReturnType;

	/* Types of the arguments to the function. */
	TArray<FAngelscriptArgumentDesc> Arguments;

	/* Whether the function should be marked as blueprint callable. */
	bool bBlueprintCallable = false;

	/* Whether this function is an override for a Blueprint{Implementable,Native}Event. */
	bool bBlueprintOverride = false;

	/* Whether this function can be overridden by blueprint as an event. */
	bool bBlueprintEvent = false;

	/* Whether this function should be marked as pure in blueprint. */
	bool bBlueprintPure = false;

	/* Whether this function is a NetFunction. */
	bool bNetFunction = false;

	/* Whether this function is a NetMulticast. */
	bool bNetMulticast = false;

	/* Whether this function is a Client net function. */
	bool bNetClient = false;

	/* Whether this function is a Server net function. */
	bool bNetServer = false;

	/* Whether this function should have a _Validate function. */
	bool bNetValidate = false;

	/* Whether to send as unreliable if a netfunction. */
	bool bUnreliable = false;

	/* Whether to tag the function as blueprint authority only. */
	bool bBlueprintAuthorityOnly = false;

	/* Whether the function is marked as Exec in angelscript. */
	bool bExec = false;

	/* Whether the blueprint event can be overridden or not. */
	bool bCanOverrideEvent = true;

	/* Whether this function is marked as a Development Function (shows up in dev-menu) */
	bool bDevFunction = false;

	/* Whether this function is a static global function. */
	bool bIsStatic = false;

	/* Whether this is a const method in angelscript. */
	bool bIsConstMethod = false;

	/* Internal angelscript function this is referencing. */
	class asIScriptFunction* ScriptFunction = nullptr;

	/* Whether this function can be called from other threads in unreal. */
	bool bThreadSafe = false;

	/* Generated UFunction for this script function. */
	UFunction* Function = nullptr;

	/* Whether this function has a completely empty body. */
	bool bIsNoOp = false;

	/* Whether the function is private in angelscript. */
	bool bIsPrivate = false;

	/* Whether the function is protected in angelscript. */
	bool bIsProtected = false;

	/* Line number in the file of the function. */
	int32 LineNumber = 1;

	bool SignatureMatches(TSharedPtr<FAngelscriptFunctionDesc> OtherFunction, bool bCheckNames = false) const;
	bool ParametersMatches(TSharedPtr<FAngelscriptFunctionDesc> OtherFunction, bool bCheckNames = false) const;

	bool IsDefinitionEquivalent(const FAngelscriptFunctionDesc& Other) const
	{
		return Other.bBlueprintCallable == bBlueprintCallable
			&& Other.bBlueprintOverride == bBlueprintOverride
			&& Other.bBlueprintEvent == bBlueprintEvent
			&& Other.bBlueprintPure == bBlueprintPure
			&& Other.bNetFunction == bNetFunction
			&& Other.bUnreliable == bUnreliable
			&& Other.bDevFunction == bDevFunction
			&& Other.bNetMulticast == bNetMulticast
			&& Other.bNetClient == bNetClient
			&& Other.bNetServer == bNetServer
			&& Other.bBlueprintAuthorityOnly == bBlueprintAuthorityOnly
			&& Other.bExec == bExec
			&& Other.bCanOverrideEvent == bCanOverrideEvent
			&& Other.bIsStatic == bIsStatic
			&& Other.bDevFunction == bDevFunction
			&& Other.bIsConstMethod == bIsConstMethod
			&& Other.bThreadSafe == bThreadSafe
			&& Other.bIsPrivate == bIsPrivate
			&& Other.bIsProtected == bIsProtected
			;
	}
};

/**
 * Description of a script class during preprocessing.
 */
struct FAngelscriptClassDesc
{
	/* Name of the class that was compiled. */
	FString ClassName;

	/* Angelscript name of the class that should be the super for this angelscript class. */
	FString SuperClass;

	/* Actual UClass of the native class that backs this script type. */
	UClass* CodeSuperClass = nullptr;

	/* Whether the direct superclass of this is a code class. If false, it is an angelscript class. */
	bool bSuperIsCodeClass = false;

	/* Whether this is a generated statics class that does not actually exist in script. */
	bool bIsStaticsClass = false;

	/* Whether this class is abstract. */
	bool bAbstract = false;

	/* Whether all instances of this class should be transient. */
	bool bTransient = false;

	/* Whether this class is hidden from property combo boxes */
	bool bHideDropdown = false;

	/* Indicates that references to this class default to instanced. Used to be subclasses of UComponent, but now can be any UObject */
	bool bDefaultToInstanced = false;

	/* Class can be constructed from EditInlineNew New button. */
	bool bEditInlineNew = false;

	/* Whether this class is deprecated. */
	bool bIsDeprecatedClass = false;

	/* Whether the class can be placed in levels. */
	bool bPlaceable = true;

	/* Whether this class represents a struct or not. */
	bool bIsStruct = false;

	/* Name of the config file to use. */
	FString ConfigName;

	/* Internal angelscript class this is referencing. */
	asITypeInfo* ScriptType = nullptr;

	/* Generated UClass that this class should be instanced as. */
	UClass* Class = nullptr;

	/* Generated UStruct that this class should be instanced as. */
	UStruct* Struct = nullptr;

	/* Properties we're adding into unreal for this class. */
	TArray<TSharedRef<FAngelscriptPropertyDesc>> Properties;

	/* Functions we're adding into unreal for this class. */
	TArray<TSharedRef<FAngelscriptFunctionDesc>> Methods;

	/* The name of the global variable that should be set in script to this UClass. */
	FString StaticClassGlobalVariableName;

	/* The code used to set default properties for this class. */
	FString DefaultsCode;

	/* Line number in the file of the class. */
	int32 LineNumber = 1;

	/* Metadata for the class. */
	TMap<FName, FString> Meta;

	/* Composable class */
	FString ComposeOntoClass;

	/* This will be set when the class resides in a namespace that is NOT the modules default namespace. */
	TOptional<FString> Namespace;

	// Find the property descriptor by name
	TSharedPtr<FAngelscriptPropertyDesc> GetProperty(const FString& PropName)
	{
		for (auto PropDesc : Properties)
		{
			if (PropName.Equals(PropDesc->PropertyName))
			{
				return PropDesc;
			}
		}
		return nullptr;
	}

	TSharedPtr<FAngelscriptPropertyDesc> GetProperty(class asCString& PropName);

	// Find the function descriptor by name
	TSharedPtr<FAngelscriptFunctionDesc> GetMethod(const FString& FuncName)
	{
		for (auto FuncDesc : Methods)
		{
			if (FuncName.Equals(FuncDesc->FunctionName))
			{
				return FuncDesc;
			}
		}
		return nullptr;
	}

	TSharedPtr<FAngelscriptFunctionDesc> GetMethodByScriptName(const FString& FuncName)
	{
		for (auto FuncDesc : Methods)
		{
			if (FuncName.Equals(FuncDesc->ScriptFunctionName))
			{
				return FuncDesc;
			}
		}
		return nullptr;
	}

	bool AreFlagsEqual(const FAngelscriptClassDesc& Other) const
	{
		return bAbstract == Other.bAbstract
			&& bTransient == Other.bTransient
			&& bHideDropdown == Other.bHideDropdown
			&& bDefaultToInstanced == Other.bDefaultToInstanced
			&& bEditInlineNew == Other.bEditInlineNew
			&& bIsDeprecatedClass == Other.bIsDeprecatedClass
			&& bPlaceable == Other.bPlaceable
		;
	}
};

/**
 * Description of a script enum during preprocessing.
 */
struct FAngelscriptEnumDesc
{
	/* Name of the enum that was compiled. */
	FString EnumName;
	FString Documentation;

	/* Enum values to add to the enum. */
	TArray<FName> ValueNames;
	TArray<int32> EnumValues;

	/* Actual generated UEnum for this. */
	UEnum* Enum = nullptr;

	/* Internal angelscript type this is referencing. */
	asITypeInfo* ScriptType = nullptr;

	/* Line number in the file of the enum. */
	int32 LineNumber = 1;

	/* Metadata for the enum. */
	TMap<TPair<FName, int32>, FString> Meta;
};

/**
 * Description of a delegate signature declared in angelscript.
 */
struct FAngelscriptDelegateDesc
{
	/* Name of the delegate that was compiled. */
	FString DelegateName;

	/* Whether this is a multicast delegate. */
	bool bIsMulticast = false;

	/* Description of the signature function in angelscript. */
	TSharedPtr<FAngelscriptFunctionDesc> Signature;

	/* Actual generated signature UFunction for this. */
	UDelegateFunction* Function = nullptr;

	/* Internal angelscript type this is referencing. */
	asITypeInfo* ScriptType = nullptr;

	/* Line number in the file of the delegate. */
	int32 LineNumber = 1;
};

// Stores either a :
// - UnitTest_
// - ComplexUnitTest_
// - IntegrationTest_
// - ComplexIntegrationTest_
// 
// UnitTest_ and IntegrationTest_ functions always generate exactly one test case.
// ComplexUnitTest_ and ComplexIntegrationTest_ functions may generate more, in that case you will have multiple
// instances of this struct with different test case names and arguments, but pointing to the same function.
// If bIsComplexTest is true it's a complex test, and in that case the ComplexTestParam should be passed to the function.
struct FAngelscriptTestDesc
{
	asIScriptFunction* Function;
	bool bIsComplexTest;
	FString ComplexTestParam;
};

/**
 * Description of a script module during preprocessing.
 */
struct FAngelscriptModuleDesc
{
	/* Name of the module in angelscript. Usually Dir.SubDir.ModuleName */
	FString ModuleName;

	/* Code sections to add to the module during compilation. Map is filename->processed code. */
	struct FCodeSection
	{
		FString RelativeFilename;
		FString AbsoluteFilename;
		FString Code;
		int64 CodeHash;
	};

	TArray<FCodeSection> Code;

	// Hash of the preprocessed code that was added to this module
	int64 CodeHash = 0;
	// Combined hash of all code in the module and other modules that it depends on
	int64 CombinedDependencyHash = 0;

	/* List of classes that will be compiled in this module. */
	TArray<TSharedRef<FAngelscriptClassDesc>> Classes;

	/* List of enums that will be compiled in this module. */
	TArray<TSharedRef<FAngelscriptEnumDesc>> Enums;

	/* List of delegates that will be compiled in this module. */
	TArray<TSharedRef<FAngelscriptDelegateDesc>> Delegates;

	/* Names of modules that should be imported into this module. */
	TArray<FString> ImportedModules;

	/* Names of functions in this module that should be executed after CDOs for classes are created. */
	TArray<FString> PostInitFunctions;

#if WITH_EDITOR
	/* Usage restrictions specified in this file with preprocessor macros. */
	struct FUsageRestriction
	{
		bool bIsAllow;
		FString Pattern;
	};

	TArray<FUsageRestriction> UsageRestrictions;

	// Each editor-only code block has its start and end line recorded here for error checking
	TArray<TPair<int,int>> EditorOnlyBlockLines;
#endif

	/* Internal angelscript data that get set during compilation. */
	class asCModule* ScriptModule = nullptr;
	const struct FAngelscriptPrecompiledModule* PrecompiledData = nullptr;
	bool bCompileError = false;
	bool bLoadedPrecompiledCode = false;
	bool bModuleSwapInError = false;

	/* All unit tests in this module. Maps function name -> function description struct. */
	TMap<FString, FAngelscriptTestDesc> UnitTestFunctions;

	/* All integration tests in this module. Maps function name -> function description struct. */
	TMap<FString, FAngelscriptTestDesc> IntegrationTestFunctions;

	// Find the class descriptor by name in this module
	TSharedPtr<FAngelscriptClassDesc> GetClass(const FString& ClassName)
	{
		for (auto ClassDesc : Classes)
		{
			if (ClassName == ClassDesc->ClassName)
			{
				return ClassDesc;
			}
		}
		return nullptr;
	}

	TSharedPtr<FAngelscriptClassDesc> GetClass(asITypeInfo* Type)
	{
		for (auto ClassDesc : Classes)
		{
			if (ClassDesc->ScriptType == Type)
			{
				return ClassDesc;
			}
		}
		return nullptr;
	}

	// Find the enum descriptor by name in this module
	TSharedPtr<FAngelscriptEnumDesc> GetEnum(const FString& EnumName)
	{
		for (auto EnumDesc : Enums)
		{
			if (EnumName == EnumDesc->EnumName)
			{
				return EnumDesc;
			}
		}
		return nullptr;
	}

	// TestFunctions are all functions in a module which match the signature 
	// void Test_<name>(FUnitTest& t).
	FAngelscriptTestDesc* GetUnitTestFunction(const FString& TestFunctionName)
	{
		FAngelscriptTestDesc* FunctionDesc = UnitTestFunctions.Find(TestFunctionName);
		if (FunctionDesc == nullptr)
		{
			return nullptr;
		}
		return FunctionDesc;
	}

	FAngelscriptTestDesc* GetIntegrationTestFunction(const FString& TestFunctionName)
	{
		FAngelscriptTestDesc* FunctionDesc = IntegrationTestFunctions.Find(TestFunctionName);
		if (FunctionDesc == nullptr)
		{
			return nullptr;
		}
		return FunctionDesc;
	}
};

/* Helper scope struct to print out performance information. */
struct ANGELSCRIPTCODE_API FAngelscriptScopeTimer
{
#if AS_PRINT_STATS
	double StartTime;
	FString Name;
public:
	FAngelscriptScopeTimer(const TCHAR* Name);
	~FAngelscriptScopeTimer();

	static void OutputTime(const TCHAR* Name, double Time);
#else
	FORCEINLINE FAngelscriptScopeTimer(const TCHAR* Name) {}
	FORCEINLINE static void OutputTime(const TCHAR* Name, double Time) {}
#endif
};

struct FAngelscriptScopeTotalTimer
{
#if AS_PRINT_STATS && AS_PRECOMPILED_STATS
	double* Timer;
	double StartTime;
public:
	FAngelscriptScopeTotalTimer(double& TotalTime);
	~FAngelscriptScopeTotalTimer();
#else
	FORCEINLINE FAngelscriptScopeTotalTimer(double& TotalTime) {}
#endif
};

inline uint32 GetTypeHash(const FAngelscriptManager::FFilenamePair& FilenamePair)
{
	return HashCombine(GetTypeHash(FilenamePair.AbsolutePath), GetTypeHash(FilenamePair.RelativePath));
}

inline bool operator==(const FAngelscriptManager::FFilenamePair& A, const FAngelscriptManager::FFilenamePair& B)
{
	return A.AbsolutePath == B.AbsolutePath && A.RelativePath == B.RelativePath;
}

#include "AngelscriptManager.h"
#include "Preprocessor/AngelscriptPreprocessor.h"
#include "ClassGenerator/AngelscriptClassGenerator.h"
#include "ClassGenerator/ASClass.h"
#include "ClassGenerator/ASStruct.h"
#include "Debugging/AngelscriptDebugServer.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/MessageDialog.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Engine/Engine.h"
#include "Engine/AssetManager.h"
#include "UObject/Package.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Interfaces/IPluginManager.h"

#include "AngelscriptCodeModule.h"
#include "AngelscriptInclude.h"
#include "AngelscriptBinds.h"
#include "AngelscriptDocs.h"
#include "AngelscriptBindDatabase.h"

#include "StaticJIT/PrecompiledData.h"
#include "StaticJIT/AngelscriptStaticJIT.h"
#include "StaticJIT/StaticJITHeader.h"

#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBorder.h"

#include "StartAngelscriptHeaders.h"
//#include "as_context.h"
//#include "as_scriptengine.h"
//#include "as_scriptfunction.h"
//#include "as_objecttype.h"
//#include "as_module.h"
//#include "as_builder.h"
#include "source/as_context.h"
#include "source/as_scriptengine.h"
#include "source/as_scriptfunction.h"
#include "source/as_objecttype.h"
#include "source/as_module.h"
#include "source/as_builder.h"
#include "EndAngelscriptHeaders.h"

#include "Testing/DiscoverTests.h"
#include "Testing/UnitTest.h"
#include "Testing/AngelscriptTestSettings.h"

#if WITH_AS_COVERAGE
#include "CodeCoverage/AngelscriptCodeCoverage.h"
#endif

DEFINE_LOG_CATEGORY(Angelscript);

static FName NAME_ReplicatedUsing("ReplicatedUsing");
static FName NAME_BlueprintSetter("BlueprintSetter");
static FName NAME_BlueprintGetter("BlueprintGetter");

TArray<FName> FAngelscriptManager::StaticNames;
TMap<FName, int32> FAngelscriptManager::StaticNamesByIndex;

FAngelscriptManager::FAngelscriptDebugStack* GAngelscriptStack = nullptr;
FAngelscriptManager* GAngelscriptManager = nullptr;
static bool GAngelscriptLineReentry = false;
bool FAngelscriptManager::bSimulateCooked = false;
bool FAngelscriptManager::bUseEditorScripts = false;
bool FAngelscriptManager::bIsInitialCompileFinished = false;
bool FAngelscriptManager::bScriptDevelopmentMode = false;
bool FAngelscriptManager::bTestErrors = false;
bool FAngelscriptManager::bIsHotReloading = false;
bool FAngelscriptManager::bForcePreprocessEditorCode = false;
bool FAngelscriptManager::bGeneratePrecompiledData = true;
bool FAngelscriptManager::bUseAutomaticImportMethod = false;
bool FAngelscriptManager::bStaticJITTranspiledCodeLoaded = false;
bool FAngelscriptManager::bUseScriptNameForBlueprintLibraryNamespaces = true;
TArray<FString> FAngelscriptManager::BlueprintLibraryNamespacePrefixesToStrip;
TArray<FString> FAngelscriptManager::BlueprintLibraryNamespaceSuffixesToStrip;

static int32 GAngelscriptRecompileAvoidance = 1;
static FAutoConsoleVariableRef CVar_AngelscriptRecompileAvoidance(TEXT("angelscript.UseRecompileAvoidance"), GAngelscriptRecompileAvoidance, TEXT(""));

UObject* FAngelscriptManager::CurrentWorldContext = nullptr;
class asCThreadLocalData* FAngelscriptManager::GameThreadTLD = nullptr;
thread_local FAngelscriptContextPool GAngelscriptContextPool;

void LogAngelscriptError(asSMessageInfo* Message, void* DataPtr);
void LogAngelscriptException(asIScriptContext* Context);
void AngelscriptLineCallback(asCContext* Context);
void AngelscriptStackPopCallback(asCContext* Context, void* OldStackFrameStart, void* OldStackFrameEnd);
void AngelscriptLoopDetectionCallback(asCContext* Context);

bool MakePathRelativeTo_IgnoreCase(FString& InPath, const TCHAR* InRelativeTo);

asIScriptContext* AngelscriptRequestContext(asIScriptEngine* Engine, void* Data);
void AngelscriptReturnContext(asIScriptEngine* Engine, asIScriptContext* Context, void* Data);

#if WITH_EDITOR && ENGINE_MAJOR_VERSION >= 5
void AngelscriptResolveObjectPtr(void** PointerToObjectPtr)
{
	((FObjectPtr*)PointerToObjectPtr)->Get();
}
#endif

bool FAngelscriptManager::IsInitialized()
{
	return GAngelscriptManager != nullptr;
}

FAngelscriptManager& FAngelscriptManager::Get()
{
	checkf(GAngelscriptManager != nullptr, TEXT("Attempted to use angelscript manager before initialization. Make sure FAngelscriptCodeModule::InitializeAngelscript has been called."));
	return *GAngelscriptManager;
}

FAngelscriptManager& FAngelscriptManager::GetOrCreate()
{
	if (GAngelscriptManager == nullptr)
		GAngelscriptManager = new FAngelscriptManager();
	return *GAngelscriptManager;
}

FString FAngelscriptManager::GetScriptRootDirectory()
{
	const auto& AllRootPaths = GAngelscriptManager->AllRootPaths;
	// The first root in the list of roots is the game project root.
	return AllRootPaths.IsEmpty() ? TEXT("") : GAngelscriptManager->AllRootPaths[0];
}

UPackage* FAngelscriptManager::GetPackage()
{
	return GAngelscriptManager->AngelscriptPackage;
}

bool FAngelscriptManager::ShouldInitializeThreaded()
{
#if WITH_EDITOR
	// In editor we default to un-threaded
	static bool bForceThreadedInitialize = FParse::Param(FCommandLine::Get(), TEXT("as-force-threaded-initialize"));
	return bForceThreadedInitialize;
#else
	// In cooked we default to threaded
	static bool bSkipThreadedInitialize = FParse::Param(FCommandLine::Get(), TEXT("as-skip-threaded-initialize"));
	return !bSkipThreadedInitialize;
#endif
}

void FAngelscriptManager::Initialize()
{
	PreInitialize_GameThread();

	if (ShouldInitializeThreaded())
	{
		volatile bool bInitializationDone = false;
		AsyncTask(ENamedThreads::AnyHiPriThreadHiPriTask, [&] {
			FGCScopeGuard GCLock;

			auto* RealGameThreadTLD = GameThreadTLD;
			GameThreadTLD = asCThreadManager::GetLocalData();
			GameThreadTLD->primaryContext = RealGameThreadTLD->primaryContext;

			Initialize_AnyThread();

			GameThreadTLD->primaryContext = nullptr;
			GameThreadTLD = RealGameThreadTLD;

			bInitializationDone = true;
		});

		while (!bInitializationDone)
		{
			FCoreDelegates::OnAsyncLoadingFlushUpdate.Broadcast();
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FPlatformProcess::Sleep(0.002f);
		}
	}
	else
	{
		Initialize_AnyThread();
	}

	PostInitialize_GameThread();
}

void FAngelscriptManager::PreInitialize_GameThread()
{
	/**
	 * Tell angelscript to use the appropriate allocators.
	 */
	asSetAllocScriptObjectFunction(&UASClass::AllocScriptObject, &UASClass::FinishConstructObject);

	ConfigSettings = GetMutableDefault<UAngelscriptSettings>();
	FAngelscriptManager::bUseAutomaticImportMethod = ConfigSettings->bAutomaticImports;

	FAngelscriptManager::bUseScriptNameForBlueprintLibraryNamespaces = ConfigSettings->bUseScriptNameForBlueprintLibraryNamespaces;
	FAngelscriptManager::BlueprintLibraryNamespacePrefixesToStrip = ConfigSettings->BlueprintLibraryNamespacePrefixesToStrip;
	FAngelscriptManager::BlueprintLibraryNamespaceSuffixesToStrip = ConfigSettings->BlueprintLibraryNamespaceSuffixesToStrip;

	// Sort the prefixes and suffixes by length
	// We sort to prioritize stripping long prefixes/suffixes, since eg. Library should not be stripped if we can strip BlueprintFunctionLibrary instead
	const auto SortByStringLength = [](const FString& LHS, const FString& RHS)
	{
		if(LHS.Len() == RHS.Len())
			return LHS > RHS;
		else
			return LHS.Len() > RHS.Len();
	};
	FAngelscriptManager::BlueprintLibraryNamespacePrefixesToStrip.Sort(SortByStringLength);
	FAngelscriptManager::BlueprintLibraryNamespaceSuffixesToStrip.Sort(SortByStringLength);

#if WITH_EDITOR && ENGINE_MAJOR_VERSION >= 5
	// In editor, we need to be able to resolve object pointers to make
	// unreal's access tracking and lazy resolving work.
	asSetResolveObjectPtrFunction(&AngelscriptResolveObjectPtr);
#endif

	/**
	 * Set up angelscript engine, used to bind c++ functions
	 * and compile script modules.
	 */
	Engine = (asCScriptEngine*)asCreateScriptEngine(ANGELSCRIPT_VERSION);

	// Set up thread local data for game thread
	GameThreadTLD = asCThreadManager::GetLocalData();
}

TArray<FString> FAngelscriptManager::MakeAllScriptRoots(bool bOnlyProjectRoot)
{
	FString RootPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Script"));

	// Create the script root folder if it doesn't exist
	auto& FileManager = IFileManager::Get();

#if WITH_EDITOR
	if (!FileManager.DirectoryExists(*RootPath))
		FileManager.MakeDirectory(*RootPath, true);
#endif

	// Find all plugin script roots
	TArray<FString> AllRootPaths;

	if (!bOnlyProjectRoot)
	{
		// Add the script root paths from all the enabled plugins
		IPluginManager& PluginManager = IPluginManager::Get();
		for (auto& Plugin : PluginManager.GetEnabledPluginsWithContent())
		{
			auto ScriptPath = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Script"));
			if (FileManager.DirectoryExists(*ScriptPath) && ScriptPath != RootPath)
			{
				AllRootPaths.Add(ScriptPath);
			}
		}

		// Make the search order somewhat deterministic
		AllRootPaths.Sort();
	}

	// Inject the project root first in the list so GetModuleByFilename looks there first.
	AllRootPaths.Insert(RootPath, 0);

	return AllRootPaths;
}

void FAngelscriptManager::Initialize_AnyThread()
{
	FAngelscriptManager::bSimulateCooked = FParse::Param(FCommandLine::Get(), TEXT("as-simulate-cooked"));
	FAngelscriptManager::bTestErrors = FParse::Param(FCommandLine::Get(), TEXT("as-test-errors"));
	FAngelscriptManager::bForcePreprocessEditorCode = FParse::Param(FCommandLine::Get(), TEXT("as-force-preprocess-editor-code"));
	FAngelscriptManager::bUseEditorScripts = WITH_EDITOR
		&& ((GIsEditor && !IsRunningCommandlet()) || FAngelscriptManager::bForcePreprocessEditorCode)
		&& !FAngelscriptManager::bSimulateCooked;

	// Store the angelscript package we create everything into
	AngelscriptPackage = NewObject<UPackage>(nullptr, FName(TEXT("/Script/Angelscript")), RF_Public | RF_Standalone | RF_MarkAsRootSet);
	AngelscriptPackage->SetPackageFlags(PKG_CompiledIn);

	AssetsPackage = NewObject<UPackage>(nullptr, FName(TEXT("/Script/AngelscriptAssets")), RF_Public | RF_Standalone | RF_MarkAsRootSet);
	AssetsPackage->SetPackageFlags(PKG_CompiledIn);

	Engine->SetEngineProperty(asEP_ALLOW_UNSAFE_REFERENCES, 1);
	Engine->SetEngineProperty(asEP_USE_CHARACTER_LITERALS, 1);
	Engine->SetEngineProperty(asEP_ALLOW_MULTILINE_STRINGS, 1);
	Engine->SetEngineProperty(asEP_SCRIPT_SCANNER, 1);

	Engine->SetEngineProperty(asEP_OPTIMIZE_BYTECODE, 1);

	Engine->SetEngineProperty(asEP_AUTO_GARBAGE_COLLECT, 0);
	Engine->SetEngineProperty(asEP_ALTER_SYNTAX_NAMED_ARGS, 1);

	Engine->SetEngineProperty(asEP_DISALLOW_VALUE_ASSIGN_FOR_REF_TYPE, 1);
	Engine->SetEngineProperty(asEP_ALLOW_IMPLICIT_HANDLE_TYPES, 1);
	Engine->SetEngineProperty(asEP_REQUIRE_ENUM_SCOPE, 1);
	Engine->SetEngineProperty(asEP_ALWAYS_IMPL_DEFAULT_CONSTRUCT, 1);

	Engine->SetEngineProperty(asEP_PROPERTY_ACCESSOR_MODE, AS_PROPERTY_ACCESSOR_MODE);

	Engine->SetEngineProperty(asEP_TYPECHECK_SWITCH_ENUMS, 1);

	Engine->SetEngineProperty(asEP_FLOAT_IS_FLOAT64, ConfigSettings->bScriptFloatIsFloat64 ? 1 : 0);
	Engine->SetEngineProperty(asEP_ALLOW_DOUBLE_TYPE, ConfigSettings->bDeprecateDoubleType ? 0 : 1);
	Engine->SetEngineProperty(asEP_WARN_ON_FLOAT_CONSTANTS_FOR_DOUBLES, ConfigSettings->bWarnOnFloatConstantsForDoubleValues ? 1 : 0);
	Engine->SetEngineProperty(asEP_WARN_INTEGER_DIVISION, ConfigSettings->bWarnIntegerDivision ? 1 : 0);

	if (FAngelscriptManager::bUseAutomaticImportMethod)
		Engine->SetEngineProperty(asEP_AUTOMATIC_IMPORTS, 1);

#if !WITH_AS_DEBUGSERVER && !WITH_AS_DEBUGVALUES
	Engine->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, 1);
#endif

	Engine->SetMessageCallback(asFUNCTION(LogAngelscriptError), 0, asCALL_CDECL);
	Engine->SetContextCallbacks(&AngelscriptRequestContext, &AngelscriptReturnContext, nullptr);

	bGeneratePrecompiledData = FParse::Param(FCommandLine::Get(), TEXT("as-generate-precompiled-data"));
	bScriptDevelopmentMode = GIsEditor || FParse::Param(FCommandLine::Get(), TEXT("as-development-mode"));
	bUsePrecompiledData = !bGeneratePrecompiledData && !FParse::Param(FCommandLine::Get(), TEXT("as-ignore-precompiled-data"))
		&& !IsRunningCommandlet() && !WITH_EDITOR && !bScriptDevelopmentMode;

	// Wait with the plugin script roots until we know we need them
	AllRootPaths = MakeAllScriptRoots(/*bOnlyProjectRoot =*/ true);

	if (bGeneratePrecompiledData)
		PrecompiledData = new FAngelscriptPrecompiledData(Engine);

	if (bGeneratePrecompiledData)
	{
		StaticJIT = new FAngelscriptStaticJIT();
		StaticJIT->PrecompiledData = PrecompiledData;

#if AS_CAN_GENERATE_JIT
		StaticJIT->bGenerateOutputCode = bGeneratePrecompiledData;
#endif

		Engine->SetEngineProperty(asEP_BUILD_WITHOUT_LINE_CUES, 1);
		Engine->SetJITCompiler(StaticJIT);
	}

	/*
	Start the debug server that external tools can connect to.
	*/
#if WITH_AS_DEBUGSERVER
	if ((!bUsePrecompiledData || bScriptDevelopmentMode) && FApp::HasProjectName())
	{
		int Port = 27099;
		FParse::Value(FCommandLine::Get(), TEXT("-asdebugport="), Port);
		DebugServer = new FAngelscriptDebugServer(Port);
	}
#endif

#if WITH_AS_COVERAGE
	if (FAngelscriptCodeCoverage::CoverageEnabled())
	{
		CodeCoverage = new FAngelscriptCodeCoverage;
	}
#endif

#if AS_USE_BIND_DB
	{
		FAngelscriptScopeTimer Timer(TEXT("load bind database"));
		FAngelscriptBindDatabase::Get().Load(GetScriptRootDirectory() / TEXT("Binds.Cache"), bGeneratePrecompiledData);
	}
#endif	
	//WILL-EDIT
	TSharedPtr<IPlugin> plugin = IPluginManager::Get().FindPlugin("Angelscript");		

	if (plugin)
	{
		FModuleManager::Get().LoadModule(FName("AngelscriptNativeBinds"), ELoadModuleFlags::LogFailures);
	}

	//if (plugin)
	//{
	//	FAngelscriptBinds::LoadBindModules(plugin->GetBaseDir() / "BindModules.Cache");
	//}
	//
	////WILL-EDIT: Load our auto-generated Bind Modules here
	//if (!FAngelscriptBinds::BindModuleNames.IsEmpty())
	//{
	//	for (FString ModuleName : FAngelscriptBinds::BindModuleNames)
	//	{			
	//		if (!ModuleName.IsEmpty())
	//		{				
	//			FModuleManager::Get().LoadModule(FName(ModuleName), ELoadModuleFlags::LogFailures);				
	//		}
	//	}
	//}
	//Set everything up for angelscript usage.	
	{
		FAngelscriptScopeTimer Timer(TEXT("== bindings total =="));
		BindScriptTypes();
	}
	
#if !AS_USE_BIND_DB
	// If we aren't using the database, write it during cook
	const bool bSkipWriteBindDB = FParse::Param(FCommandLine::Get(), TEXT("as-skip-write-bind-db"));
	const bool bForceWriteBindDB = FParse::Param(FCommandLine::Get(), TEXT("as-write-bind-db"));
	if ((IsRunningCommandlet() && !bSkipWriteBindDB) || bForceWriteBindDB)
	{
		UE_LOG(Angelscript, Log, TEXT("Writing angelscript bind database to Binds.Cache file"));
		FAngelscriptBindDatabase::Get().Save(GetScriptRootDirectory() / TEXT("Binds.Cache"));
	}

#elif AS_USE_BIND_DB
	FAngelscriptBindDatabase::Get().Clear();
#endif

	// Load precompiled data if it is available and we can use it
	if (bUsePrecompiledData)
	{
		FAngelscriptScopeTimer Timer(TEXT("load precompiled data"));

		FString Filename;
			
		// Try configuration-specific precompiled script files for easier debugging
#if UE_BUILD_SHIPPING
		Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Shipping.Cache");
#elif UE_BUILD_TEST
		Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Test.Cache");
#elif UE_BUILD_DEVELOPMENT
		Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript_Development.Cache");
#endif

		if (!IFileManager::Get().FileExists(*Filename))
			Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");

		if (IFileManager::Get().FileExists(*Filename))
		{
			PrecompiledData = new FAngelscriptPrecompiledData(Engine);
			PrecompiledData->Load(Filename);

			if (!PrecompiledData->IsValidForCurrentBuild())
			{
				delete PrecompiledData;
				PrecompiledData = nullptr;

				UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data was for a different build configuration. Discarding all precompiled data."));
			}
			else
			{
				if (StaticJIT != nullptr)
					StaticJIT->PrecompiledData = PrecompiledData;
				if (!bScriptDevelopmentMode)
					PrecompiledData->bMinimizeMemoryUsage = true;

				// If we have compiled in JIT code, we can only use it if it matches the precompiled data
				const FStaticJITCompiledInfo* CompiledInfo = FStaticJITCompiledInfo::Get();
				if (CompiledInfo != nullptr && CompiledInfo->PrecompiledDataGuid != PrecompiledData->DataGuid)
				{
					UE_LOG(Angelscript, Warning, TEXT("Loaded angelscript precompiled data does not match the transpiled C++ in the game binary. Transpiled code will not be used!"));
					FJITDatabase::Get().Clear();
				}
			}
		}
	}
	else
	{
		StaticNames.Reserve(7000);
	}

	// Setup thread local data
	GameThreadTLD->primaryContext = CreateContext();

	// Perform the initial compile of all script files
	InitialCompile();

	bool bForcedExit = false;

#if AS_CAN_GENERATE_JIT
	// If we're in static jit generate mode, write it to the output files
	if (StaticJIT != nullptr && StaticJIT->bGenerateOutputCode)
	{
		StaticJIT->WriteOutputCode();
		bForcedExit = true;
	}
#endif

	// Save out precompiled data if we indicated we should
	if (bGeneratePrecompiledData)
	{
		FString Filename = GetScriptRootDirectory() / TEXT("PrecompiledScript.Cache");
		PrecompiledData->InitFromActiveScript();
		PrecompiledData->Save(Filename);
		bForcedExit = true;
	}

	if (PrecompiledData != nullptr)
	{
		// See if we actually loaded and are going to use any transpiled code.
		bStaticJITTranspiledCodeLoaded = FJITDatabase::Get().Functions.Num() > 0;

		// Delete any precompiled data we used during initial compile
		if (!bScriptDevelopmentMode && !bGeneratePrecompiledData)
			PrecompiledData->ClearUnneededRuntimeData();

		delete PrecompiledData;
		PrecompiledData = nullptr;
		FJITDatabase::Clear();
	}

	// We may have requested an exit due to compilation
	if (bForcedExit)
	{
		FPlatformMisc::RequestExitWithStatus(false, 0);
	}

#if AS_CAN_HOTRELOAD
	HotReloadTestRunner = new FHotReloadTestRunner();
#endif

	// Use the checker thread if we want to detect hot reloads,
	// but we don't have access to the editor. In editor, the AngelscriptEditor
	// module will use the directory watcher system to detect reloads instead.
	bUseHotReloadCheckerThread = bScriptDevelopmentMode && !GIsEditor;
	if (bUseHotReloadCheckerThread)
		StartHotReloadThread();

#if AS_PRINT_STATS && AS_PRECOMPILED_STATS
	if (bUsePrecompiledData)
		FAngelscriptPrecompiledData::OutputTimingData();
#endif

#if WITH_EDITOR && WITH_AS_COVERAGE
	FCoreDelegates::OnPostEngineInit.AddLambda([&]()
	{
		if (CodeCoverage != nullptr)
		{
			CodeCoverage->AddTestFrameworkHooks();
		}
	});
#endif

#if !UE_BUILD_SHIPPING
	FCoreDelegates::OnGetOnScreenMessages.AddRaw(this, &FAngelscriptManager::GetOnScreenMessages);
#endif
	UpdateLineCallbackState();
}

bool FAngelscriptManager::IsGeneratingPrecompiledData()
{
	return GAngelscriptManager->StaticJIT != nullptr;
}

void FAngelscriptManager::PostInitialize_GameThread()
{
	FAngelscriptCodeModule::GetOnInitialCompileFinished().Broadcast();
}

void FAngelscriptManager::StartHotReloadThread()
{
	if (bUsedPrecompiledDataForPreprocessor)
		return;
	if (!bUseHotReloadCheckerThread)
		return;
	if (bHotReloadThreadStarted)
		return;
	bHotReloadThreadStarted = true;

	// Do a check to start with before starting the thread,
	// this will pre-fill all the timestamps. Discard the actual file change events.
	CheckForFileChanges();
	FileChangesDetectedForReload.Empty();

#if AS_CAN_HOTRELOAD
	struct FAngelscriptHotReloadThread : public FRunnable
	{
		bool bRunning = true;

		uint32 Run() override
		{
			auto& Manager = FAngelscriptManager::Get();
			while(bRunning)
			{
				if (!Manager.bUseHotReloadCheckerThread)
					break;

				if (Manager.bWaitingForHotReloadResults)
				{
					Manager.CheckForFileChanges();
					Manager.bWaitingForHotReloadResults = false;
				}
				FPlatformProcess::Sleep(0.001f);
			}
			return 0;
		}

		void Stop() override { bRunning = false; }
		void Exit() override { bRunning = false; }
	};

	FRunnableThread::Create(new FAngelscriptHotReloadThread(), TEXT("AngelscriptHotReload"), 0, EThreadPriority::TPri_Lowest);
#endif
}

asCContext* FAngelscriptManager::CreateContext()
{
	// Create a new context
	auto* Context = (asCContext*)Engine->CreateContext();
	Context->SetExceptionCallback(asFUNCTION(LogAngelscriptException), 0, asCALL_CDECL);
#if WITH_AS_DEBUGVALUES || WITH_AS_DEBUGSERVER
	Context->SetLineCallback(AngelscriptLineCallback);
	Context->SetStackPopCallback(AngelscriptStackPopCallback);
#endif
#if WITH_EDITOR
	if (!IsRunningCommandlet())
		Context->SetLoopDetectionCallback(AngelscriptLoopDetectionCallback);
#elif !UE_BUILD_TEST && !UE_BUILD_SHIPPING
	Context->SetLoopDetectionCallback(AngelscriptLoopDetectionCallback);
#endif
	return Context;
}

asIScriptContext* AngelscriptRequestContext(asIScriptEngine* Engine, void* Data)
{
	// Take a context from the thread-local pool if we have one
	auto& LocalPool = GAngelscriptContextPool;
	if (LocalPool.FreeContexts.Num() > 0)
	{
		auto* Context = LocalPool.FreeContexts.Pop(false);
		check(Context->GetState() != asEXECUTION_ACTIVE);
		check(Context->GetState() != asEXECUTION_SUSPENDED);
		return Context;
	}

	auto& Manager = FAngelscriptManager::Get();
	return Manager.CreateContext();
}

void AngelscriptReturnContext(asIScriptEngine* Engine, asIScriptContext* Context, void* Data)
{
	check(Context->GetState() != asEXECUTION_ACTIVE);
	check(Context->GetState() != asEXECUTION_SUSPENDED);

	// Return context to the thread local context poos
	auto& LocalPool = GAngelscriptContextPool;
	if (LocalPool.FreeContexts.Num() < AS_MAX_POOLED_CONTEXTS)
	{
		LocalPool.FreeContexts.Push((asCContext*)Context);
		return;
	}

	// Can't add to global pool, just deallocate
	Context->Release();
}

FAngelscriptPooledContextBase::FAngelscriptPooledContextBase()
{
	Init(asCThreadManager::GetLocalData());
}

void FAngelscriptPooledContextBase::PrepareExternal(asIScriptFunction* Function)
{
	(*this)->Prepare(Function);
}

void FAngelscriptPooledContextBase::ExecuteExternal()
{
	(*this)->Execute();
}

void FAngelscriptPooledContextBase::Init(asCThreadLocalData* tld)
{
	asCContext* ActiveContext = tld->activeContext;
	if (ActiveContext != nullptr)
	{
		auto State = ActiveContext->m_status;
		if (State == asEXECUTION_ACTIVE)
		{
			Context = ActiveContext;
			Context->PushState();
			bWasNested = true;
			return;
		}
	}

	// Take a context from the thread-local pool if we have one
	auto& LocalPool = GAngelscriptContextPool;
	if (LocalPool.FreeContexts.Num() > 0)
	{
		Context = LocalPool.FreeContexts.Pop(false);
		check(Context->GetState() != asEXECUTION_ACTIVE);
		check(Context->GetState() != asEXECUTION_SUSPENDED);
		bWasNested = false;
		return;
	}

	// Take a context from the global pool if we have one
	auto& Manager = FAngelscriptManager::Get();
	if (Manager.GlobalContextPool.Num() > 0)
	{
		FScopeLock Lock(&Manager.GlobalContextPoolLock);
		if (Manager.GlobalContextPool.Num() > 0)
		{
			Context = Manager.GlobalContextPool.Pop(false);
			Context->MovedToNewThread();
			check(Context->GetState() != asEXECUTION_ACTIVE);
			check(Context->GetState() != asEXECUTION_SUSPENDED);
			bWasNested = false;
			return;
		}
	}

	// Create a new context if none was found
	Context = Manager.CreateContext();
	bWasNested = false;
}

FAngelscriptPooledContextBase::FAngelscriptPooledContextBase(FAngelscriptPooledContextBase&& Other)
{
	Context = Other.Context;
	bWasNested = Other.bWasNested;
	Other.Context = nullptr;
}

FAngelscriptPooledContextBase::~FAngelscriptPooledContextBase()
{
	if (Context == nullptr)
		return;

	if (bWasNested)
	{
		Context->PopState();
		return;
	}

	if (Context->m_status == asEXECUTION_EXCEPTION)
	{
		// Contexts that caused an exception should be cleaned up,
		// so all the objects still on the stack get destructed properly
		Context->Unprepare();
	}

	check(Context->GetState() != asEXECUTION_ACTIVE);
	check(Context->GetState() != asEXECUTION_SUSPENDED);

	// Return context to the thread local context poos
	auto& LocalPool = GAngelscriptContextPool;
	if (LocalPool.FreeContexts.Num() < AS_MAX_POOLED_CONTEXTS)
	{
		LocalPool.FreeContexts.Push(Context);
		return;
	}

	// Local context pool is full, return it to the global one
	auto& Manager = FAngelscriptManager::Get();
	{
		FScopeLock Lock(&Manager.GlobalContextPoolLock);
		if (Manager.GlobalContextPool.Num() < AS_MAX_POOLED_CONTEXTS)
		{
			Manager.GlobalContextPool.Push(Context);
			return;
		}
	}

	// Global context pool is also full, just deallocate the context
	Context->Release();
}

FAngelscriptContextPool::~FAngelscriptContextPool()
{
	// NOTE: Don't access FAngelscriptManager here since this destructor is being called at the destruction of every thread (it's a thread_local)
	// and there's no guarantee AngelscriptManager is still around (e.g. if another global static destroys a thread in the destructor).
	if (IsEngineExitRequested())
		return;
	for (auto* Context : FreeContexts)
		Context->Release();
}

void FAngelscriptManager::BindScriptTypes()
{
	FAngelscriptBinds::CallBinds();
}

void FAngelscriptManager::FindScriptFiles(
	IFileManager& FileManager,
	const FString& RelativeRoot,
	const FString& SearchDirectory,
	const TCHAR* Pattern,
	TArray<FFilenamePair>& OutFilenames,
	bool bSkipDevelopmentScripts,
	bool bSkipEditorScripts)
{
	FString CurrentSearch = SearchDirectory / Pattern;

	TArray<FString> LocalFiles;
	FileManager.FindFiles(LocalFiles, *CurrentSearch, true, false);

	for (const FString& FoundFile : LocalFiles)
	{
		OutFilenames.Add(FFilenamePair{
			SearchDirectory / FoundFile,
			RelativeRoot / FoundFile
			});
	}

	TArray<FString> LocalDirs;
	FString RecursiveDirSearch = SearchDirectory / TEXT("*");
	FileManager.FindFiles(LocalDirs, *RecursiveDirSearch, false, true);

	// FindFiles can return the same dir twice on Linux sometimes so eliminate dupes.
	for (const FString& FoundDirectory : TSet<FString>(LocalDirs))
	{
		if (bSkipDevelopmentScripts)
		{
			if (FoundDirectory == TEXT("Examples"))
				continue;
			if (FoundDirectory == TEXT("Dev"))
				continue;
		}

		if (bSkipEditorScripts)
		{
			if (FoundDirectory == TEXT("Editor"))
				continue;
		}

		FindScriptFiles(
			FileManager,
			RelativeRoot / FoundDirectory,
			SearchDirectory / FoundDirectory,
			Pattern,
			OutFilenames,
			bSkipDevelopmentScripts,
			bSkipEditorScripts
		);
	}
}

void FAngelscriptManager::FindAllScriptFilenames(TArray<FFilenamePair>& OutFilenames)
{
	const bool bSkipDevelopmentScripts = !FAngelscriptManager::bUseEditorScripts;
	const bool bSkipEditorScripts = bSkipDevelopmentScripts;

	for (auto& Path : AllRootPaths)
	{
		FindScriptFiles(
			IFileManager::Get(),
			TEXT(""),
			Path,
			TEXT("*.as"),
			OutFilenames,
			bSkipDevelopmentScripts,
			bSkipEditorScripts);
	}
}

bool FAngelscriptManager::HasAnyDebugServerClients()
{
#if WITH_AS_DEBUGSERVER
	if (DebugServer == nullptr)
		return false;
	if (DebugServer->HasAnyClients())
		return true;
#endif
	return false;
}

void FAngelscriptManager::ReplaceScriptAssetContent(FString AssetName, TArray<FString> AssetContent)
{
#if WITH_AS_DEBUGSERVER
	FAngelscriptReplaceAssetDefinition Message;
	Message.AssetName = AssetName;
	Message.Lines = AssetContent;
	DebugServer->SendMessageToAll(EDebugMessageType::ReplaceAssetDefinition, Message);
#endif
}

void FAngelscriptManager::InitialCompile()
{
	bool bSuccess = true;
	TArray<TSharedRef<FAngelscriptModuleDesc>> ModulesToCompile;
	TArray<FFilenamePair> Filenames;

	ResetDiagnostics();

	if (PrecompiledData != nullptr && bUsePrecompiledData && !bScriptDevelopmentMode)
	{
		FAngelscriptScopeTimer Timer(TEXT("instantiating precompiled descriptors"));

		// Use precompiled data instead of the preprocessor
		bUsedPrecompiledDataForPreprocessor = true;
		ModulesToCompile = PrecompiledData->GetModulesToCompile();
		
#if AS_CAN_HOTRELOAD
		UE_LOG(Angelscript, Warning, TEXT("Using fully precompiled scripts. Hot reloading is disabled for this run."));
		UE_LOG(Angelscript, Warning, TEXT("Delete PrecompiledScript.Cache or run with -as-development-mode flag to enable hot reload."));
#endif
	}
	else
	{
		// Make sure we scan all plugins for script roots as well, now that we know we need them.
		AllRootPaths = MakeAllScriptRoots();
		for (const FString& Path : AllRootPaths)
		{
			UE_LOG(Angelscript, Display, TEXT("Angelscript root path: %s"), *Path);
		}

		// Use preprocessor to read script files from disk
		FAngelscriptPreprocessor Preprocessor;

		{
			FAngelscriptScopeTimer Timer(TEXT("load script files from disk"));

			/* Add all files from the script root recursively.*/
			FindAllScriptFilenames(Filenames);

			for (FFilenamePair& Filename : Filenames)
				Preprocessor.AddFile(Filename.RelativePath, Filename.AbsolutePath);
		}

		bSuccess = Preprocessor.Preprocess();
		ModulesToCompile = Preprocessor.GetModulesToCompile();
	}

	if (bSuccess)
	{
		TArray<TSharedRef<FAngelscriptModuleDesc>> CompiledModules;
		ECompileResult Result = CompileModules(ECompileType::Initial, ModulesToCompile, CompiledModules);
		if (Result == ECompileResult::Error)
		{
			bSuccess = false;
		}
	}
	else
	{
		UE_LOG(Angelscript, Error, TEXT("Angelscript preprocessing failed!"));
	}

	// Don't allow commandlets to run if angelscript is not there
	if (!bSuccess && (IsRunningCommandlet() || FParse::Param(FCommandLine::Get(), TEXT("as-exit-on-error"))))
	{
		UE_LOG(Angelscript, Error, TEXT("Cannot run when angelscript has failed to compile. Requesting exit."));

		GIsCriticalError = true;
		FPlatformMisc::RequestExit(true);
	}
	else if (!bSuccess)
	{
		bool bPreviousUseHotReloadCheckerThread = bUseHotReloadCheckerThread;

		// Next hot-reload compiles everything, since it's hard to tell what needs reloading.
		for (const FFilenamePair& Filename : Filenames)
			PreviouslyFailedReloadFiles.Add(Filename);

		volatile bool bErrorResponseDone = false;
		auto ShowErrorDialog = [&]()
		{
			// Prematurely start the hot reload thread since we will be using it in our modal window
			bUseHotReloadCheckerThread = true;
			StartHotReloadThread();

			auto PromptWindow = SNew(SWindow)
				.Title(FText::FromString("Angelscript Compile Errors"))
				.ClientSize(FVector2D(800, 600))
				.SizingRule(ESizingRule::UserSized);

			auto PromptText = SNew(STextBlock);
			PromptWindow->SetContent(
				SNew(SBorder)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
						.Padding(10.f)
						[
							PromptText
						]
				]);

			auto TickHandle = FSlateApplication::Get().GetOnModalLoopTickEvent().AddLambda([&](float DeltaTime)
			{
				if (PreviouslyFailedReloadFiles.Num() == 0)
				{
					// We succesfully compiled! Close the prompt.
					FSlateApplication::Get().RequestDestroyWindow(PromptWindow);
					bSuccess = true;
				}

				// Show an error and prompt for retry
				FString Callstack = FormatDiagnostics();
				FString Message = FString::Printf(
					TEXT("Angelscript code failed to compile at engine startup.\n")
					TEXT("Various assets will not load correctly without working angelscript code.\n")
					TEXT("\n\nPlease fix the errors and save the script files to proceed to open the editor.")
					TEXT("\n\n%s"),
					*Callstack);
				PromptText->SetText(FText::FromString(Message));

				// Make sure we detect hot reloads when we need them
				CheckForHotReload(ECompileType::FullReload);

				// Run the debug server if we have one to send diagnostics through
	#if WITH_AS_DEBUGSERVER
				if (DebugServer != nullptr)
					DebugServer->ProcessMessages();
	#endif
			});

			FSlateApplication::Get().AddModalWindow(PromptWindow, nullptr);
			FSlateApplication::Get().GetOnModalLoopTickEvent().Remove(TickHandle);

			if (!bSuccess)
			{
				FPlatformMisc::RequestExit(true);
				return;
			}
			else
			{
				bErrorResponseDone = true;
			}
		};

		if (IsInGameThread())
		{
			ShowErrorDialog();
		}
		else
		{
			AsyncTask(ENamedThreads::GameThread, [&]() { ShowErrorDialog(); });

			while (!bErrorResponseDone)
				FPlatformProcess::Sleep(0.01f);
		}

		// Reset the hot reload detection mmethod we were using
		bUseHotReloadCheckerThread = bPreviousUseHotReloadCheckerThread;
	}

	// In order to provide proper support for tests that need the AssetManager
	// and UPrimaryDataAsset already created, we need to delay the test discovery
	// until the initial scan is finished.
	FCoreDelegates::OnPostEngineInit.AddLambda([&]()
	{
		UAssetManager* AssetManager = UAssetManager::GetIfInitialized();
		if (AssetManager != nullptr)
		{
			AssetManager->CallOrRegister_OnCompletedInitialScan(
				FSimpleMulticastDelegate::FDelegate::CreateLambda([&]() {
					DiscoverTests();
					bCompletedAssetScan = true;
				})
			);
		}
		else
		{
			UE_LOG(Angelscript, Warning, TEXT("Asset Manager was not ready in PostEngineInit. Tests are discovered without completing an initial asset scan."));
			DiscoverTests();
		}
	});

	bDidInitialCompileSucceed = bSuccess;
	bIsInitialCompileFinished = true;

#if WITH_EDITOR
	if (FParse::Param(FCommandLine::Get(), TEXT("dump-as-doc")))
	{
		FAngelscriptDocs::DumpDocumentation(Engine);
		FPlatformMisc::RequestExit(false);
	}
#endif
}

void FAngelscriptManager::DiscoverTests()
{
#if WITH_EDITOR
	if (!GetDefault<UAngelscriptTestSettings>()->bEnableTestDiscovery)
	{
		return;
	}
	if (FAngelscriptManager::bSimulateCooked || IsRunningCookCommandlet())
	{
		// It doesn't make sense to run tests in simulate cooked mode since that's meant to simulate the
		// AS that runs in a server or client - tests will never run there. Likewise actually cooking.
		return;
	}
	for (auto& ActiveModule : GetActiveModules())
	{
		DiscoverUnitTests(*ActiveModule, ActiveModule->UnitTestFunctions);
		DiscoverIntegrationTests(*ActiveModule, ActiveModule->IntegrationTestFunctions);
	}
#endif
}

bool FAngelscriptManager::PerformHotReload(ECompileType CompileType, const TArray<FFilenamePair>& InReloadFiles)
{
	TGuardValue<bool> ScopeHotReloading(FAngelscriptManager::bIsHotReloading, true);
	FAngelscriptScopeTimer Timer(TEXT("==script reload total =="));

	// Create progress indicator
	FScopedSlowTask SlowTask(3.f, FText::FromString(TEXT("Angelscript Hot Reload")));
	if (CompileType == ECompileType::FullReload && bIsInitialCompileFinished)
		SlowTask.MakeDialogDelayed(0.5f);
	SlowTask.EnterProgressFrame(0.5f);

	FAngelscriptPreprocessor Preprocessor;

	TSet<FFilenamePair> AlreadyDeletedFiles;
	TArray<FFilenamePair> FileList;
	FileList.Append(InReloadFiles);

	// Any files we tried to reload before but failed to should also be reload now.
	auto& FileManager = IFileManager::Get();
	for (auto& FailedFile : PreviouslyFailedReloadFiles)
	{
		// If it was already deleted before, remember that
		if (!FileManager.FileExists(*FailedFile.AbsolutePath))
			AlreadyDeletedFiles.Add(FailedFile);

		FileList.AddUnique(FailedFile);
	}
	PreviouslyFailedReloadFiles.Empty();

	// Build a set of all files which are dependent on any of the modified files,
	// such that we can hot reload all of them.
	TSet<FFilenamePair> FilesToHotReload;
	if (FileList.Num() > 0)
	{
		if (GAngelscriptRecompileAvoidance && FAngelscriptManager::bUseAutomaticImportMethod)
		{
			// When using recompile avoidance, dependency handling is done by the compile step,
			// so we don't track it here, we only reload actually changed files.
			FilesToHotReload.Append(FileList);
		}
		else
		{
			FAngelscriptScopeTimer DependencyCheckTimer(TEXT("reload dependency check"));
			FilesToHotReload.Reserve(ActiveModules.Num() * 2);

			// Build a lookup table from relative file path -> module
			TMap<FString, FAngelscriptModuleDesc*> RelativeFileToModule;
			RelativeFileToModule.Reserve(ActiveModules.Num() * 2);

			TMap<asCModule*, FAngelscriptModuleDesc*> ScriptModuleToModule;
			ScriptModuleToModule.Reserve(ActiveModules.Num() * 2);

			for (auto& Module : ActiveModules)
			{
				auto ModulePtr = &(Module.Value.Get());
				for (const auto& Section : ModulePtr->Code)
					RelativeFileToModule.Add(Section.RelativeFilename, ModulePtr);

				if (ModulePtr->ScriptModule != nullptr)
					ScriptModuleToModule.Add((asCModule*)ModulePtr->ScriptModule, ModulePtr);
			}

			if (FAngelscriptManager::bUseAutomaticImportMethod)
			{
				// We will need to progressively mark all modules that depend on one of the files that should be reloaded
				TSet<asCModule*> MarkedModules;
				MarkedModules.Reserve(ActiveModules.Num());

				// Push the modules for all changed files on the module job stack
				for (auto& File : FileList)
				{
					if (auto* ModulePtr = RelativeFileToModule.Find(File.RelativePath))
					{
						if ((*ModulePtr)->ScriptModule != nullptr)
							MarkedModules.Add((asCModule*)((*ModulePtr)->ScriptModule));
					}
					else
					{
						FilesToHotReload.Add(File);
					}
				}

				// Keep marking modules until we settle down
				bool bDidMarkModules = true;
				while (bDidMarkModules)
				{
					bDidMarkModules = false;

					for (auto& Module : ActiveModules)
					{
						auto* ScriptModule = (asCModule*)Module.Value->ScriptModule;
						if (ScriptModule == nullptr)
							continue;
						if (MarkedModules.Contains(ScriptModule))
							continue;

						bool bIsDependent = false;
						for (const auto& DependencyElem : ScriptModule->moduleDependencies)
						{
							if (MarkedModules.Contains(DependencyElem.Key))
							{
								bIsDependent = true;
								break;
							}
						}

						if (bIsDependent)
						{
							MarkedModules.Add(ScriptModule);
							bDidMarkModules = true;
						}
					}
				}

				// Queue up reloads for all marked modules
				for (asCModule* ReloadModule : MarkedModules)
				{
					if (auto* ModulePtr = ScriptModuleToModule.Find(ReloadModule))
					{
						for (const auto& Section : (*ModulePtr)->Code)
							FilesToHotReload.Add(FFilenamePair{ Section.AbsoluteFilename, Section.RelativeFilename });
					}
				}
			}
			else
			{
				// Book-keeping over the modules which have been visited when
				// traversing the module dependencies.
				TSet<FAngelscriptModuleDesc*> VisitedModules;
				VisitedModules.Reserve(ActiveModules.Num());

				// A job stack of modules which should be visited
				TArray<FAngelscriptModuleDesc*> ModuleJobs;
				ModuleJobs.Reserve(ActiveModules.Num());

				// Push the modules for all changed files on the module job stack
				for (auto& File : FileList)
				{
					FilesToHotReload.Add(File);
					if (auto ModulePtr = RelativeFileToModule.Find(File.RelativePath))
					{
						ModuleJobs.AddUnique(*ModulePtr);
						VisitedModules.Add(*ModulePtr);
					}
				}


				// Build a reverse dependency map for module->dependent modules (non-recursive)
				TMap<FAngelscriptModuleDesc*, TArray<FAngelscriptModuleDesc*>> ReverseDeps;
				if (ModuleJobs.Num() > 0)
				{
					ReverseDeps.Reserve(ActiveModules.Num());
					for (auto& Module : ActiveModules)
					{
						auto ModulePtr = &(Module.Value.Get());
						for (const FString& ImportedModule : Module.Value->ImportedModules)
						{
							auto ImportedModulePtr = &(ActiveModules.Find(ImportedModule)->Get());
							ReverseDeps.FindOrAdd(ImportedModulePtr).Add(ModulePtr);
						}
					}
				}


				// Add all files associated with the visited modules and recurse through
				// the dependent modules.
				while (ModuleJobs.Num() > 0)
				{
					auto ModulePtr = ModuleJobs.Pop(false);

					for (const auto& Section : ModulePtr->Code)
					{
						FilesToHotReload.Add(FFilenamePair{ Section.AbsoluteFilename, Section.RelativeFilename });
					}

					if (auto DependentModulesPtr = ReverseDeps.Find(ModulePtr))
					{
						for (auto ModuleDepPtr : *DependentModulesPtr)
						{
							if (!VisitedModules.Contains(ModuleDepPtr))
							{
								VisitedModules.Add(ModuleDepPtr);
								ModuleJobs.Push(ModuleDepPtr);
							}
						}
					}
				}
			}
		}
	}

	// Mark all needed files for preprocessing
	for (const auto& PathPair : FilesToHotReload)
	{
		const bool bTreatAsDeleted = AlreadyDeletedFiles.Num() != 0 && AlreadyDeletedFiles.Contains(PathPair);
		Preprocessor.AddFile(PathPair.RelativePath, PathPair.AbsolutePath, bTreatAsDeleted);
	}

	bool bPreprocessSuccess = Preprocessor.Preprocess();
	if (!bPreprocessSuccess)
	{
		UE_LOG(Angelscript, Error, TEXT("Hot reload failed in preprocessing. Keeping all old angelscript code."));
		PreviouslyFailedReloadFiles.Append(FileList);
		EmitDiagnostics();
		return false;
	}

	// Notify for progress after preprocessor
	SlowTask.EnterProgressFrame(2.5f);

	TArray<TSharedRef<FAngelscriptModuleDesc>> CompiledModules;
	ECompileResult Result = CompileModules(CompileType, Preprocessor.GetModulesToCompile(), CompiledModules);
	if (Result == ECompileResult::ErrorNeedFullReload)
	{
		return false;
	}
	else if (Result == ECompileResult::Error)
	{
		return false;
	}

	// In the scenario where the initial compile fails and the user presses "Try Again" GEngine is nullptr.
	// Since the unit tests assumes an existing GEngine we need to skip the unit testing in that case.
	// Asset Manager initial scan should be completed also to queue tests for executing after hot reload finishes.
	if (GEngine && bCompletedAssetScan && HotReloadTestRunner != nullptr && HotReloadTestRunner->ShouldRunUnitTestsOnHotReload())
	{
		TArray<FString> RelativeFileList;
		RelativeFileList.Reserve(FileList.Num());
		for (const auto& FilenamePair : FileList)
		{
			RelativeFileList.Add(FilenamePair.RelativePath);
		}
		HotReloadTestRunner->PrepareTests(GetActiveModules(), CompiledModules, RelativeFileList, FAngelscriptManager::bUseAutomaticImportMethod);
	}

	if(Result == ECompileResult::FullyHandled || Result == ECompileResult::PartiallyHandled)
	{
		FAngelscriptPostCompileClassCollection& PostCompileDelegate = FAngelscriptCodeModule::GetPostCompileClassCollection();
			if (PostCompileDelegate.IsBound())
				PostCompileDelegate.Broadcast(CompiledModules);
	}

#if WITH_AS_DEBUGSERVER
	// Make sure all our breakpoints are applied to modules that might be newly compiled now
	if (DebugServer != nullptr)
		DebugServer->ReapplyBreakpoints();
#endif

	return true;
}

bool FAngelscriptManager::VerifyPropertySpecifiers(const TArray<TSharedRef<FAngelscriptModuleDesc>>& Modules)
{
	bool bPassedVerification = true;
	for (const TSharedRef<FAngelscriptModuleDesc>& Module : Modules)
	{
		for (const TSharedRef<FAngelscriptClassDesc>& Class : Module->Classes)
		{
			for (const TSharedRef<FAngelscriptPropertyDesc>& Property : Class->Properties)
			{
				FString* RepNotifyFunc = Property->Meta.Find(NAME_ReplicatedUsing);
				bPassedVerification &= VerifyRepFunc(RepNotifyFunc, Property, Class, Module);

				FString* BlueprintSetFunc = Property->Meta.Find(NAME_BlueprintSetter);
				bPassedVerification &= VerifyBlueprintSetFunc(BlueprintSetFunc, Property, Class, Module);

				FString* BlueprintGetFunc = Property->Meta.Find(NAME_BlueprintGetter);
				bPassedVerification &= VerifyBlueprintGetFunc(BlueprintGetFunc, Property, Class, Module);
			}
		}
	}

	return bPassedVerification;
}

bool FAngelscriptManager::VerifyRepFunc(FString* FuncName, const TSharedRef<FAngelscriptPropertyDesc>& Property,
	const TSharedRef<FAngelscriptClassDesc>& Class, const TSharedRef<FAngelscriptModuleDesc>& Module)
{
	if (FuncName != nullptr)
	{
		auto FuncDesc = Class->GetMethod(*FuncName);
		// First make sure we can find the method
		if (!FuncDesc.IsValid())
		{
			ScriptCompileError(Module, Property->LineNumber,
				FString::Printf(TEXT("The function '%s' which is specified for %s on property %s::%s "
									 "could not be found within the script class. (It also has to be UFUNCTION())"),
					**FuncName,
					*Property->PropertyName,
					*Class->ClassName,
					*Property->PropertyName
				)
			);
			return false;
		}

		// Having an ReplicatedUsing callback with 0 arguments is OK, we only need to verify the argument if we actually
		// have one!
		if (FuncDesc->Arguments.Num() > 0)
		{
			if (FuncDesc->Arguments.Num() > 1)
			{
				ScriptCompileError(Module, FuncDesc->LineNumber,
					FString::Printf(TEXT("The function '%s' which is specified for ReplicatedUsing on property %s::%s "
										 "can not have more than 1 argument."),
						**FuncName,
						*Class->ClassName,
						*Property->PropertyName
					)
				);
				return false;
			}

			const FAngelscriptTypeUsage& FuncArgType = FuncDesc->Arguments[0].Type;
			const FAngelscriptTypeUsage& PropType = Property->PropertyType;

			// The type of the argument in the function has to be the same as the type of the variable we're
			// replicating!
			if (!FuncArgType.EqualsUnqualified(PropType))
			{
				ScriptCompileError(Module, FuncDesc->LineNumber,
					FString::Printf(TEXT("The function '%s' which is specified for ReplicatedUsing on property %s::%s "
										 "takes an argument of type '%s', but the value replicated is of type '%s'."),
						**FuncName,
						*Class->ClassName,
						*Property->PropertyName,
						*FuncArgType.GetFriendlyTypeName(),
						*PropType.GetFriendlyTypeName()
					)
				);
				return false;
			}
		}
	}
	return true;
}

bool FAngelscriptManager::VerifyBlueprintSetFunc(FString* FuncName,
	const TSharedRef<FAngelscriptPropertyDesc>& Property, const TSharedRef<FAngelscriptClassDesc>& Class,
	const TSharedRef<FAngelscriptModuleDesc>& Module)
{
	if (FuncName != nullptr)
	{
		auto FuncDesc = Class->GetMethod(*FuncName);
		// First make sure we can find the method
		if (!FuncDesc.IsValid())
		{
			ScriptCompileError(Module, Property->LineNumber,
				FString::Printf(TEXT("The function '%s' which is specified for BlueprintSetter on property %s::%s "
									 "could not be found within the script class. (It also has to be UFUNCTION())"),
					**FuncName,
					*Class->ClassName,
					*Property->PropertyName
				)
			);
			return false;
		}

		// Having an BlueprintSetter callback requires a func with one argument matching the property type
		if (FuncDesc->Arguments.Num() == 1)
		{
			const FAngelscriptTypeUsage& FuncArgType = FuncDesc->Arguments[0].Type;
			const FAngelscriptTypeUsage& PropType = Property->PropertyType;

			// The type of the argument in the function has to be the same as the type of the variable we're
			// replicating!
			if (!FuncArgType.EqualsUnqualified(PropType))
			{
				ScriptCompileError(Module, FuncDesc->LineNumber,
					FString::Printf(TEXT("The function '%s' which is specified for BlueprintSetter on property %s::%s "
										 "takes an argument of type '%s', but the value written is of type '%s'."),
						**FuncName,
						*Class->ClassName,
						*Property->PropertyName,
						*FuncArgType.GetFriendlyTypeName(),
						*PropType.GetFriendlyTypeName()
					)
				);
				return false;
			}
		}
		else
		{
			ScriptCompileError(Module, Property->LineNumber,
				FString::Printf(TEXT("The function '%s' which is specified for BlueprintSetter on property %s::%s "
									 "should take exactly 1 argument."),
					**FuncName,
					*Class->ClassName,
					*Property->PropertyName
				)
			);
			return false;
		}
	}
	return true;
}

bool FAngelscriptManager::VerifyBlueprintGetFunc(FString* FuncName,
	const TSharedRef<FAngelscriptPropertyDesc>& Property, const TSharedRef<FAngelscriptClassDesc>& Class,
	const TSharedRef<FAngelscriptModuleDesc>& Module)
{
	if (FuncName != nullptr)
	{
		auto FuncDesc = Class->GetMethod(*FuncName);
		// First make sure we can find the method
		if (!FuncDesc.IsValid())
		{
			ScriptCompileError(Module, Property->LineNumber,
				FString::Printf(TEXT("The function '%s' which is specified for BlueprintGetter on property %s::%s "
									 "could not be found within the script class. (It also has to be UFUNCTION())"),
					**FuncName,
					*Class->ClassName,
					*Property->PropertyName
				)
			);
			return false;
		}

		// BlueprintGetters need to be BlueprintPure
		if (!FuncDesc->bBlueprintPure)
		{
			ScriptCompileError(Module, Property->LineNumber,
				FString::Printf(TEXT("The function '%s' which is specified for BlueprintGetter on property %s::%s "
									 "needs to be marked as BlueprintPure."),
					**FuncName,
					*Class->ClassName,
					*Property->PropertyName
				)
			);
			return false;
		}

		// Having an BlueprintGetter callback requires a function with 0 arguments
		if (FuncDesc->Arguments.Num() == 0)
		{
			const FAngelscriptTypeUsage& FuncRetType = FuncDesc->ReturnType;
			const FAngelscriptTypeUsage& PropType = Property->PropertyType;

			// The type of the argument in the function has to be the same as the type of the variable we're
			// replicating!
			if (!FuncRetType.EqualsUnqualified(PropType))
			{
				FString FriendlyTypeName = FuncRetType.Type == NULL ? FString("void") : *FuncRetType.GetFriendlyTypeName();
				ScriptCompileError(Module, FuncDesc->LineNumber,
					FString::Printf(TEXT("The function '%s' which is specified for BlueprintGetter on property %s::%s "
										 "returns type '%s', but the value read is of type '%s'."),
						**FuncName,
						*Class->ClassName,
						*Property->PropertyName,
						*FriendlyTypeName,
						*PropType.GetFriendlyTypeName()
					)
				);
				return false;
			}
		}
		else
		{
			ScriptCompileError(Module, Property->LineNumber,
				FString::Printf(TEXT("The function '%s' which is specified for BlueprintGetter on property %s::%s "
									 "should not take any arguments."),
					**FuncName,
					*Class->ClassName,
					*Property->PropertyName
				)
			);
			return false;
		}
	}
	return true;
}

void FAngelscriptManager::CheckForHotReload(ECompileType CompileType)
{
	// If we used precompiled data we can't hot reload at all
	if (bUsedPrecompiledDataForPreprocessor)
		return;

	if (bUseHotReloadCheckerThread)
	{
		// Still waiting for hot reload results to come back,
		// so don't do anything for now.
		if (bWaitingForHotReloadResults)
			return;
	}

	// Check if anything is queued for hot reload
	TArray<FFilenamePair> FileList;

	FileList.Append(FileChangesDetectedForReload);
	FileChangesDetectedForReload.Empty();

	// If any files were deleted, this should also cause a hotreload
	// We delay hotreloads from deletions slightly so if this was a rename instead of a delete we will see the new file right away
	if (FileList.Num() != 0 || FPlatformTime::Seconds() - LastFileChangeDetectedTime > 0.2)
	{
		for (const auto& DeletedFile : FileDeletionsDetectedForReload)
			FileList.AddUnique(DeletedFile);
		FileDeletionsDetectedForReload.Empty();
	}

	if (CompileType != ECompileType::SoftReloadOnly)
	{
		for (const auto& QueuedFile : QueuedFullReloadFiles)
			FileList.AddUnique(QueuedFile);
		QueuedFullReloadFiles.Empty();
	}

	if (FileList.Num() != 0)
	{
		// Background task gave us stuff to reload, so do it now
		PerformHotReload(CompileType, FileList);
	}

	// Kick off a new check cycle if we are using the checker thread
	if (bUseHotReloadCheckerThread)
	{
		// Spawn new background task to check for reloads
		bWaitingForHotReloadResults = true;
	}
}

static bool HasGameWorld()
{
	for(const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		// Context.World() can be null when running with auto login in PIE
		if(Context.World() != nullptr && Context.World()->IsGameWorld())
		{
			return true;
		}
	}
	return false;
}

void FAngelscriptManager::Tick(float DeltaTime)
{
#if AS_CAN_HOTRELOAD
	if (bScriptDevelopmentMode)
	{
		// In the scenario where the initial compile fails and the user press "Try Again" GEngine is nullptr.
		// Since the unit tests assumes an existing GEngine we need to skip the unit testing in that case.
		if (GEngine != nullptr && HotReloadTestRunner != nullptr)
		{
			bool AllUnitTestsPass = HotReloadTestRunner->RunTests(this);
			if (!AllUnitTestsPass)
			{
				EmitDiagnostics();
			}
		}

		// Only check for hot reloads periodically instead of every tick
		if (bUseHotReloadCheckerThread)
		{
			double CurrentTime = FPlatformTime::Seconds();
			if (NextHotReloadCheck > CurrentTime && !bWaitingForHotReloadResults)
				return;
			NextHotReloadCheck = CurrentTime + 0.1;
		}

		// If we're in PIE or cooked, only soft reloads are allowed,
		// otherwise we can do a full reload and reinstantiate all
		// editor objects using unreal's hot reload mechanisms.
		if (!GIsEditor || HasGameWorld())
		{
			CheckForHotReload(ECompileType::SoftReloadOnly);
		}
		else
		{
			CheckForHotReload(ECompileType::FullReload);
		}
	}
#endif

#if WITH_AS_DEBUGSERVER
	if(DebugServer != nullptr)
		DebugServer->Tick();
#endif

	// If this is ever not null during this tick, something changed
	// the world context without resetting it. Very bad
	UE_CLOG(CurrentWorldContext != nullptr, Angelscript, Fatal, TEXT("Angelscript world context was improperly restored after use!"));
}

TStatId FAngelscriptManager::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FAngelscriptManager, STATGROUP_Tickables);
}

void FAngelscriptManager::CheckForFileChanges()
{
	ensure(bUseHotReloadCheckerThread);

#if AS_PRINT_STATS
	double StartCompute = FPlatformTime::Seconds();
#endif

	// Clear any previous data we had
	FileChangesDetectedForReload.Empty();

	// Check all files in script directory for hot reload need
	auto& FileManager = IFileManager::Get();

	TArray<FFilenamePair> Filenames;
	FindAllScriptFilenames(Filenames);

	for (FFilenamePair& Filename : Filenames)
	{
		FDateTime FileTime = FileManager.GetTimeStamp(*Filename.AbsolutePath);

		FHotReloadState* FileState = FileHotReloadState.Find(Filename.RelativePath);
		if (FileState == nullptr)
		{
			// File didn't exist before, so definitely hot reload it
			FileChangesDetectedForReload.Add(Filename);

			FHotReloadState NewState;
			NewState.LastChange = FileTime;
			FileHotReloadState.Add(Filename.RelativePath, NewState);
		}
		else if (FileTime != FileState->LastChange)
		{
			// File on disk is newer, queue reload
			FileChangesDetectedForReload.Add(Filename);
			FileState->LastChange = FileTime;
		}
	}

#if AS_PRINT_STATS
	double EndCompute = FPlatformTime::Seconds();
	if (FileChangesDetectedForReload.Num() != 0)
	{
		UE_LOG(Angelscript, Log, TEXT("scanning for changed files took %.3f ms"), (EndCompute - StartCompute) * 1000);
	}
#endif
}

void FAngelscriptManager::SwapInModules(const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& Modules, TArray<TSharedRef<FAngelscriptModuleDesc>>& DiscardedModules)
{
	FAngelscriptScopeTimer PostTimer(TEXT("new module swap-in"));

	for (auto Module : Modules)
	{
		// Mark old modules as outdated
		auto* OldModule = ActiveModules.Find(Module->ModuleName);
		if (OldModule != nullptr)
		{
			DiscardedModules.Add(*OldModule);

			// Need to rename the angelscript module to something so
			// we don't look it up later.
			FString TrashName = FString::Printf(TEXT("%s_OLD_%d"), *Module->ModuleName, TempNameIndex);
			TempNameIndex += 1;

			if ((*OldModule)->ScriptModule)
			{
				SetOutdated((*OldModule)->ScriptModule);
				(*OldModule)->ScriptModule->SetName(TCHAR_TO_ANSI(*TrashName));
			}
		}

		if (Module->ScriptModule != nullptr)
		{
			// Rename the new module to the right name
			Module->ScriptModule->SetName(TCHAR_TO_ANSI(*Module->ModuleName));
		}

		ActiveModules.Add(Module->ModuleName, Module);
	}

	// Update dependencies for discarded modules
	for (auto OldModule : DiscardedModules)
	{
		if (OldModule->ScriptModule)
			ModulesByScriptModule.Remove(OldModule->ScriptModule);
	}

#if AS_CAN_HOTRELOAD
	// Rebuild the full list of all active types
	ActiveClassesByName.Reset();
	ActiveDelegatesByName.Reset();
	ActiveEnumsByName.Reset();

	for (auto ModuleElem : ActiveModules)
	{
		auto Module = ModuleElem.Value;
		for (auto Class : Module->Classes)
		{
			ActiveClassesByName.Add(Class->ClassName,
				TPair<TSharedPtr<FAngelscriptModuleDesc>,TSharedPtr<FAngelscriptClassDesc>>{
					Module, Class
			});
		}

		for (auto Delegate : Module->Delegates)
		{
			ActiveDelegatesByName.Add(Delegate->DelegateName,
				TPair<TSharedPtr<FAngelscriptModuleDesc>,TSharedPtr<FAngelscriptDelegateDesc>>{
					Module, Delegate
			});
		}

		for (auto Enum : Module->Enums)
		{
			ActiveEnumsByName.Add(Enum->EnumName,
				TPair<TSharedPtr<FAngelscriptModuleDesc>,TSharedPtr<FAngelscriptEnumDesc>>{
					Module, Enum
			});
		}
	}

#endif
}

#if !UE_BUILD_SHIPPING
void FAngelscriptManager::GetOnScreenMessages(TMultiMap<FCoreDelegates::EOnScreenMessageSeverity, FText>& OutMessages)
{
#if AS_CAN_HOTRELOAD
	// This shouldn't be translated so... :) FromString it is.
	const static FText CompileErrorText = FText::FromString(TEXT("ANGELSCRIPT HOT-RELOAD FAILED -- KEEPING OLD CODE"));

	// If the previous hot-reload failed, display a useful message bejbi
	if (PreviouslyFailedReloadFiles.Num())
		OutMessages.Add(FCoreDelegates::EOnScreenMessageSeverity::Error, CompileErrorText);
#endif
}
#endif

TSharedPtr<struct FAngelscriptModuleDesc> FAngelscriptManager::GetModule(asIScriptModule* Module)
{
	TSharedPtr<struct FAngelscriptModuleDesc>* FoundModule = ModulesByScriptModule.Find(Module);
	if (FoundModule == nullptr)
		return nullptr;
	return *FoundModule;
}

TSharedPtr<FAngelscriptModuleDesc> FAngelscriptManager::GetModuleByModuleName(const FString& ModuleName)
{
	auto ModulePtr = GetModule(ModuleName);
	if (ModulePtr.IsValid())
	{
		return ModulePtr;
	}

	return TSharedPtr<struct FAngelscriptModuleDesc>();
}

TSharedPtr<struct FAngelscriptModuleDesc> FAngelscriptManager::GetModuleByFilenameOrModuleName(const FString& Filename, const FString& ModuleName)
{
	auto Module = GetModuleByFilename(Filename);
	if (Module.IsValid())
	{
		return Module;
	}

	return GetModuleByModuleName(ModuleName);
}

TSharedPtr<struct FAngelscriptModuleDesc> FAngelscriptManager::GetModuleByFilename(const FString& Filename)
{
	for (auto RootDir : AllRootPaths)
	{
		RootDir += TEXT("/"); // Needed for MakePathRelativeTo to work

		FString ModuleName = Filename;
		MakePathRelativeTo_IgnoreCase(ModuleName, *RootDir);
		ModuleName = ModuleName.Replace(TEXT(".as"), TEXT("")).Replace(TEXT("/"), TEXT("."));

		auto ModulePtr = GetModule(ModuleName);
		if (ModulePtr.IsValid())
		{
			return ModulePtr;
		}
	}

	return TSharedPtr<struct FAngelscriptModuleDesc>();
}

ECompileResult FAngelscriptManager::CompileModules(ECompileType CompileType, const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& InModules, TArray<TSharedRef<FAngelscriptModuleDesc>>& OutCompiledModules)
{
	// We allocate from the memstack in the script compiler, so use a MemMark to deallocate everything at the end
	FMemMark MemoryMark(FMemStack::Get());

	FAngelscriptCompilationDelegate& PreCompileDelegate = FAngelscriptCodeModule::GetPreCompile();
	if (PreCompileDelegate.IsBound())
		PreCompileDelegate.Broadcast();

	// Create progress indicator
	FScopedSlowTask SlowTask(3.f, FText::FromString(TEXT("Script Module Compilation")));
	if (CompileType == ECompileType::FullReload && bIsInitialCompileFinished)
		SlowTask.MakeDialogDelayed(0.5f);

	bool bWasFullyHandled = true;
	bool bHadCompileErrors = false;

	TMap<FString, TSharedRef<struct FAngelscriptModuleDesc>> CompilingModulesByName;
	TMap<FString, TSharedRef<struct FAngelscriptClassDesc>> CompilingClassesByName;

	TArray<TSharedRef<FAngelscriptModuleDesc>> CompiledModules;
	TSet<TSharedRef<FAngelscriptModuleDesc>> ModulesToUpdateReferences;
	asModuleReferenceUpdateMap ScriptUpdateMap;

	const bool bUseRecompileAvoidance = (GAngelscriptRecompileAvoidance != 0)
		&& bIsInitialCompileFinished && FAngelscriptManager::bUseAutomaticImportMethod;

	auto* ScriptEngine = (asCScriptEngine*)Engine;

	ScriptEngine->deferValidationOfTemplateTypes = true;
	ScriptEngine->deferCalculatingTemplateSize = true;
	ScriptEngine->RequestBuild();
	ScriptEngine->PrepareEngine();

	// Always compile every module in the list
	{
		FAngelscriptScopeTimer Timer(TEXT("script compilation total"));
		int32 ProgressUpdateCounter = 0;
		int32 ProgressUpdatesDone = 0;

		TArray<TSharedRef<FAngelscriptModuleDesc>> CompilationQueue;
		bool bRecompiledAnyDependencies = false;

		// Queue up all changed modules for compilation
		CompilationQueue.Append(InModules);

		// Reset diagnostics for each module
		for (auto& Elem : Diagnostics)
			Elem.Value.bIsCompiling = false;

		// Recursively resolve all dependencies
		{
			FAngelscriptScopeTimer StageTimer(TEXT("script compilation stage1 and stage2"));
			while (CompilationQueue.Num() != 0)
			{
				TArray<TSharedRef<FAngelscriptModuleDesc>> CurrentCompileList = MoveTemp(CompilationQueue);
				CompilationQueue.Reset();

				// Initial setup for compilation
				for (int i = 0, Count = CurrentCompileList.Num(); i < Count; ++i)
				{
					auto Module = CurrentCompileList[i];

					// Setup diagnostic capture
					for (auto Section : Module->Code)
					{
						auto& Diag = Diagnostics.FindOrAdd(Section.AbsoluteFilename);
						Diag.Diagnostics.Reset();
						Diag.Filename = Section.AbsoluteFilename;
						Diag.bIsCompiling = true;
					}

					// Add it to a lookup table so imports can do fast lookups later
					ensureMsgf(!CompilingModulesByName.Contains(Module->ModuleName), TEXT("Duplicate module %s"), *Module->ModuleName);
					CompilingModulesByName.Add(Module->ModuleName, Module);
					CompiledModules.Add(Module);

					// Remove the old module so the script engine can no longer see it
					if (CompileType != ECompileType::Initial)
					{
						auto* OldModule = ActiveModules.Find(Module->ModuleName);
						if (OldModule != nullptr)
						{
							auto* OldScriptModule = (asCModule*)(*OldModule)->ScriptModule;
							if (OldScriptModule != nullptr)
								OldScriptModule->RemoveTypesAndGlobalsFromEngineAvailability();
						}
					}

					// Mark which classes we're compiling
					for (auto Class : Module->Classes)
						CompilingClassesByName.Add(Class->ClassName, Class);
				}


				// Stage 1
				for (int i = 0, Count = CurrentCompileList.Num(); i < Count; ++i)
				{
					auto Module = CurrentCompileList[i];

					// Update progress indicator
					if (--ProgressUpdateCounter <= 0 && ProgressUpdatesDone < 10)
					{
						SlowTask.EnterProgressFrame(0.025f);
						ProgressUpdateCounter = FMath::Max(CurrentCompileList.Num() / 10, 10);
						ProgressUpdatesDone += 1;
					}

					// Find all modules, either old or currently compiling, that we should import.
					bool bImportErrors = false;
					TArray<TSharedRef<FAngelscriptModuleDesc>> ImportedModules;

					if (!FAngelscriptManager::bUseAutomaticImportMethod)
					{
						for (FString& ImportName : Module->ImportedModules)
						{
							TSharedPtr<FAngelscriptModuleDesc> FoundModule;
							auto* CompilingModule = CompilingModulesByName.Find(ImportName);
							if (CompilingModule != nullptr)
								FoundModule = *CompilingModule;
							if (!FoundModule.IsValid())
							{
								auto* ExistingModule = ActiveModules.Find(ImportName);
								if (ExistingModule != nullptr)
									FoundModule = *ExistingModule;
							}

							if (FoundModule.IsValid())
							{
								ImportedModules.Add(FoundModule.ToSharedRef());
							}
							else
							{
								ScriptCompileError(Module, 1, FString::Printf(
									TEXT("Could not compile module %s: could not find module %s to import."),
									*Module->ModuleName, *ImportName));
								bImportErrors = true;
							}
						}
					}

					if (bImportErrors)
					{
						// Don't even try to compile if we couldn't import everything
						Module->bCompileError = true;
						bHadCompileErrors = true;
					}
					else
					{
						CompileModule_Types_Stage1(CompileType, Module, ImportedModules);
					}
				}

				// In parallel, parse the script code
				int ModulesPerTask = 100;
				int TaskCount = 1 + CurrentCompileList.Num() / ModulesPerTask;
				ParallelFor(TaskCount, [&](int TaskIndex)
				{
					int StartIndex = (TaskIndex * ModulesPerTask);
					int EndIndex = FMath::Min(StartIndex + ModulesPerTask, CurrentCompileList.Num());
					for (int i = StartIndex; i < EndIndex; ++i)
					{
						auto Module = CurrentCompileList[i];
						asCModule* ScriptModule = Module->ScriptModule;
						if (ScriptModule == nullptr)
							continue;
						if (Module->bLoadedPrecompiledCode)
							continue;
						if (Module->bCompileError)
							continue;

						// Add types from the module into the engine
						auto Result = ScriptModule->builder->BuildParallelParseScripts();
						if (Result != asSUCCESS)
						{
							Module->bCompileError = true;
							bHadCompileErrors = true;
						}
					}
				});

				// Now that everything is parsed, generate the actual types
				for (auto Module : CompiledModules)
				{
					asCModule* ScriptModule = Module->ScriptModule;
					if (ScriptModule == nullptr)
						continue;
					if (Module->bLoadedPrecompiledCode)
						continue;
					if (Module->bCompileError)
						continue;

					// Add types from the module into the engine
					auto Result = ScriptModule->builder->BuildGenerateTypes();
					if (Result != asSUCCESS)
					{
						Module->bCompileError = true;
						bHadCompileErrors = true;
					}
				}

				// If parsing failed on one of our modules during a hotreload,
				// we want to ignore any non-parsing-related errors afterward.
				// This is so we don't spam errors in dependencies.
				if (bHadCompileErrors && CompileType != ECompileType::Initial)
					bIgnoreCompileErrorDiagnostics = true;

				// Stage 2
				for (int i = 0, Count = CurrentCompileList.Num(); i < Count; ++i)
				{
					auto Module = CurrentCompileList[i];

					// Update progress indicator
					if (--ProgressUpdateCounter <= 0 && ProgressUpdatesDone < 10)
					{
						SlowTask.EnterProgressFrame(0.025f);
						ProgressUpdateCounter = FMath::Max(CurrentCompileList.Num() / 10, 10);
						ProgressUpdatesDone += 1;
					}

					// Perform stage 2 of compilation
					CompileModule_Functions_Stage2(CompileType, Module);

					// Cancel out on a compile error if we're
					// doing a hot-reload compile.
					if (Module->bCompileError)
						bHadCompileErrors = true;
				}

				if (bUseRecompileAvoidance)
				{
					// Collect which types are updated to which for each module we compiled
					for (int i = 0, Count = CurrentCompileList.Num(); i < Count; ++i)
					{
						auto Module = CurrentCompileList[i];
						
						asCModule* ScriptModule = Module->ScriptModule;
						if (ScriptModule == nullptr)
							continue;

						// Link up the old and new script modules
						auto OldModule = ActiveModules.Find(Module->ModuleName);
						if (OldModule != nullptr)
						{
							auto* OldScriptModule = (*OldModule)->ScriptModule;
							check(OldScriptModule != nullptr);

							ScriptModule->CollectUpdatedTypeReferences(
								OldScriptModule,
								OUT ScriptUpdateMap);

							ScriptModule->ReloadOldModule = OldScriptModule;
							OldScriptModule->ReloadNewModule = ScriptModule;
						}
					}

					// Each module we just compiled should check whether there were structural changes
					for (int i = 0, Count = CurrentCompileList.Num(); i < Count; ++i)
					{
						auto Module = CurrentCompileList[i];
						
						asCModule* ScriptModule = Module->ScriptModule;
						if (ScriptModule == nullptr)
							continue;

						bool bHasStructuralChanges = false;

						// Link up the old and new script modules
						if (ScriptModule->ReloadOldModule != nullptr)
						{
							ScriptModule->DiffForReferenceUpdate(
								ScriptModule->ReloadOldModule,
								OUT ScriptUpdateMap,
								OUT bHasStructuralChanges);
						}
						else
						{
							bHasStructuralChanges = true;
						}

						if (bHasStructuralChanges)
							ScriptModule->ReloadState = asCModule::EReloadState::RecompiledWithStructuralChanges;
						else
							ScriptModule->ReloadState = asCModule::EReloadState::RecompiledOnlyCodeChanges;

						// Link up the old and new script modules
						if (ScriptModule->ReloadOldModule != nullptr)
							ScriptModule->ReloadOldModule->ReloadState = ScriptModule->ReloadState;
					}

					// Go through all existing modules that we aren't already recompiling, and mark them
					// appropriately based on what the dependencies are doing.
					bool bMarkedNewStructuralChanges = true;
					while (bMarkedNewStructuralChanges)
					{
						bMarkedNewStructuralChanges = false;

						for (auto ModuleElem : ActiveModules)
						{
							auto OldModule = ModuleElem.Value;
							asCModule* OldScriptModule = OldModule->ScriptModule;
							if (OldScriptModule == nullptr)
								continue;
							if (OldScriptModule->ReloadState == asCModule::EReloadState::RecompiledWithStructuralChanges)
								continue;
							if (OldScriptModule->ReloadState == asCModule::EReloadState::QueuedForCompilation)
								continue;

							// Code changes only compiles can get upgraded to structural compiles if any of our structural dependencies changed
							if (OldScriptModule->ReloadState == asCModule::EReloadState::RecompiledOnlyCodeChanges)
							{
								for (const auto& DependencyElem : OldScriptModule->moduleDependencies)
								{
									if (DependencyElem.Value.bIsStructuralDependency)
									{
										if (DependencyElem.Key->ReloadState == asCModule::EReloadState::RecompiledWithStructuralChanges)
										{
											// One of our structural dependencies ended up with a structural change, so we
											// definitely need to have a structural change on our end as well!
											OldScriptModule->ReloadState = asCModule::EReloadState::RecompiledWithStructuralChanges;
											OldScriptModule->ReloadNewModule->ReloadState = asCModule::EReloadState::RecompiledWithStructuralChanges;

											// We need to re-check all moduls after upgrading something to a structural change
											bMarkedNewStructuralChanges = true;

											break;
										}
									}
								}

								continue;
							}

							// If we haven't decided to compile it yet, we might choose to do so now
							check(OldScriptModule->ReloadState == asCModule::EReloadState::None 
								|| OldScriptModule->ReloadState == asCModule::EReloadState::UpdateReferences);

							bool bWantUpdateReferences = false;
							bool bTriggeredCompile = false;

							for (const auto& DependencyElem : OldScriptModule->moduleDependencies)
							{
								if (DependencyElem.Key->ReloadState == asCModule::EReloadState::RecompiledOnlyCodeChanges)
								{
									// If this is a hard value dependency we need to recompile anyway, even if it was only a code change
									if (DependencyElem.Value.bIsHardValueDependency || DependencyElem.Value.bIsStructuralDependency)
									{
										bTriggeredCompile = true;
										break;
									}
									else
									{
										// Otherwise we only update references and don't recompile
										bWantUpdateReferences = true;
									}
								}
								else if (DependencyElem.Key->ReloadState == asCModule::EReloadState::RecompiledWithStructuralChanges)
								{
									bTriggeredCompile = true;
									break;
								}
							}

							if (bTriggeredCompile)
							{
								// We no longer want to update references in the old module
								ModulesToUpdateReferences.Remove(OldModule);

								// Create a copy of the old module's preprocessor data so we can re-compile it
								TSharedRef<FAngelscriptModuleDesc> NewModule = MakeShared<FAngelscriptModuleDesc>(*OldModule);
								NewModule->ScriptModule = nullptr;
								NewModule->PrecompiledData = nullptr;
								NewModule->bCompileError = false;
								NewModule->bLoadedPrecompiledCode = false;
								NewModule->UnitTestFunctions.Empty();
								NewModule->IntegrationTestFunctions.Empty();

								NewModule->Classes.Reset();
								for (int ClassIndex = 0, ClassCount = OldModule->Classes.Num(); ClassIndex < ClassCount; ++ClassIndex)
								{
									TSharedRef<FAngelscriptClassDesc> OldClass = OldModule->Classes[ClassIndex];
									TSharedRef<FAngelscriptClassDesc> NewClass = MakeShared<FAngelscriptClassDesc>(*OldClass);
									NewClass->ScriptType = nullptr;
									NewClass->Class = nullptr;
									NewClass->Struct = nullptr;

									// This can have changed! Make sure we look this up again using the descriptors we are currently recompiling!
									// We need to check the whole inheritance tree, not just one step, in case we haven't propagated stuff yet!
									TSharedPtr<FAngelscriptClassDesc> Supermost = NewClass;
									while (!Supermost->bSuperIsCodeClass)
									{
										TSharedPtr<FAngelscriptClassDesc> CheckSuper;
										auto* CompilingSuper = CompilingClassesByName.Find(Supermost->SuperClass);
										if (CompilingSuper != nullptr)
											CheckSuper = *CompilingSuper;

										if (!CheckSuper.IsValid())
											CheckSuper = FAngelscriptManager::Get().GetClass(Supermost->SuperClass);
										if (!CheckSuper.IsValid())
											break;

										Supermost = CheckSuper;
									}
									NewClass->CodeSuperClass = Supermost->CodeSuperClass;

									NewClass->Properties.Reset();
									for (int PropertyIndex = 0, PropertyCount = OldClass->Properties.Num(); PropertyIndex < PropertyCount; ++PropertyIndex)
									{
										TSharedRef<FAngelscriptPropertyDesc> OldProperty = OldClass->Properties[PropertyIndex];
										TSharedRef<FAngelscriptPropertyDesc> NewProperty = MakeShared<FAngelscriptPropertyDesc>(*OldProperty);
										NewProperty->ScriptPropertyIndex = -1;
										NewProperty->ScriptPropertyOffset = 0;
										NewProperty->bIsPrivate = false;
										NewProperty->bIsProtected = false;
										NewProperty->PropertyType = FAngelscriptTypeUsage();

										NewClass->Properties.Add(NewProperty);
									}

									NewClass->Methods.Reset();
									for (int MethodIndex = 0, MethodCount = OldClass->Methods.Num(); MethodIndex < MethodCount; ++MethodIndex)
									{
										TSharedRef<FAngelscriptFunctionDesc> OldMethod = OldClass->Methods[MethodIndex];
										TSharedRef<FAngelscriptFunctionDesc> NewMethod = MakeShared<FAngelscriptFunctionDesc>(*OldMethod);
										NewMethod->ScriptFunction = nullptr;
										NewMethod->bIsNoOp = false;
										NewMethod->bIsConstMethod = false;
										if (!NewMethod->OriginalFunctionName.IsEmpty())
										{
											NewMethod->FunctionName = MoveTemp(NewMethod->OriginalFunctionName);
											NewMethod->OriginalFunctionName.Reset();
										}
										NewMethod->bIsPrivate = false;
										NewMethod->bIsProtected = false;
										NewMethod->Arguments.Reset();
										NewMethod->ReturnType = FAngelscriptTypeUsage();

										NewClass->Methods.Add(NewMethod);
									}

									NewModule->Classes.Add(NewClass);
								}

								NewModule->Enums.Reset();
								for (int EnumIndex = 0, EnumCount = OldModule->Enums.Num(); EnumIndex < EnumCount; ++EnumIndex)
								{
									TSharedRef<FAngelscriptEnumDesc> OldEnum = OldModule->Enums[EnumIndex];
									TSharedRef<FAngelscriptEnumDesc> NewEnum = MakeShared<FAngelscriptEnumDesc>(*OldEnum);
									NewEnum->Enum = nullptr;
									NewEnum->ScriptType = nullptr;
									NewEnum->ValueNames.Reset();
									NewEnum->EnumValues.Reset();

									NewModule->Enums.Add(NewEnum);
								}

								NewModule->Delegates.Reset();
								for (int DelegateIndex = 0, DelegateCount = OldModule->Delegates.Num(); DelegateIndex < DelegateCount; ++DelegateIndex)
								{
									TSharedRef<FAngelscriptDelegateDesc> OldDelegate = OldModule->Delegates[DelegateIndex];
									TSharedRef<FAngelscriptDelegateDesc> NewDelegate = MakeShared<FAngelscriptDelegateDesc>(*OldDelegate);
									NewDelegate->ScriptType = nullptr;
									NewDelegate->Signature = nullptr;
									NewDelegate->Function = nullptr;

									NewModule->Delegates.Add(NewDelegate);
								}

								bRecompiledAnyDependencies = true;
								CompilationQueue.Add(NewModule);

								OldScriptModule->ReloadState = asCModule::EReloadState::QueuedForCompilation;
							}
							else if (bWantUpdateReferences)
							{
								// Add the module to have its references updated. We don't need to recompile at this point.
								ModulesToUpdateReferences.Add(OldModule);
								OldScriptModule->ReloadState = asCModule::EReloadState::UpdateReferences;
							}
						}
					}
				}
			}
		}

		// If any dependencies were compiled _later_ than the original batch, we need to do type reference replacement
		// The modules that were compiled first could be referencing the *old* versions of the types declared in
		// the modules that were compiled later, so we just do a big pass on all the type data.
		if (bRecompiledAnyDependencies || ModulesToUpdateReferences.Num() != 0)
		{
			FAngelscriptScopeTimer StageTimer(TEXT("script reload reference replacement"));

			// For each class that was recompiled, we need to check which template instances it had,
			// and then add all the functions and properties to the replacement list
			struct FTemplateReplacementHelper
			{
				static asCTypeInfo* CreateReplacementTemplateType(asCScriptEngine* Engine, asModuleReferenceUpdateMap& ScriptUpdateMap, asCObjectType* OldInstance)
				{
					asCTypeInfo* ReplacedInstance = ScriptUpdateMap.Types.FindRef(OldInstance);
					if (ReplacedInstance != nullptr)
					{
						check(ReplacedInstance->module != nullptr && ReplacedInstance->module->ReloadNewModule == nullptr);
						return ReplacedInstance;
					}

					asCArray<asCDataType> subTypes = OldInstance->templateSubTypes;

					bool bRequiresReplacement = false;
					for (int i = 0, Count = subTypes.GetLength(); i < Count; ++i)
					{
						if (auto* TypeInfo = (asCTypeInfo*)subTypes[i].GetTypeInfo())
						{
							if (auto* Replacement = ScriptUpdateMap.Types.FindRef(TypeInfo))
							{
								subTypes[i].SetTypeInfo(Replacement);
								bRequiresReplacement = true;

								check(Replacement->module != nullptr && Replacement->module->ReloadNewModule == nullptr);
							}
							else if (TypeInfo->flags & asOBJ_TEMPLATE)
							{
								// Need to recursively create subtypes that are also templates, because we
								// might not actually have gotten to this type yet.
								asCTypeInfo* NewSubTemplateInstance = CreateReplacementTemplateType(
									Engine, ScriptUpdateMap, (asCObjectType*)TypeInfo
								);

								if (NewSubTemplateInstance != nullptr)
								{
									bRequiresReplacement = true;
									subTypes[i].SetTypeInfo(NewSubTemplateInstance);

									check(NewSubTemplateInstance->module != nullptr && NewSubTemplateInstance->module->ReloadNewModule == nullptr);
								}
							}
							else
							{
								if (TypeInfo->module != nullptr && TypeInfo->module->ReloadNewModule != nullptr)
								{
									// This is an old type that no longer exists in the new module (compile errors?)
									// We should avoid replacement on this type entirely.
									return nullptr;
								}
							}
						}
					}

					if (!bRequiresReplacement)
						return nullptr;

					asCObjectType* NewInstance = Engine->GetTemplateInstanceType(OldInstance->templateBaseType, subTypes, nullptr);

					// Add the template type and all its functions and properties to the replacement map
					ScriptUpdateMap.Types.Add(OldInstance, NewInstance);

					// Keep the old instance alive, because we might want to replace it back if there's a compile error later
					OldInstance->AddRefInternal();
					ScriptUpdateMap.TemplateInstances.Add(OldInstance, NewInstance);

					check(OldInstance->properties.GetLength() == NewInstance->properties.GetLength());
					for (int i = 0, Count = OldInstance->properties.GetLength(); i < Count; ++i)
						ScriptUpdateMap.ObjectProperties.Add(OldInstance->properties[i], NewInstance->properties[i]);

					check(OldInstance->methods.GetLength() == NewInstance->methods.GetLength());
					for (int i = 0, Count = OldInstance->methods.GetLength(); i < Count; ++i)
					{
						asCScriptFunction* OldFunction = Engine->scriptFunctions[OldInstance->methods[i]];
						if (!OldFunction->traits.GetTrait(asTRAIT_GENERIC_TEMPLATE_FUNCTION))
						{
							asCScriptFunction* NewFunction = Engine->GenerateTemplateFunction(NewInstance, Engine->scriptFunctions[NewInstance->methods[i]]);
							ScriptUpdateMap.Functions.Add(OldFunction, NewFunction);
						}
					}

					check(OldInstance->beh.constructors.GetLength() == NewInstance->beh.constructors.GetLength());
					for (int i = 0, Count = OldInstance->beh.constructors.GetLength(); i < Count; ++i)
					{
						asCScriptFunction* OldFunction = Engine->scriptFunctions[OldInstance->beh.constructors[i]];
						if (!OldFunction->traits.GetTrait(asTRAIT_GENERIC_TEMPLATE_FUNCTION))
						{
							asCScriptFunction* NewFunction = Engine->GenerateTemplateFunction(NewInstance, Engine->scriptFunctions[NewInstance->beh.constructors[i]]);
							ScriptUpdateMap.Functions.Add(OldFunction, NewFunction);
						}
					}

					if (OldInstance->beh.destruct != 0)
					{
						asCScriptFunction* OldDestructor = Engine->scriptFunctions[OldInstance->beh.destruct];
						if (!OldDestructor->traits.GetTrait(asTRAIT_GENERIC_TEMPLATE_FUNCTION))
						{
							asCScriptFunction* NewDestructor = Engine->GenerateTemplateFunction(NewInstance, Engine->scriptFunctions[NewInstance->beh.destruct]);
							ScriptUpdateMap.Functions.Add(OldDestructor, NewDestructor);
						}
					}

					return NewInstance;
				};
			};

			// Generate new template instances
			for (auto Module : CompiledModules)
			{
				asCModule* ScriptModule = Module->ScriptModule;
				if (ScriptModule == nullptr)
					continue;
				if (ScriptModule->ReloadOldModule == nullptr)
					continue;

				for (int n = 0, Count = ScriptModule->ReloadOldModule->templateInstances.Num(); n < Count; ++n)
				{
					asCObjectType* OldInstance = ScriptModule->ReloadOldModule->templateInstances[n];
					FTemplateReplacementHelper::CreateReplacementTemplateType(Engine, ScriptUpdateMap, OldInstance);
				}
			}

			// If we decided to do reference replacement on any modules that were previously compiled, do so
			for (auto Module : ModulesToUpdateReferences)
			{
				asCModule* ScriptModule = Module->ScriptModule;
				if (ScriptModule == nullptr)
					continue;

				ScriptModule->UpdateReferencesInReflectionDataOnly(ScriptUpdateMap);
				UpdateScriptReferencesInUnrealData(ScriptUpdateMap, Module);
			}

			if (bRecompiledAnyDependencies)
			{
				// Replace all the old pointers with the new pointers
				for (auto Module : CompiledModules)
				{
					asCModule* ScriptModule = Module->ScriptModule;
					if (ScriptModule == nullptr)
						continue;

					ScriptModule->UpdateReferencesInReflectionDataOnly(ScriptUpdateMap);
				}
			}
		}

		{
			FAngelscriptScopeTimer StageTimer(TEXT("script compilation class layouting"));

			// Now that we have properly stage1&stage2'd the modules, and all modules have references to the newly generated types,
			// we make sure anything we've actually recompiled is properly layouted
			for (auto Module : CompiledModules)
			{
				asCModule* ScriptModule = Module->ScriptModule;
				if (ScriptModule == nullptr)
					continue;
				if (Module->bLoadedPrecompiledCode)
					continue;

				auto Result = ScriptModule->builder->BuildLayoutClasses();
				if (Result != asSUCCESS)
					Module->bCompileError = true;
			}

			// Now that class layouting is done, make sure all template types are layouted as well
			ScriptEngine->deferCalculatingTemplateSize = false;
			for (asCObjectType* tmpl : ScriptEngine->unvalidatedTemplateInstances)
				tmpl->CalculateTemplateSize();

			if (ModulesToUpdateReferences.Num() != 0)
			{
				// Create allocations for global variables after all classes are layouted
				for (auto Module : CompiledModules)
				{
					asCModule* ScriptModule = Module->ScriptModule;
					if (ScriptModule == nullptr)
						continue;
					if (Module->bLoadedPrecompiledCode)
						continue;

					ScriptModule->builder->BuildAllocateGlobalVariables();
				}

				// Add the actual memory for the global variables to the replacement list
				for (const auto& GlobalPropertyElement : ScriptUpdateMap.GlobalProperties)
				{
					asCGlobalProperty* OldProperty = GlobalPropertyElement.Key;
					asCGlobalProperty* NewProperty = GlobalPropertyElement.Value;

					ScriptUpdateMap.GlobalVariablePointers.Add(
						OldProperty->GetAddressOfValue(),
						NewProperty->GetAddressOfValue()
					);
				}

				// Now that all classes are layouted (and global variables have been allocated),
				// we can update references in old modules. We do this before we layout the functions,
				// because that will initialize global variables and could call into old functions.
				for (auto Module : ModulesToUpdateReferences)
				{
					asCModule* ScriptModule = Module->ScriptModule;
					if (ScriptModule == nullptr)
						continue;

					ScriptModule->UpdateReferencesInScriptBytecode(ScriptUpdateMap);
				}
			}

			// Functions also need to be layouted
			for (auto Module : CompiledModules)
			{
				asCModule* ScriptModule = Module->ScriptModule;
				if (ScriptModule == nullptr)
					continue;
				if (Module->bLoadedPrecompiledCode)
					continue;
				if (Module->bCompileError)
					continue;

				auto Result = ScriptModule->builder->BuildLayoutFunctions();
				if (Result != asSUCCESS)
					Module->bCompileError = true;
			}
		}

		// It should be visible which modules are compiling during a hotreload
		if (CompileType != ECompileType::Initial && bIsInitialCompileFinished)
		{
			for (auto Module : CompiledModules)
			{
				asCModule* ScriptModule = Module->ScriptModule;
				if (ScriptModule == nullptr)
					continue;
				if (ScriptModule->ReloadState == asCModule::EReloadState::RecompiledWithStructuralChanges)
				{
					UE_LOG(Angelscript, Log, TEXT("Compiling (structural): %s"), *Module->ModuleName);
				}
				else
				{
					UE_LOG(Angelscript, Log, TEXT("Compiling (code only): %s"), *Module->ModuleName);
				}
			}
		}

		{
			FAngelscriptScopeTimer StageTimer(TEXT("script compilation stage3"));

			for (auto Module : CompiledModules)
			{
				// Update progress indicator
				if (--ProgressUpdateCounter <= 0 && ProgressUpdatesDone < 10)
				{
					SlowTask.EnterProgressFrame(0.025f);
					ProgressUpdateCounter = FMath::Max(CompiledModules.Num() / 10, 10);
					ProgressUpdatesDone += 1;
				}

				// Perform stage 3 of compilation
				CompileModule_Code_Stage3(CompileType, Module);

				// Cancel out on a compile error if we're
				// doing a hot-reload compile.
				if (Module->bCompileError)
					bHadCompileErrors = true;
			}
		}

		// If we added any precompiled modules, finalize them now
		if (PrecompiledData != nullptr && bUsePrecompiledData)
		{
			PrecompiledData->PrepareToFinalizePrecompiledModules();
		}

		{
			FAngelscriptScopeTimer StageTimer(TEXT("script compilation stage4"));

			// Validate all template instances we've created
			asCBuilder builder(ScriptEngine, nullptr);
			builder.Reset();
			builder.EvaluateTemplateInstances(false);
			ScriptEngine->deferValidationOfTemplateTypes = false;

			if (builder.numErrors > 0)
				bHadCompileErrors = true;

			if (!bHadCompileErrors)
			{
				for (auto Module : CompiledModules)
				{
					// Update progress indicator
					if (--ProgressUpdateCounter <= 0 && ProgressUpdatesDone < 10)
					{
						SlowTask.EnterProgressFrame(0.025f);
						ProgressUpdateCounter = FMath::Max(CompiledModules.Num() / 10, 10);
						ProgressUpdatesDone += 1;
					}

					// Perform stage 4 of compilation
					CompileModule_Globals_Stage4(CompileType, Module);

					// Cancel out on a compile error if we're
					// doing a hot-reload compile.
					if (Module->bCompileError)
						bHadCompileErrors = true;
				}
			}
		}
	}

	ScriptEngine->BuildCompleted();
	bIgnoreCompileErrorDiagnostics = false;

	// Check if any function imports would error out later
	if (!FAngelscriptManager::bUseAutomaticImportMethod)
	{
		if (!CheckFunctionImportsForNewModules(CompiledModules))
		{
			bHadCompileErrors = true;
		}
	}

	// Decide whether to swap in the new modules or not
	bool bShouldSwapInModules = true;
	bool bFullReloadRequired = false;

	// In script reloads, don't swap in anything unless
	// *everything* compiled without errors, so we don't
	// end up in inconsistent state.
	if (bHadCompileErrors)
	{
		UE_LOG(Angelscript, Error, TEXT("Hot reload failed due to script compile errors. Keeping all old script code."));
		bShouldSwapInModules = false;
	}

#if WITH_EDITOR
	// If any script modules specified usage restrictions we should check those now
	CheckUsageRestrictions(CompiledModules);
#endif

	TArray<TSharedRef<FAngelscriptModuleDesc>> DiscardedModules;

	if (bShouldSwapInModules)
	{
		FAngelscriptClassGenerator ClassGenerator;

		// Run the delegate that the game might hook to provide more errors/warnings
		FAngelscriptCodeModule::GetPreGenerateClasses().Broadcast(CompiledModules);

		for (auto Module : CompiledModules)
		{
			if (Module->ScriptModule != nullptr)
			{
				// Generate classes for the module based on the preprocessed data and the compiled angelscript data
				ClassGenerator.AddModule(Module);
			}
		}

		// Update progress indicator
		SlowTask.EnterProgressFrame(0.5f, FText::FromString(TEXT("Class Generator Setup")));

		// Perform the actual reload
		auto ReloadReq = ClassGenerator.Setup();

		bool bVerifiedProperties = true;
		if (GIsEditor || bScriptDevelopmentMode)
		{
			// Verify Unreal properties
			bVerifiedProperties = VerifyPropertySpecifiers(CompiledModules);
		}

		if (!bVerifiedProperties)
		{
			bShouldSwapInModules = false;
			bHadCompileErrors = true;
		}
		else
		{
			// Emit diagnostics before we go into potentially very slow unreal reload
			EmitDiagnostics();

			// Update progress indicator
			SlowTask.EnterProgressFrame(1.5f, FText::FromString(TEXT("Class Generation")));

			switch (ReloadReq)
			{
				case FAngelscriptClassGenerator::EReloadRequirement::SoftReload:
					SwapInModules(CompiledModules, DiscardedModules);
					ClassGenerator.PerformSoftReload();
					break;
				case FAngelscriptClassGenerator::EReloadRequirement::FullReloadSuggested:
					if (CompileType == ECompileType::SoftReloadOnly)
					{
#if WITH_EDITOR
						FString Msg =
							TEXT("Performing a Soft Reload during PIE. New UPROPERTY()s and UFUNCTION()s won't show up")
								TEXT(" until full reload. A Full Reload will be queued for after PIE ends.");
						UE_LOG(Angelscript, Warning, TEXT("%s"), *Msg);

						for (auto Module : CompiledModules)
						{
							if (ClassGenerator.WantsFullReload(Module))
							{
								TArray<int32> Lines;
								ClassGenerator.GetFullReloadLines(Module, Lines);
								for (int32 ReloadLine : Lines)
									ScriptCompileError(Module, ReloadLine, Msg, false);
							}
						}
#endif
						bWasFullyHandled = false;
						SwapInModules(CompiledModules, DiscardedModules);
						ClassGenerator.PerformSoftReload();
					}
					else
					{
						SwapInModules(CompiledModules, DiscardedModules);
						ClassGenerator.PerformFullReload();
					}
					break;
				case FAngelscriptClassGenerator::EReloadRequirement::FullReloadRequired:
					if (CompileType == ECompileType::SoftReloadOnly)
					{
						FString Msg =
							TEXT("Full Reload is required due to UPROPERTY() or UFUNCTION() changes, but cannot")
								TEXT(" perform a full reload right now. Keeping old angelscript code active.");
						UE_LOG(Angelscript, Error, TEXT("%s"), *Msg);

						for (auto Module : CompiledModules)
						{
							if (ClassGenerator.NeedsFullReload(Module))
							{
								TArray<int32> Lines;
								ClassGenerator.GetFullReloadLines(Module, Lines);
								for (int32 ReloadLine : Lines)
									ScriptCompileError(Module, ReloadLine, Msg, true);
							}
						}
						bShouldSwapInModules = false;
						bFullReloadRequired = true;
					}
					else
					{
						SwapInModules(CompiledModules, DiscardedModules);
						ClassGenerator.PerformFullReload();
					}
					break;
				case FAngelscriptClassGenerator::EReloadRequirement::Error:
					UE_LOG(Angelscript, Error,
						TEXT("An error was encountered during angelscript hot reload. Keeping old angelscript code "
							 "active."));
					bShouldSwapInModules = false;
					bHadCompileErrors = true;
					break;
			}
		}
	}

	if (bShouldSwapInModules)
	{
		// Actually delete old modules
		{
			FAngelscriptScopeTimer PostTimer(TEXT("old module cleanup"));
			for (auto OldModule : DiscardedModules)
			{
				if (OldModule->ScriptModule != nullptr)
				{
					// Discard it from the engine as well
					Engine->DiscardModule(OldModule->ScriptModule->GetName());
					OldModule->ScriptModule = nullptr;
				}
			}

			Engine->DeleteDiscardedModules();
		}
		
		// Remove all old template instances we are no longer using
		for (auto TemplateElem : ScriptUpdateMap.TemplateInstances)
		{
			// Key contains the old instance, which we discard
			Engine->DiscardTemplateInstance(TemplateElem.Key);
			// We've held a reference to the old instance just in case, which we drop now
			TemplateElem.Key->ReleaseInternal();
		}

		{
			FAngelscriptScopeTimer PostTimer(TEXT("update module cache"));
			for (auto Module : CompiledModules)
			{
				// Update the cache of script module to module desc
				if (Module->ScriptModule != nullptr)
				{
					ModulesByScriptModule.Add(Module->ScriptModule, Module);
				}

				// If the module has received any synthetic errors from the class generator,
				// make sure it gets recompiled until those go away
				if (Module->bModuleSwapInError)
				{
					for (auto& Section : Module->Code)
						PreviouslyFailedReloadFiles.Add(FFilenamePair{ Section.AbsoluteFilename, Section.RelativeFilename });
				}
			}
		}

		// We changed some modules, so we should re-resolve all declared imports in all modules
		//  Technically we could store dependencies for these as well and only re-resolve as needed,
		//  but it's not expensive at all so not worth doing.
		if (!FAngelscriptManager::bUseAutomaticImportMethod)
		{
			FAngelscriptScopeTimer PostTimer(TEXT("resolve declared imports"));
			ResolveAllDeclaredImports();
		}
	}
	else
	{
		// Any existing modules that we replaced references in,
		// well, we need to replace the references back again to the old ones, yay!
		if (ModulesToUpdateReferences.Num() != 0)
		{
			FAngelscriptScopeTimer PostTimer(TEXT("undo script reference update"));

			asModuleReferenceUpdateMap ReverseUpdateMap;
			ScriptUpdateMap.BuildReverseMap(OUT ReverseUpdateMap);

			for (auto Module : ModulesToUpdateReferences)
			{
				asCModule* ScriptModule = Module->ScriptModule;
				if (ScriptModule == nullptr)
					continue;

				ScriptModule->UpdateReferencesInReflectionDataOnly(ReverseUpdateMap);
				UpdateScriptReferencesInUnrealData(ReverseUpdateMap, Module);
				ScriptModule->UpdateReferencesInScriptBytecode(ReverseUpdateMap);
			}
		}

		// Remove all new template instances we won't actually be using
		for (auto TemplateElem : ScriptUpdateMap.TemplateInstances)
		{
			// Value contains the new instance, which we discard
			Engine->DiscardTemplateInstance(TemplateElem.Value);
			// We previously held a reference to the old instance (in Key), to make sure
			// it didn't get deleted. We drop that reference now, it's been replaced back into the old modules.
			TemplateElem.Key->ReleaseInternal();
		}

		// Discard script modules for stuff we haven't decided to swap in
		for (auto Module : CompiledModules)
		{
			if (Module->ScriptModule != nullptr)
			{
				auto* OldScriptModule = (asCModule*)Module->ScriptModule;
				OldScriptModule->RemoveTypesAndGlobalsFromEngineAvailability();
				OldScriptModule->InternalReset();
				Engine->DiscardModule(OldScriptModule->GetName());

				Module->ScriptModule = nullptr;
			}
		}
	}

	// If we have any new diagnostics, emit them again
	if (bDiagnosticsDirty)
		EmitDiagnostics();

	// Reset reload state on all modules
	for (int i = 0, Count = Engine->scriptModules.GetLength(); i < Count; ++i)
	{
		asCModule* ScriptModule = Engine->scriptModules[i];
		if (ScriptModule == nullptr)
			continue;

		ScriptModule->ReloadState = asCModule::EReloadState::None;
		ScriptModule->ReloadOldModule = nullptr;
		ScriptModule->ReloadNewModule = nullptr;
	}

	ECompileResult Result = ECompileResult::FullyHandled;
	if (!bShouldSwapInModules || bHadCompileErrors)
		Result = bFullReloadRequired ? ECompileResult::ErrorNeedFullReload : ECompileResult::Error;
	else if (!bWasFullyHandled)
		Result = ECompileResult::PartiallyHandled;

	if (bShouldSwapInModules && !bHadCompileErrors)
	{
		FAngelscriptCompilationDelegate& PostCompileDelegate = FAngelscriptCodeModule::GetPostCompile();
		if (PostCompileDelegate.IsBound())
			PostCompileDelegate.Broadcast();

#if WITH_EDITOR
		if (FAngelscriptManager::Get().bIsInitialCompileFinished && bCompletedAssetScan
			&& !FAngelscriptManager::bSimulateCooked && !IsRunningCookCommandlet()
			&& GetDefault<UAngelscriptTestSettings>()->bEnableTestDiscovery)
		{
			for (auto Module : CompiledModules)
			{
				// Avoid discovering tests if Asset Manager has not finised scanning.
				// Test discovery will be performed after initial scan is completed.
				DiscoverUnitTests(*Module, Module->UnitTestFunctions);
				DiscoverIntegrationTests(*Module, Module->IntegrationTestFunctions);
			}
		}
#endif
	}

	if (CompileType != ECompileType::Initial
		&& Result != ECompileResult::FullyHandled)
	{
		TArray<FFilenamePair> AllCompiledFiles;
		for (auto Module : CompiledModules)
		{
			for (auto& Section : Module->Code)
				AllCompiledFiles.Add(FFilenamePair{ Section.AbsoluteFilename, Section.RelativeFilename });
		}

		if (Result == ECompileResult::ErrorNeedFullReload)
		{
			// An error was caused because we need a full reload, so queue that up
			for (const auto& RepeatFile : AllCompiledFiles)
				QueuedFullReloadFiles.Add(RepeatFile);

			PreviouslyFailedReloadFiles.Append(AllCompiledFiles);
		}
		else if (Result == ECompileResult::Error)
		{
			// Store failed files so we retry them next reload automatically
			PreviouslyFailedReloadFiles.Append(AllCompiledFiles);
		}
		else if (Result == ECompileResult::PartiallyHandled)
		{
			// If the compilation wasn't fully handled, queue up the files we soft reloaded
			// for a full reload later when it is possible.
			for (const auto& RepeatFile : AllCompiledFiles)
				QueuedFullReloadFiles.Add(RepeatFile);
		}
	}

	OutCompiledModules = MoveTemp(CompiledModules);

	return Result;
}

void FAngelscriptManager::UpdateScriptReferencesInUnrealData(struct asModuleReferenceUpdateMap& UpdateMap, TSharedRef<FAngelscriptModuleDesc> Module)
{
	auto UpdateTypeUsage = [&](FAngelscriptTypeUsage& Type)
	{
		if (Type.ScriptClass != nullptr)
		{
			auto* NewTypeInfo = UpdateMap.Types.FindRef((asCTypeInfo*)Type.ScriptClass);
			if (NewTypeInfo != nullptr)
				Type.ScriptClass = NewTypeInfo;
		}
	};

	auto UpdateFunctionDesc = [&](FAngelscriptFunctionDesc& Function)
	{
		UpdateTypeUsage(Function.ReturnType);

		for (auto& Argument : Function.Arguments)
		{
			UpdateTypeUsage(Argument.Type);
		}
	};

	auto UpdateUnrealFunction = [&](UASFunction* Function)
	{
		UpdateTypeUsage(Function->ReturnArgument.Type);
		for (auto& Argument : Function->Arguments)
			UpdateTypeUsage(Argument.Type);
		for (auto& Argument : Function->DestroyArguments)
			UpdateTypeUsage(Argument.Type);
	};

	for (auto Class : Module->Classes)
	{
		for (auto Property : Class->Properties)
		{
			UpdateTypeUsage(Property->PropertyType);
		}

		for (auto Method : Class->Methods)
		{
			UpdateFunctionDesc(*Method);
			if (Method->Function != nullptr)
				UpdateUnrealFunction((UASFunction*)Method->Function);
		}
	}

	for (auto Delegate : Module->Delegates)
	{
		UpdateFunctionDesc(*Delegate->Signature);
	}
}

void FAngelscriptManager::CompileModule_Types_Stage1(ECompileType CompileType, TSharedRef<struct FAngelscriptModuleDesc> Module, const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& ImportedModules)
{
	// Modules always compile with a temporary name, the code
	// then later decides whether to use them and rename them.
	FString TempName = Module->ModuleName;
	if (CompileType != ECompileType::Initial)
	{
		TempName = FString::Printf(TEXT("%s_NEW_%d"), *TempName, TempNameIndex);
		TempNameIndex += 1;
	}

	// Generate the angelscript module
	auto* ScriptModule = (asCModule*)Engine->GetModule(TCHAR_TO_ANSI(*TempName), asGM_ALWAYS_CREATE);
	ScriptModule->baseModuleName = TCHAR_TO_ANSI(*Module->ModuleName);

	Module->CombinedDependencyHash = Module->CodeHash;

	// Add stuff from all imported modules into the newly generated module
	bool bAllImportsPreCompiled = true;
	for (auto ImportModule : ImportedModules)
	{
		if (ensure(ImportModule->ScriptModule != nullptr))
		{
			ImportIntoModule(ScriptModule, ImportModule->ScriptModule);
		}

		// If any of our imports are not precompiled, we should not use precompiled code either
		if (!ImportModule->bLoadedPrecompiledCode)
		{
			bAllImportsPreCompiled = false;
		}

		// Combine the hash of the imported module into our own dependency hash
		Module->CombinedDependencyHash ^= ImportModule->CombinedDependencyHash;
	}

	// Check if we have precompiled data for this module and use it if we can
	if (PrecompiledData != nullptr && bAllImportsPreCompiled && bUsePrecompiledData)
	{
		const FAngelscriptPrecompiledModule* CompiledModule = PrecompiledData->Modules.Find(Module->ModuleName);
		if (CompiledModule != nullptr)
		{
			// Check if file content hashes are the same or not
			if (CompiledModule->CodeHash == Module->CodeHash)
			{
				CompiledModule->ApplyToModule_Stage1(*PrecompiledData, ScriptModule);

				Module->PrecompiledData = CompiledModule;
				Module->bCompileError = false;
				Module->ScriptModule = ScriptModule;
				Module->bLoadedPrecompiledCode = true;

				return;
			}
			else
			{
				UE_LOG(Angelscript, Warning, TEXT("Angelscript precompiled data for module '%s' did not match script as loaded from file. Discarding precompiled data."), *Module->ModuleName);
			}
		}
		else
		{
			UE_LOG(Angelscript, Warning, TEXT("Angelscript precompiled data did not include any code for module '%s'."), *Module->ModuleName);
		}
	}

	// Set up proper pre-class data, this tells angelscript how
	// to treat compilation for classes derived from code classes.
	for (auto ClassDesc : Module->Classes)
	{
		if (ClassDesc->CodeSuperClass == nullptr)
			continue;

		asPreClassData Data;
		Data.PropertyOffset = ClassDesc->CodeSuperClass->GetPropertiesSize();

		FString SuperClassName = FAngelscriptType::GetBoundClassName(ClassDesc->CodeSuperClass);
		Data.ShadowType = Engine->allRegisteredTypesByName.FindFirst_CaseInsensitive(TCHAR_TO_ANSI(*SuperClassName));

		checkf(Data.ShadowType != nullptr, TEXT("Unable to find C++ class %s to inherit from"), *SuperClassName);

		ScriptModule->AddPreClassData(TCHAR_TO_ANSI(*ClassDesc->ClassName), Data);
	}

	// Delegates need to be tagged with a userdata tag so they can be detected correctly during compilation
	for (auto DelegateDesc : Module->Delegates)
	{
		asPreClassData Data;
		if (DelegateDesc->bIsMulticast)
			Data.InitialUserData = FAngelscriptType::TAG_UserData_Multicast_Delegate;
		else
			Data.InitialUserData = FAngelscriptType::TAG_UserData_Delegate;

		ScriptModule->AddPreClassData(TCHAR_TO_ANSI(*DelegateDesc->DelegateName), Data);
	}

	// Add all code we need
	for (auto& Section : Module->Code)
	{
		ScriptModule->AddScriptSection(TCHAR_TO_ANSI(*Section.AbsoluteFilename), TCHAR_TO_UTF8(*Section.Code), 0, 0);
	}
	
	// Set the code hash as userdata so we can find it later
#if AS_CAN_GENERATE_JIT
	ScriptModule->SetUserData((void*)(size_t)Module->CombinedDependencyHash, 0);
#endif

#if WITH_EDITOR
	// Allow the script compiler to see which lines are editor-only so it can emit warnings
	ScriptModule->builder->SetEditorOnlyBlockLinePositions(Module->EditorOnlyBlockLines);
	ScriptModule->builder->isEditorOnlyModule = Module->ModuleName.StartsWith(TEXT("Editor.")) || Module->ModuleName.Contains(TEXT(".Editor."));
#endif

	Module->ScriptModule = ScriptModule;
}

void FAngelscriptManager::CompileModule_Functions_Stage2(ECompileType CompileType, TSharedRef<struct FAngelscriptModuleDesc> Module)
{
	auto* ScriptModule = (asCModule*)Module->ScriptModule;
	if (Module->bCompileError)
		return;
	if (ScriptModule == nullptr)
		return;

	if (Module->bLoadedPrecompiledCode)
	{
		Module->PrecompiledData->ApplyToModule_Stage2(*PrecompiledData, ScriptModule);
		return;
	}

	auto Result = ScriptModule->builder->BuildGenerateFunctions();
	if (Result != asSUCCESS)
		Module->bCompileError = true;
}

void FAngelscriptManager::CompileModule_Code_Stage3(ECompileType CompileType, TSharedRef<struct FAngelscriptModuleDesc> Module)
{
	auto* ScriptModule = (asCModule*)Module->ScriptModule;
	if (ScriptModule == nullptr)
		return;

	if (Module->bLoadedPrecompiledCode)
	{
		Module->PrecompiledData->ApplyToModule_Stage3(*PrecompiledData, ScriptModule);
		return;
	}

	auto Result = ScriptModule->builder->BuildCompileCode();
	if (Result != asSUCCESS)
		Module->bCompileError = true;

	asDELETE(ScriptModule->builder, asCBuilder);
	ScriptModule->builder = nullptr;

	ScriptModule->JITCompile();
}

void FAngelscriptManager::CompileModule_Globals_Stage4(ECompileType CompileType, TSharedRef<struct FAngelscriptModuleDesc> Module)
{
	auto* ScriptModule = (asCModule*)Module->ScriptModule;
	if (ScriptModule == nullptr)
		return;

	check(!Module->bCompileError);
	ScriptModule->ResetGlobalVars(0);

#if WITH_AS_COVERAGE
	if (CodeCoverage != nullptr && !Module->bCompileError)
	{
		CodeCoverage->MapExecutableLines(*Module);
	}
#endif
}

void FAngelscriptManager::ImportIntoModule(class asIScriptModule* IntoModule, class asIScriptModule* FromModuleIntf)
{
	asCModule* FromModule = (asCModule*)FromModuleIntf;
	IntoModule->ImportModule(FromModule);
}

void FAngelscriptManager::ResolveAllDeclaredImports()
{
	for (auto& Elem : ActiveModules)
		ResolveDeclaredImports(Elem.Value->ScriptModule);
}

#if WITH_AS_DEBUGSERVER
bool FAngelscriptManager::IsEvaluatingDebuggerWatch()
{
	if (DebugServer == nullptr)
		return false;
	if (DebugServer->bIsEvaluatingDebuggerWatch)
		return true;
	return false;
}
#endif

FString FAngelscriptManager::FormatDiagnostics()
{
	FString Str;
	for (auto& FileDiagElem : Diagnostics)
	{
		if (FileDiagElem.Value.Diagnostics.Num() == 0)
			continue;
		Str += TEXT("\n");
		Str += FileDiagElem.Value.Filename;
		Str += TEXT(":\n");
		for (auto& Diag : FileDiagElem.Value.Diagnostics)
		{
			if (Diag.Row || Diag.Column)
				Str += FString::Printf(TEXT("(%d:%d) "), Diag.Row, Diag.Column);
			Str += Diag.Message;
			Str += TEXT("\n");
		}
	}
	return Str;
}

void FAngelscriptManager::ResetDiagnostics()
{
	Diagnostics.Empty();
}

void FAngelscriptManager::EmitDiagnostics(class FSocket* Client)
{
	// Output captured diagnostic messages to debugger
	for (auto Iterator = Diagnostics.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator->Value.Diagnostics.Num() == 0)
		{
			if (Iterator->Value.bHasEmittedAny || Iterator->Value.bIsCompiling)
				EmitDiagnostics(Iterator->Value, Client);

#if WITH_AS_DEBUGSERVER
			if (Client == nullptr && (DebugServer == nullptr || DebugServer->HasAnyClients()))
				Iterator.RemoveCurrent();
#else
			Iterator.RemoveCurrent();
#endif
		}
		else
		{
			EmitDiagnostics(Iterator->Value, Client);
			Iterator->Value.bHasEmittedAny = true;
		}
	}

	bDiagnosticsDirty = false;
}

void FAngelscriptManager::EmitDiagnostics(FDiagnostics& Diag, class FSocket* Client)
{
#if WITH_AS_DEBUGSERVER
	if (DebugServer == nullptr)
		return;

	FAngelscriptDiagnostics Message;
	Message.Filename = Diag.Filename;
	for (auto& Ms : Diag.Diagnostics)
	{
		FAngelscriptDiagnostic New;
		New.Message = Ms.Message;
		New.Line = Ms.Row;
		New.Character = Ms.Column;
		New.bIsError = Ms.bIsError;
		New.bIsInfo = Ms.bIsInfo;
		Message.Diagnostics.Add(New);
	}

	if (Client == nullptr)
		DebugServer->SendMessageToAll(EDebugMessageType::Diagnostics, Message);
	else
		DebugServer->SendMessageToClient(Client, EDebugMessageType::Diagnostics, Message);
#endif
}

#if WITH_EDITOR
void FAngelscriptManager::CheckUsageRestrictions(const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& Modules)
{
	// Figure out which modules have restrictions
	// We do this for both modules that are compiling right now, and modules that were previously compiled
	TMap<asCModule*, TSharedRef<FAngelscriptModuleDesc>> ModulesWithRestrictions;
	for (auto Module : Modules)
	{
		auto* ScriptModule = (asCModule*)Module->ScriptModule;
		if (ScriptModule == nullptr)
			return;
		if (Module->Code.Num() == 0)
			return;

		if (Module->UsageRestrictions.Num() != 0)
			ModulesWithRestrictions.Add(ScriptModule, Module);
	}

	for (auto ModuleElem : ActiveModules)
	{
		auto Module = ModuleElem.Value;
		auto* ScriptModule = (asCModule*)Module->ScriptModule;
		if (ScriptModule == nullptr)
			return;
		if (Module->Code.Num() == 0)
			return;

		if (Module->UsageRestrictions.Num() != 0)
			ModulesWithRestrictions.Add(ScriptModule, Module);
	}

	// Early out if we don't have any restrictions at all
	if (ModulesWithRestrictions.Num() == 0)
		return;

	// Check each module we're compiling if it violates any restrictions
	for (auto Module : Modules)
	{
		auto* ScriptModule = (asCModule*)Module->ScriptModule;
		if (ScriptModule == nullptr)
			return;
		if (Module->Code.Num() == 0)
			return;

		for (const auto& DependencyElement : ScriptModule->moduleDependencies)
		{
			const auto& DependencyInfo = DependencyElement.Value;
			asCModule* Dependency = DependencyElement.Key;

			auto* DependencyModuleDescPtr = ModulesWithRestrictions.Find(Dependency);
			if (DependencyModuleDescPtr == nullptr)
				continue;

			auto DependencyModuleDesc = *DependencyModuleDescPtr;
			bool bMatchesAllow = false;
			bool bMatchesDisallow = false;
			bool bHasAnyDisallow = false;

			for (auto& Restriction : DependencyModuleDesc->UsageRestrictions)
			{
				if (Restriction.bIsAllow)
				{
					if (Module->ModuleName.MatchesWildcard(Restriction.Pattern))
						bMatchesAllow = true;
				}
				else
				{
					bHasAnyDisallow = true;
					if (Module->ModuleName.MatchesWildcard(Restriction.Pattern))
						bMatchesDisallow = true;
				}
			}

			if (!bMatchesAllow && (bMatchesDisallow || !bHasAnyDisallow))
			{
				ScriptCompileError(
					Module, DependencyInfo.FirstLineNumber,
					FString::Printf(
						TEXT("Restricted usage of module %s within module %s is disallowed."),
						*DependencyModuleDesc->ModuleName,
						*Module->ModuleName
					)
				);
			}
		}
	}
}
#endif

void FAngelscriptManager::ResolveDeclaredImports(class asIScriptModule* Module)
{
	if (Module == nullptr)
		return;

	int32 ImportCount = Module->GetImportedFunctionCount();
	if (ImportCount == 0)
		return;

	auto ToModuleDesc = GetModule(ANSI_TO_TCHAR(Module->GetName()));
	for (int32 ImportIndex = 0; ImportIndex < ImportCount; ++ImportIndex)
	{
		const char* Decl = Module->GetImportedFunctionDeclaration(ImportIndex);

		FString FromModuleName = ANSI_TO_TCHAR(Module->GetImportedFunctionSourceModule(ImportIndex));
		auto FromModule = GetModule(FromModuleName);
		if (!FromModule.IsValid() || FromModule->ScriptModule == nullptr)
		{
			// Errors already presented by CheckFunctionImportsForNewModules
			Module->UnbindImportedFunction(ImportIndex);
			continue;
		}

		asIScriptFunction* Function = FromModule->ScriptModule->GetFunctionByDecl(Decl);
		if (Function == nullptr)
		{
			// Errors already presented by CheckFunctionImportsForNewModules
			Module->UnbindImportedFunction(ImportIndex);
			continue;
		}

		Module->BindImportedFunction(ImportIndex, Function);
	}
}

bool FAngelscriptManager::CheckFunctionImportsForNewModules(const TArray<TSharedRef<struct FAngelscriptModuleDesc>>& Modules)
{
	bool bValid = true;

	TMap<FString, TSharedRef<struct FAngelscriptModuleDesc>> SwappingModules;
	for (auto Module : Modules)
		SwappingModules.Add(Module->ModuleName, Module);

	auto FindModule = [&](const FString& Name) -> TSharedPtr<FAngelscriptModuleDesc>
	{
		auto* SwapModule = SwappingModules.Find(Name);
		if (SwapModule != nullptr)
			return *SwapModule;
		return GetModule(Name);
	};

	auto CheckModule = [&](TSharedRef<struct FAngelscriptModuleDesc> Module)
	{	
		auto* ScriptModule = Module->ScriptModule;
		bool bModuleValid = ScriptModule != nullptr;

		if (bModuleValid)
		{
			for (int32 ImportIndex = 0, ImportCount = ScriptModule->GetImportedFunctionCount(); ImportIndex < ImportCount; ++ImportIndex)
			{
				const char* Decl = ScriptModule->GetImportedFunctionDeclaration(ImportIndex);

				FString FromModuleName = ANSI_TO_TCHAR(ScriptModule->GetImportedFunctionSourceModule(ImportIndex));
				auto FromModule = FindModule(FromModuleName);
				if (!FromModule.IsValid() || FromModule->ScriptModule == nullptr)
				{
					// Don't show error if we had a compile error in that module, we need
					// to fix that first so this error isn't helpful.
					if (!FromModule.IsValid() || !FromModule->bCompileError)
					{
						ScriptCompileError(Module, 1, FString::Printf(
							TEXT("Error resolving import in module %s of function %s: could not find module %s to import from."),
							ANSI_TO_TCHAR(ScriptModule->GetName()), ANSI_TO_TCHAR(Decl), *FromModuleName));
					}
					bModuleValid = false;
					continue;
				}

				asIScriptFunction* Function = FromModule->ScriptModule->GetFunctionByDecl(Decl);
				if (Function == nullptr)
				{
					ScriptCompileError(Module, 1, FString::Printf(
						TEXT("Error resolving import in module %s of function %s: could not find function with this signature in module %s."),
						ANSI_TO_TCHAR(ScriptModule->GetName()), ANSI_TO_TCHAR(Decl), *FromModuleName));
					bModuleValid = false;
					continue;
				}
			}
		}
	
		if (!bModuleValid)
		{
			bValid = false;

			// Make sure this module is added to the next reload
			for (auto& Section : Module->Code)
				PreviouslyFailedReloadFiles.Add(FFilenamePair{ Section.AbsoluteFilename, Section.RelativeFilename });
		}
	};

	// Check new modules
	for (auto Module : Modules)
		CheckModule(Module);

	// Check any old modules we aren't swapping in
	for (auto OldElem : ActiveModules)
	{
		if (SwappingModules.Contains(OldElem.Value->ModuleName))
			continue;
		CheckModule(OldElem.Value);
	}

	return bValid;
}

void* FAngelscriptManager::GetCurrentFunctionUserDataPtr()
{
	auto* Function = (asCScriptFunction*)asGetActiveFunction();
	if (Function == nullptr)
		return nullptr;
	return Function->userData;
}

asITypeInfo* FAngelscriptManager::GetCurrentFunctionObjectType()
{
	auto* Function = (asCScriptFunction*)asGetActiveFunction();
	if (Function == nullptr)
		return nullptr;
	return Function->GetObjectType();
}

asCContext* FAngelscriptManager::GetCurrentScriptContext()
{
	return (asCContext*)asGetActiveContext();
}

asCContext* FAngelscriptManager::GetPreviousScriptContext()
{
	auto* tld = asCThreadManager::GetLocalData();
	if (tld->activeContext != nullptr)
		return tld->activeContext;

	auto* Execution = tld->activeExecution;
	while (Execution != nullptr)
	{
		if (Execution->prevContext != nullptr)
			return Execution->prevContext;
		Execution = Execution->prevExecution;
	}

	return nullptr;
}

bool FAngelscriptManager::IsOutdated(asIScriptFunction* Function)
{
	// Outdated functions will have a null module set,
	// since the module has been discarded.
	return Function->GetModule() == nullptr;
}

void FAngelscriptManager::SetOutdated(asIScriptModule* OldModule)
{
}

TSharedPtr<FAngelscriptClassDesc> FAngelscriptManager::GetClass(const FString& ClassName, TSharedPtr<FAngelscriptModuleDesc>* FoundInModule)
{
#if AS_CAN_HOTRELOAD
	if (ActiveClassesByName.Num() != 0)
	{
		auto* FoundEntry = ActiveClassesByName.Find(ClassName);
		if (FoundEntry != nullptr)
		{
			if (FoundInModule != nullptr)
				*FoundInModule = FoundEntry->Key;
			return FoundEntry->Value;
		}
		else
		{
			return nullptr;
		}
	}
#endif

	for (auto ModulePair : ActiveModules)
	{
		auto Module = ModulePair.Value;
		for (auto Class : Module->Classes)
		{
			if(Class->ClassName == ClassName)
			{
				if (FoundInModule != nullptr)
					*FoundInModule = Module;
				return Class;
			}
		}
	}

	if (FoundInModule != nullptr)
		*FoundInModule = nullptr;
	return nullptr;
}

TSharedPtr<FAngelscriptEnumDesc> FAngelscriptManager::GetEnum(const FString& EnumName, TSharedPtr<FAngelscriptModuleDesc>* FoundInModule)
{
#if AS_CAN_HOTRELOAD
	if (ActiveEnumsByName.Num() != 0)
	{
		auto* FoundEntry = ActiveEnumsByName.Find(EnumName);
		if (FoundEntry != nullptr)
		{
			if (FoundInModule != nullptr)
				*FoundInModule = FoundEntry->Key;
			return FoundEntry->Value;
		}
		else
		{
			return nullptr;
		}
	}
#endif

	for (auto ModulePair : ActiveModules)
	{
		auto Module = ModulePair.Value;
		for (auto EnumDesc : Module->Enums)
		{
			if(EnumDesc->EnumName == EnumName)
			{
				if (FoundInModule != nullptr)
					*FoundInModule = Module;
				return EnumDesc;
			}
		}
	}

	if (FoundInModule != nullptr)
		*FoundInModule = nullptr;
	return nullptr;
}

TSharedPtr<FAngelscriptDelegateDesc> FAngelscriptManager::GetDelegate(const FString& DelegateName, TSharedPtr<FAngelscriptModuleDesc>* FoundInModule)
{
#if AS_CAN_HOTRELOAD
	if (ActiveDelegatesByName.Num() != 0)
	{
		auto* FoundEntry = ActiveDelegatesByName.Find(DelegateName);
		if (FoundEntry != nullptr)
		{
			if (FoundInModule != nullptr)
				*FoundInModule = FoundEntry->Key;
			return FoundEntry->Value;
		}
		else
		{
			return nullptr;
		}
	}
#endif

	for (auto ModulePair : ActiveModules)
	{
		auto Module = ModulePair.Value;
		for (auto DelegateDesc : Module->Delegates)
		{
			if(DelegateDesc->DelegateName == DelegateName)
			{
				if (FoundInModule != nullptr)
					*FoundInModule = Module;
				return DelegateDesc;
			}
		}
	}

	if (FoundInModule != nullptr)
		*FoundInModule = nullptr;
	return nullptr;
}

bool FAngelscriptFunctionDesc::SignatureMatches(TSharedPtr<FAngelscriptFunctionDesc> OtherFunction, bool bCheckNames) const
{
	if (ReturnType != OtherFunction->ReturnType)
		return false;
	
	return ParametersMatches(OtherFunction, bCheckNames);
}
bool FAngelscriptFunctionDesc::ParametersMatches(TSharedPtr<FAngelscriptFunctionDesc> OtherFunction, bool bCheckNames) const
{
	if (Arguments.Num() != OtherFunction->Arguments.Num())
		return false;

	for (int32 i = 0, ArgCount = Arguments.Num(); i < ArgCount; ++i)
	{
		if (!Arguments[i].IsDefinitionEquivalent(OtherFunction->Arguments[i]))
			return false;
		if (bCheckNames && Arguments[i].ArgumentName != OtherFunction->Arguments[i].ArgumentName)
			return false;
	}

	return true;
}

void FAngelscriptManager::Throw(const ANSICHAR* Exception)
{
	auto* tld = asCThreadManager::GetLocalData();
	if (tld->activeExecution != nullptr)
	{
		tld->activeExecution->bExceptionThrown = true;
		HandleExceptionFromJIT(Exception);
	}
	else if (tld->activeContext != nullptr)
	{
		tld->activeContext->SetException(Exception);
	}
}

void FAngelscriptManager::ScriptCompileError(const FString& AbsoluteFilename, const FDiagnostic& Diagnostic)
{
	bDiagnosticsDirty = true;

	auto& FileDiagnostics = Diagnostics.FindOrAdd(AbsoluteFilename);
	FileDiagnostics.Filename = AbsoluteFilename;
	FileDiagnostics.Diagnostics.Add(Diagnostic);

	if (Diagnostic.bIsError)
	{
		UE_LOG(Angelscript, Error, TEXT("%s"), *Diagnostic.Message);
	}
	else
	{
		UE_LOG(Angelscript, Warning, TEXT("%s"), *Diagnostic.Message);
	}
}

void FAngelscriptManager::ScriptCompileError(TSharedPtr<FAngelscriptModuleDesc> Module, int32 LineNumber, const FString& Message, bool bIsError)
{
	FAngelscriptManager::FDiagnostic Diagnostic;
	Diagnostic.Message = Message;
	Diagnostic.Row = LineNumber;
	Diagnostic.Column = 1;
	Diagnostic.bIsError = bIsError;
	Diagnostic.bIsInfo = false;

	if (Module->Code.Num() != 0)
		ScriptCompileError(Module->Code[0].AbsoluteFilename, Diagnostic);
	else
		ScriptCompileError(Module->ModuleName, Diagnostic);
}

void FAngelscriptManager::ScriptCompileError(UClass* InsideClass, const FString& FunctionName, const FString& Message, bool bIsError)
{
	UASClass* asClass = Cast<UASClass>(InsideClass);
	if (asClass == nullptr)
	{
		//UE_LOG(Angelscript, Warning, TEXT("Failed Cast to UASClass"))
		GLog->Log(TEXT("Failed Cast to UASClass"));
		return;
	}
	//auto* ScriptTypePtr = (asITypeInfo*)InsideClass->ScriptTypePtr;
	auto* ScriptTypePtr = (asITypeInfo*)asClass->ScriptTypePtr;
	if (ScriptTypePtr == nullptr)
	{
		ensureMsgf(false, TEXT("Not a script class."));
		return;
	}

	//auto* ScriptModule = ScriptTypePtr->GetModule();
	asIScriptModule* ScriptModule = ScriptTypePtr->GetModule();
	TSharedPtr<FAngelscriptModuleDesc> ModuleDesc;
	for (auto Elem : ActiveModules)
	{
		if (Elem.Value->ScriptModule == ScriptModule)
		{
			ModuleDesc = Elem.Value;
			break;
		}
	}
	if (!ModuleDesc.IsValid())
	{
		ensureMsgf(false, TEXT("Could not find compiled module."));
		return;
	}

	int32 LineNumber = 1;
	auto ClassDesc = ModuleDesc->GetClass(ScriptTypePtr);
	if (ClassDesc.IsValid())
	{
		LineNumber = ClassDesc->LineNumber;

		if (FunctionName.Len() != 0)
		{
			auto MethodDesc = ClassDesc->GetMethod(FunctionName);
			if (!MethodDesc.IsValid())
				MethodDesc = ClassDesc->GetMethodByScriptName(FunctionName);
			if (MethodDesc.IsValid())
				LineNumber = MethodDesc->LineNumber;
		}
	}

	ScriptCompileError(ModuleDesc, LineNumber, Message, bIsError);
}

void LogAngelscriptError(asSMessageInfo* Message, void* DataPtr)
{
	static FString PreviousSection;
	static int32 PreviousType;

	auto& Manager = FAngelscriptManager::Get();
	if (Manager.bIgnoreCompileErrorDiagnostics)
		return;

	// Some compilation steps can happen on different threads, so we need to lock sending messages
	FScopeLock MessageLock(&Manager.CompilationLock);

	bool bPrintSection = false;
	if (Message->section)
	{
		if (PreviousSection != Message->section || PreviousType != Message->type)
		{
			PreviousSection = Message->section;
			PreviousType = Message->type;

			bPrintSection = true;
		}
	}

	FString ErrorMessage;
	if (Message->col || Message->row)
	{
		ErrorMessage = FString::Printf(TEXT("(%d:%d): %s"),
			Message->row, Message->col,
			ANSI_TO_TCHAR(Message->message)
		);
	}
	else
	{
		ErrorMessage = Message->message;
	}

	FString Section = ANSI_TO_TCHAR(Message->section);

	if (Message->type == asMSGTYPE_INFORMATION)
	{
		if (bPrintSection)
		{
			UE_LOG(Angelscript, Log, TEXT("%s:"), *Section);
		}
		UE_LOG(Angelscript, Log, TEXT(" %s"), *ErrorMessage);
	}
	else if (Message->type == asMSGTYPE_ERROR)
	{
		if (bPrintSection)
		{
			UE_LOG(Angelscript, Error, TEXT("%s:"), *Section);
		}
		UE_LOG(Angelscript, Error, TEXT(" %s"), *ErrorMessage);
	}
	else
	{
		if (bPrintSection)
		{
			UE_LOG(Angelscript, Warning, TEXT("%s:"), *Section);
		}
		UE_LOG(Angelscript, Warning, TEXT(" %s"), *ErrorMessage);
	}

	// Check if this message should be captured as a diagnostic
	auto* FileDiagnostics = Manager.Diagnostics.Find(Section);
	if (FileDiagnostics != nullptr)
	{
		FileDiagnostics->Diagnostics.Add({ ANSI_TO_TCHAR(Message->message), Message->row, Message->col,
			Message->type == asMSGTYPE_ERROR, Message->type == asMSGTYPE_INFORMATION });
		Manager.bDiagnosticsDirty = true;
	}
}

void GetStackTrace(TArray<FString>& OutTrace)
{
	auto* tld = asCThreadManager::GetLocalData();
	asCContext* Context = nullptr;

	struct FStackFrameDescription
	{
		FString Frame;
		FString Module;
		UObject* ThisObject;
	};

	TArray<FStackFrameDescription, TInlineAllocator<16>> Stack;

	if (tld->activeExecution != nullptr)
	{
		FScriptExecution* Execution = tld->activeExecution;
		while (Execution != nullptr)
		{
#if AS_JIT_DEBUG_CALLSTACKS
			auto* DebugStack = (FScopeJITDebugCallstack*)Execution->debugCallStack;
			while (DebugStack != nullptr)
			{
				auto* ThisObject = (UObject*)DebugStack->ThisObject;

				Stack.Add({
					FString::Printf(TEXT("  %s | Line %d"),
									ANSI_TO_TCHAR(DebugStack->FunctionName),
									DebugStack->LineNumber),
					ANSI_TO_TCHAR(DebugStack->Filename),
					ThisObject
				});

				DebugStack = DebugStack->PrevFrame;
			}
#endif

			Context = Execution->prevContext;
			Execution = Execution->prevExecution;
		}
	}
	else
	{
		Context = tld->activeContext;
		if (Context == nullptr)
		{
			OutTrace.Add(TEXT("No Angelscript Context"));
			return;
		}
	}

	if (Context != nullptr)
	{
		int32 FrameCount = FMath::Min((int32)Context->GetCallstackSize(), 64);
		for (int32 i = 0; i < FrameCount; ++i)
		{
			asIScriptFunction* ScriptFunction = Context->GetFunction(i);
			if (ScriptFunction != nullptr)
			{
				int32 Line, Column;
				Line = Context->GetLineNumber(i, &Column, nullptr);

				FStackFrameDescription& Desc = Stack.Emplace_GetRef();
				Desc.Frame = FString::Printf(TEXT("  %s | Line %d | Col %d"),
					ANSI_TO_TCHAR(ScriptFunction->GetDeclaration(true, false, false, true)),
					Line, Column);
				Desc.Module = ANSI_TO_TCHAR(ScriptFunction->GetModuleName());
				Desc.ThisObject = nullptr;

				int ThisTypeId = Context->GetThisTypeId(i);
				if (ThisTypeId != 0)
				{
					auto* ThisType = Context->GetEngine()->GetTypeInfoById(ThisTypeId);
					if (ThisType != nullptr && (ThisType->GetFlags() & asOBJ_REF) != 0)
					{
						// All ref objects are UObjects
						UObject* ThisPtr = (UObject*)Context->GetThisPointer(i);
						Desc.ThisObject = ThisPtr;
					};
				}
			}
		}
	}

	AActor* PreviousThisActor = nullptr;
	OutTrace.Reserve(Stack.Num());

	for (int32 i = Stack.Num() - 1; i >= 0; --i)
	{
		// All ref objects are UObjects
		UObject* ThisPtr = Stack[i].ThisObject;
		AActor* ThisActor = nullptr;

		// Find the actor that contains the object we're in
		while (ThisPtr != nullptr)
		{
			if (Cast<UPackage>(ThisPtr) != nullptr)
			{
				break;
			}
			if (Cast<AActor>(ThisPtr) != nullptr)
			{
				ThisActor = CastChecked<AActor>(ThisPtr);
				break;
			}

			ThisPtr = ThisPtr->GetOuter();
		}

		// Display the found actor unless we already displayed it earlier
		if (ThisActor != nullptr && ThisActor != PreviousThisActor)
		{
			FString OuterStr;
			if (auto* InLevel = ThisActor->GetLevel())
			{
				if (auto* LevelWorld = InLevel->GetOuter())
				{
					OuterStr = FString::Printf(TEXT(" in %s"), *LevelWorld->GetName());
				}
			}

#if WITH_EDITOR
			OutTrace.Insert(FString::Printf(TEXT("    (Actor: %s (Label: %s)%s)"),
				*ThisActor->GetName(), *ThisActor->GetActorLabel(), *OuterStr), 0);
#else
			OutTrace.Insert(FString::Printf(TEXT("    (Actor: %s%s)"),
				*ThisActor->GetName(), *OuterStr), 0);
#endif
			PreviousThisActor = ThisActor;
		}

		OutTrace.Insert(MoveTemp(Stack[i].Frame), 0);

		// Show the module name on top of the stack
		if (i == 0)
			OutTrace.Insert(MoveTemp(Stack[i].Module), 0);
	}
}

void LogScriptStack()
{
	TArray<FString> Trace;
	GetStackTrace(Trace);

	for (FString& Line : Trace)
		UE_LOG(Angelscript, Warning, TEXT("%s"),*Line);
}

FString GetScriptStack()
{
	TArray<FString> Trace;
	GetStackTrace(Trace);
	return FString::Join(Trace, TEXT("\n"));
}

void LogAngelscriptException(const ANSICHAR* ExceptionString)
{
#if WITH_AS_DEBUGSERVER
	if (FAngelscriptManager::Get().IsEvaluatingDebuggerWatch())
		return;
#endif

	TGuardValue<bool> LineReentry(GAngelscriptLineReentry, true);

	if (ExceptionString == nullptr)
		ExceptionString = "NO EXCEPTION";

	UE_LOG(Angelscript, Error, TEXT("%s"), ANSI_TO_TCHAR(ExceptionString));

	TArray<FString> Trace;
	GetStackTrace(Trace);

	for (FString& Line : Trace)
		UE_LOG(Angelscript, Error, TEXT("%s"),*Line);

	// Print angelscript exceptions on screen
	if (GEngine != nullptr)
	{
		UKismetSystemLibrary::PrintString(
			FAngelscriptManager::CurrentWorldContext,
			FString::Printf(
				TEXT("Angelscript Exception: %s\n%s"),
				ANSI_TO_TCHAR(ExceptionString),
				Trace.Num() >= 2 ? *Trace[1] : TEXT("")),
			true, false,
			FLinearColor::Red, 30.f);
	}

#if !WITH_EDITOR && WITH_ANGELSCRIPT_HAZE
#if !DO_CHECK && UE_BUILD_TEST
	// In test builds we still want to show an error for the first exception
	static TSet<FString> ShownStacks;
	if (Trace.Num() >= 2 && !ShownStacks.Contains(Trace[1]))
	{
		ShownStacks.Add(Trace[1]);

		FText Title = FText::FromString(TEXT("Angelscript Exception"));
		FString Message;
		Message += FString::Printf(TEXT("Angelscript Exception: %s\n"), ANSI_TO_TCHAR(ExceptionString));
		Message += TEXT("\n\n");
		Message += FAngelscriptManager::FormatAngelscriptCallstack();
		Message += TEXT("\n\nFurther exceptions on this line will not show popups.");
		
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message), &Title);
	}
#else
	// In cooked, the first angelscript exception is a devEnsure,
	// that way we can see them more easily.
	devEnsure(false, TEXT("Angelscript Exception:\n\n%s"), ANSI_TO_TCHAR(ExceptionString));
#endif
#endif
}

void LogAngelscriptException(asIScriptContext* Context)
{
	const ANSICHAR* ExceptionString = Context->GetExceptionString();
	LogAngelscriptException(ExceptionString);

#if WITH_AS_DEBUGSERVER
	if (IsInGameThread())
	{
		if (auto* DebugServer = FAngelscriptManager::Get().DebugServer)
			DebugServer->ProcessException(Context);
	}
#endif
}

void FAngelscriptManager::HandleExceptionFromJIT(const ANSICHAR* ExceptionString)
{
	LogAngelscriptException(ExceptionString);
}

void FAngelscriptManager::TraceError(const ANSICHAR* Error)
{
	UE_LOG(Angelscript, Error, TEXT("%s"), ANSI_TO_TCHAR(Error));

	TArray<FString> Trace;
	GetStackTrace(Trace);

	for (FString& Line : Trace)
		UE_LOG(Angelscript, Error, TEXT("%s"),*Line);
}

FAngelscriptManager::FAngelscriptDebugStack& GetStack(asIScriptContext* Context)
{
	asCContext* Ctx = (asCContext*)Context;
	if (Ctx->DebugFramePtr == nullptr)
		Ctx->DebugFramePtr = new FAngelscriptManager::FAngelscriptDebugStack;
	return *(FAngelscriptManager::FAngelscriptDebugStack*)Ctx->DebugFramePtr;
}

FDebugValuePrototype* GetDebugPrototype(asIScriptFunction* Function)
{
	asCScriptFunction* Func = (asCScriptFunction*)Function;
	if (Func->DebugPrototypePtr != nullptr)
		return (FDebugValuePrototype*)Func->DebugPrototypePtr;
	if (Func->scriptData == nullptr)
		return nullptr;

	FDebugValuePrototype* Proto = new FDebugValuePrototype;

	int32 VarCount = Function->GetVarCount();
	for (int32 i = 0; i < VarCount; ++i)
	{
		const char* VarName;
		int VarTypeId;

		Function->GetVar(i, &VarName, &VarTypeId);

		FAngelscriptTypeUsage Type = FAngelscriptTypeUsage::FromTypeId(VarTypeId);
		if (!Type.IsValid())
			continue;

		int32 Offset = Func->scriptData->variables[i]->stackOffset;
		if( (Func->scriptData->variables[i]->type.IsObject() && !Func->scriptData->variables[i]->type.IsObjectHandle()) || (Offset <= 0) )
		{
			// Determine if the object is really on the heap
			bool onHeap = false;
			if( Func->scriptData->variables[i]->type.IsObject() &&
				!Func->scriptData->variables[i]->type.IsObjectHandle() )
			{
				onHeap = true;
				if( Func->scriptData->variables[i]->type.GetTypeInfo()->GetFlags() & asOBJ_VALUE )
				{
					for( asUINT n = 0; n < Func->scriptData->objVariablePos.GetLength(); n++ )
					{
						if( Func->scriptData->objVariablePos[n] == Offset )
						{
							onHeap = n < Func->scriptData->objVariablesOnHeap;
							break;
						}
					}
				}
			}

			// If it wasn't an object on the heap, then check if it is a reference parameter
			if( !onHeap && Offset <= 0 )
			{
				// Determine what function argument this position matches
				int stackPos = 0;
				if( Func->objectType )
					stackPos -= AS_PTR_SIZE;

				if( Func->DoesReturnOnStack() )
					stackPos -= AS_PTR_SIZE;

				for( asUINT n = 0; n < Func->parameterTypes.GetLength(); n++ )
				{
					if( stackPos == Offset )
					{
						// The right argument was found. Is this a reference parameter?
						if( Func->inOutFlags[n] != asTM_NONE )
							onHeap = true;
						break;
					}

					stackPos -= Func->parameterTypes[n].GetSizeOnStackDWords();
				}
			}

			// Heap variables are references on the stack
			if (onHeap)
				Type.bIsReference = true;
		}

		FASDebugValue* DebugValue = Type.CreateDebugValue(*Proto, -Offset * 4);
		if (DebugValue != nullptr)
			DebugValue->Name = FName(ANSI_TO_TCHAR(VarName));
	}

	Func->DebugPrototypePtr = Proto;
	return Proto;
}

FAngelscriptManager::FAngelscriptDebugFrame::~FAngelscriptDebugFrame()
{
#if WITH_AS_DEBUGVALUES
	if (Variables != nullptr)
		Prototype->Free(Variables);
#endif
}

void FAngelscriptManager::UpdateLineCallbackState()
{
	bool bEverRunLineCallback = false;
	bool bAlwaysRunLineCallback = false;

#if WITH_AS_DEBUGSERVER
	if (DebugServer != nullptr)
	{
		if (DebugServer->bIsDebugging)
			bEverRunLineCallback = true;
		if (DebugServer->DataBreakpoints.Num() != 0)
			bEverRunLineCallback = true;
		if (DebugServer->bBreakNextScriptLine)
			bAlwaysRunLineCallback = true;
	}
#endif

#if WITH_AS_COVERAGE
	if (CodeCoverage != nullptr)
	{
		bEverRunLineCallback = true;
		bAlwaysRunLineCallback = true;
	}
#endif

#if WITH_AS_DEBUGVALUES
	bEverRunLineCallback = true;
	bAlwaysRunLineCallback = true;
#endif

	asCContext::CanEverRunLineCallback = bEverRunLineCallback;
	asCContext::ShouldAlwaysRunLineCallback = bAlwaysRunLineCallback;
}

void AngelscriptLineCallback(asCContext* Context)
{
	// Only do this for things running on the game thread
	if (!IsInGameThread())
		return;

	// Guard for reentry on this function. Script called
	// inside of a line callback is not considered for line callbacks.
	if (GAngelscriptLineReentry)
		return;
	GAngelscriptLineReentry = true;

	FAngelscriptManager& AngelscriptManager = FAngelscriptManager::Get();

#if WITH_AS_DEBUGVALUES
	auto& Stack = GetStack(Context);
	GAngelscriptStack = &Stack;

	int32 StackSize = Context->GetCallstackSize();
	if (StackSize != Stack.Frames.Num()
		|| (StackSize != 0 && Stack.Frames[0].ScriptFunction != Context->GetFunction(0)))
	{
		Stack.Frames.SetNum(StackSize, false);

		for (int32 i = 0; i < StackSize; ++i)
		{
			auto& Frame = Stack.Frames[i];
			auto* ScriptFunction = Context->GetFunction(i);
			if (ScriptFunction == Frame.ScriptFunction)
				continue;

			Frame.ScriptFunction = Context->GetFunction(i);
			Frame.LineNumber = Context->GetLineNumber(i, nullptr, &Frame.File);

			if (Frame.Prototype && Frame.Variables)
			{
				Frame.Prototype->Free(Frame.Variables);
				Frame.Variables = nullptr;
			}

			if (Frame.ScriptFunction != nullptr)
			{
				Frame.Function = Frame.ScriptFunction->GetName();
				auto* ScriptClass = Frame.ScriptFunction->GetObjectType();
				Frame.Class = ScriptClass ? ScriptClass->GetName() : nullptr;

				Frame.Prototype = GetDebugPrototype(Frame.ScriptFunction);
				if (Frame.Prototype != nullptr)
				{
					Frame.Variables = (FDebugValues*)Frame.Prototype->Instantiate(
						((asCContext*)Context)->GetStackFrame(i)
					);
				}
			}
			else
			{
				Frame.Function = nullptr;
				Frame.Class = nullptr;
				Frame.Prototype = nullptr;
			}

			Frame.This = (UObject*)Context->GetThisPointer(i);
		}
	}
	else if(StackSize != 0)
	{
		auto& Frame = Stack.Frames[0];
		Frame.LineNumber = Context->GetLineNumber(0, nullptr, nullptr);
	}
#endif

#if WITH_AS_DEBUGSERVER
	if (auto* DebugServer = AngelscriptManager.DebugServer)
		DebugServer->ProcessScriptLine(Context);
#endif

#if WITH_AS_COVERAGE
	if (AngelscriptManager.CodeCoverage != nullptr)
	{
		int Column;
		int Line = Context->GetLineNumber(0, &Column, nullptr);
		asIScriptFunction* CurrentFunction = Context->GetFunction(0);
		FString ModuleName = ANSI_TO_TCHAR(CurrentFunction->GetModuleName());
		TSharedPtr<struct FAngelscriptModuleDesc> Module = AngelscriptManager.GetModule(ModuleName);
		if (Module != nullptr)
		{
			AngelscriptManager.CodeCoverage->HitLine(*Module, Line);
		}
	}
#endif

	GAngelscriptLineReentry = false;
}

void AngelscriptStackPopCallback(asCContext* Context, void* OldStackFrameStart, void* OldStackFrameEnd)
{
#if WITH_AS_DEBUGSERVER
	FAngelscriptManager& AngelscriptManager = FAngelscriptManager::Get();
	if (auto* DebugServer = AngelscriptManager.DebugServer)
		DebugServer->ProcessScriptStackPop(Context, OldStackFrameStart, OldStackFrameEnd);
#endif
}

void AngelscriptLoopDetectionCallback(asCContext* Context)
{
	float MaximumScriptExecutionTime = UAngelscriptSettings::Get().EditorMaximumScriptExecutionTime;
	if (MaximumScriptExecutionTime > 0)
	{
		if (Context->m_loopDetectionExclusionCounter != 0)
			return;

		// Loop detection triggers every 100,000 executed lines of script code or so,
		// and should kill script functions that run for too long.
		// Note that loop detection won't happen in release builds.
		double CurrentTime = FPlatformTime::Seconds();
		if (Context->m_loopDetectionTimer == -1.0)
		{
			// No time has been established for this context yet, so set it and see if we time out later
			Context->m_loopDetectionTimer = CurrentTime;
			return;
		}

		if (Context->m_loopDetectionTimer < CurrentTime - MaximumScriptExecutionTime)
		{
			Context->SetException("Script function took too long to execute. Potentially an infinite loop? (timeout controlled by EditorMaximumScriptExecutionTime setting)");
			return;
		}
	}
}

#if WITH_EDITOR
FAngelscriptExcludeScopeFromLoopTimeout::FAngelscriptExcludeScopeFromLoopTimeout()
{
	Context = (asCContext*)asGetActiveContext();
	if (Context != nullptr)
	{
		Context->m_loopDetectionExclusionCounter += 1;
		StartTime = FPlatformTime::Seconds();
	}
}

FAngelscriptExcludeScopeFromLoopTimeout::~FAngelscriptExcludeScopeFromLoopTimeout()
{
	if (Context != nullptr)
	{
		Context->m_loopDetectionExclusionCounter -= 1;

		// If the scope took 1 second we remove 1 second from the timeout
		if (Context->m_loopDetectionExclusionCounter == 0 && Context->m_loopDetectionTimer != -1.0)
		{
			double NowTime = FPlatformTime::Seconds();
			Context->m_loopDetectionTimer = FMath::Min(Context->m_loopDetectionTimer + (NowTime - StartTime), NowTime);
		}
	}
}
#endif

TArray<FString> FAngelscriptManager::GetAngelscriptCallstack()
{
	TArray<FString> Trace;
	GetStackTrace(Trace);
	return Trace;
}

FString FAngelscriptManager::FormatAngelscriptCallstack()
{
	return GetScriptStack();
}

FString FAngelscriptManager::GetAngelscriptExecutionPosition()
{
	auto* tld = asCThreadManager::GetLocalData();
	if (tld->activeExecution != nullptr)
	{
#if AS_JIT_DEBUG_CALLSTACKS
		auto* DebugStack = (FScopeJITDebugCallstack*)tld->activeExecution->debugCallStack;
		if (DebugStack == nullptr)
			return TEXT("");

		return FString::Printf(TEXT("%s::%d"),
			ANSI_TO_TCHAR(DebugStack->Filename),
			DebugStack->LineNumber);
#else
		return TEXT("");
#endif
	}
	else
	{
		auto* Context = asGetActiveContext();
		if (Context == nullptr)
			return TEXT("");

		if (Context->GetCallstackSize() == 0)
			return TEXT("");

		const char* Filename;
		int32 LineNumber = Context->GetLineNumber(0, nullptr, &Filename);

		return FString::Printf(TEXT("%s::%d"), ANSI_TO_TCHAR(Filename), LineNumber);
	}
}

void FAngelscriptManager::GetAngelscriptExecutionFileAndLine(FString& OutFilename, int& OutLineNumber)
{
	auto* tld = asCThreadManager::GetLocalData();
	if (tld->activeExecution != nullptr)
	{
#if AS_JIT_DEBUG_CALLSTACKS
		auto* DebugStack = (FScopeJITDebugCallstack*)tld->activeExecution->debugCallStack;
		if (DebugStack == nullptr)
		{
			OutFilename = ANSI_TO_TCHAR(DebugStack->Filename);
			OutLineNumber = DebugStack->LineNumber;
			return;
		}
#endif

		OutFilename = TEXT("");
		OutLineNumber = -1;
	}
	else
	{
		auto* Context = asGetActiveContext();
		if (Context == nullptr)
			return;

		if (Context->GetCallstackSize() == 0)
			return;

		const char* Filename;
		int32 LineNumber = Context->GetLineNumber(0, nullptr, &Filename);

		OutFilename = ANSI_TO_TCHAR(Filename);
		OutLineNumber = LineNumber;
	}
}

UObject* FAngelscriptManager::GetAngelscriptExecutionThisObject(int32 StackFrame)
{
	auto* Context = asGetActiveContext();
	if (Context == nullptr)
		return nullptr;

	void* ThisPtr = Context->GetThisPointer(StackFrame);
	if (ThisPtr != nullptr)
	{
		asITypeInfo* ThisType = Context->GetEngine()->GetTypeInfoById(Context->GetThisTypeId(StackFrame));
		if (ThisType != nullptr && (ThisType->GetFlags() & asOBJ_REF) != 0)
		{
			return (UObject*)ThisPtr;
		}
	}
	return nullptr;
}

bool FAngelscriptManager::TryBreakpointAngelscriptDebugging(const TCHAR* Message)
{
#if WITH_AS_DEBUGSERVER
	auto& Manager = FAngelscriptManager::Get();
	if (Manager.DebugServer == nullptr)
		return false;
	if (!Manager.DebugServer->bIsDebugging)
		return false;
	if (Manager.DebugServer->bIsPaused)
		return false;

	auto* Context = asGetActiveContext();
	if (Context == nullptr)
		return false;

	FStoppedMessage StopMessage;
	if (Message != nullptr)
	{
		StopMessage.Reason = TEXT("exception");
		StopMessage.Text = Message;
	}
	else
	{
		StopMessage.Reason = TEXT("breakpoint");
	}

	Manager.DebugServer->PauseExecution(&StopMessage);
	return true;
#else
	return false;
#endif
}


UStruct* FAngelscriptManager::GetUnrealStructFromAngelscriptTypeId(int TypeId)
{
	auto* TypeInfo = (asCTypeInfo*)Engine->GetTypeInfoById(TypeId);
	if (TypeInfo == nullptr)
		return nullptr;
	if (TypeInfo->GetSubTypeCount() != 0)
		return nullptr;
	void* UserData = (void*)TypeInfo->plainUserData;
	if (UserData == FAngelscriptType::TAG_UserData_Delegate)
		return nullptr;
	if (UserData == FAngelscriptType::TAG_UserData_Multicast_Delegate)
		return nullptr;
	if ((TypeInfo->flags & asOBJ_ENUM) != 0)
		return nullptr;
	return (UStruct*)UserData;
}

#if AS_PRINT_STATS
FAngelscriptScopeTimer::FAngelscriptScopeTimer(const TCHAR* InName)
	: StartTime(FPlatformTime::Seconds())
	, Name(InName)
{
}

FAngelscriptScopeTimer::~FAngelscriptScopeTimer()
{
	double EndTime = FPlatformTime::Seconds();
	OutputTime(*Name, EndTime - StartTime);
}

void FAngelscriptScopeTimer::OutputTime(const TCHAR* Name, double Time)
{
	UE_LOG(Angelscript, Log, TEXT("%s took %.3f ms"), Name, Time * 1000);
}
#endif
#if AS_PRINT_STATS && AS_PRECOMPILED_STATS
FAngelscriptScopeTotalTimer::FAngelscriptScopeTotalTimer(double& TotalTime)
	: Timer(&TotalTime)
	, StartTime(FPlatformTime::Seconds())
{
}

FAngelscriptScopeTotalTimer::~FAngelscriptScopeTotalTimer()
{
	double EndTime = FPlatformTime::Seconds();
	*Timer += (EndTime - StartTime);
}
#endif

// Copied from FPaths::MakePathRelativeTo, but with a case-insensitive equals
bool MakePathRelativeTo_IgnoreCase( FString& InPath, const TCHAR* InRelativeTo )
{
	FString Target = FPaths::ConvertRelativePathToFull(InPath);
	FString Source = FPaths::ConvertRelativePathToFull(InRelativeTo);
	
	Source = FPaths::GetPath(Source);
	Source.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);
	Target.ReplaceInline(TEXT("\\"), TEXT("/"), ESearchCase::CaseSensitive);

	TArray<FString> TargetArray;
	Target.ParseIntoArray(TargetArray, TEXT("/"), true);
	TArray<FString> SourceArray;
	Source.ParseIntoArray(SourceArray, TEXT("/"), true);

	if (TargetArray.Num() && SourceArray.Num())
	{
		// Check for being on different drives
		if ((TargetArray[0][1] == TEXT(':')) && (SourceArray[0][1] == TEXT(':')))
		{
			if (FChar::ToUpper(TargetArray[0][0]) != FChar::ToUpper(SourceArray[0][0]))
			{
				// The Target and Source are on different drives... No relative path available.
				return false;
			}
		}
	}

	while (TargetArray.Num() && SourceArray.Num() && TargetArray[0].Equals(SourceArray[0], ESearchCase::IgnoreCase))
	{
		TargetArray.RemoveAt(0);
		SourceArray.RemoveAt(0);
	}
	FString Result;
	for (int32 Index = 0; Index < SourceArray.Num(); Index++)
	{
		Result += TEXT("../");
	}
	for (int32 Index = 0; Index < TargetArray.Num(); Index++)
	{
		Result += TargetArray[Index];
		if (Index + 1 < TargetArray.Num())
		{
			Result += TEXT("/");
		}
	}
	
	InPath = Result;
	return true;
}

double asStringScanDouble(const char *string)
{
	return FCStringAnsi::Atod(string);
}

float asStringScanFloat(const char *string)
{
	return FCStringAnsi::Atof(string);
}

static bool asStringEquals(const asCString& ASString, const FString& UnrealString)
{
	int32 Length = UnrealString.Len();
	if (Length != ASString.GetLength())
		return false;

	const auto* APtr = ASString.AddressOf();
	const auto* BPtr = *UnrealString;

	for (int32 i = 0; i < Length; ++i)
	{
		if (APtr[i] != BPtr[i])
			return false;
	}

	return true;
}

TSharedPtr<FAngelscriptPropertyDesc> FAngelscriptClassDesc::GetProperty(asCString& PropName)
{
	int32 Length = PropName.GetLength();
	for (auto PropDesc : Properties)
	{
		if (asStringEquals(PropName, PropDesc->PropertyName))
			return PropDesc;
	}

	return nullptr;
}

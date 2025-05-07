using System.IO;
using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
	public class AngelscriptNativeBinds : ModuleRules
	{
		public AngelscriptNativeBinds(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
            OptimizeCode = CodeOptimization.Never;

			PublicDependencyModuleNames.AddRange
			(
				new string[]
				{
					"Core",
					"CoreUObject",
					"Engine",
					"AngelscriptCode",
				}
			);
			PrivateDependencyModuleNames.AddRange
			(
				new string[]
				{
                    //"AngelscriptCode",
					"MovieSceneTracks",
                    "Engine",
                    "LevelSequence",
                    "MovieSceneTracks",
                    "AIModule",
                    "GameplayTasks",
                    "GameplayTags",
                    "NavigationSystem",
                    "Constraints",
                    "MovieSceneCapture",
                    "Landscape",
                    "Foliage",
                    "CinematicCamera",
                    "UMG",
                    "SlateRHIRenderer",
                    "InterchangeEngine",
                    "ClothingSystemRuntimeInterface",
                    "InterchangeCore",
                    "AnimGraphRuntime",
                    "GeometryCollectionEngine",
                    "DataflowEngine",
                    "FieldSystemEngine",
                    "ChaosSolverEngine",
                    "StaticMeshDescription",
                    "MovieScene",
                    "TimeManagement",
                    "MediaAssets",
                    "AudioMixer",
                    "HeadMountedDisplay",
                    "EngineSettings",
                    "AssetRegistry",
                    "ImageWriteQueue",
                    "MeshDescription",
                    "TypedElementRuntime",
                    "TypedElementFramework",
                    "ImageCore",
                    "MRMesh",
                    "EyeTracker",
                    "FunctionalTesting",
                    "SourceControl",
                    "ToolMenus",
                    "StructUtilsEngine",
                    "StateTreeModule",
                    "GameplayStateTreeModule",
                    "DataRegistry",
                    "Niagara",
                    "NiagaraAnimNotifies",
                    "AudioAnalyzer",
                    "AdvancedWidgets",
                    "GameplayAbilities",
                    "TargetingSystem",
                    "ModularGameplay",
                    "GameFeatures",
                    "GooglePAD",
                    "AssetTags",
                    "VariantManagerContent",
                    "GLTFExporter",
                    "AnimationLocomotionLibraryRuntime",
                    "GeometryCache",
                    "LocalizableMessageBlueprint",
                    "MotionWarping",
                    "AnimationWarpingRuntime",
                    "MetasoundFrontend",
                    "MetasoundEngine",
                    "ActorSequence",
                    "TemplateSequence",
                    "GameplayCameras",
                    "RigVM",
                    "SequencerScripting",
                    //"RigVMDeveloper",
                    "ControlRig",
                    "AnimationSharing",
                    "OnlineSubsystemUtils",
                    "GameplayBehaviorsModule",
                    "GeometryFramework",
                    "ModelingComponents",
                    "GeometryScriptingCore",
                    "AndroidPermission",
                    "TraceUtilities",
                    "ProceduralMeshComponent",
                    "ResonanceAudio",
                    "EnhancedInput",
                    "DatasmithContent",
                    "AudioCapture",
                    "AudioSynesthesia",
                    "AudioWidgets",
                    "LocationServicesBPLibrary",
                    "CustomMeshComponent",
                    "CableComponent",
                    "InterchangeNodes",
                    "InterchangeFactoryNodes",
                    "InterchangePipelines",
                    "MediaPlate",
                    "ImgMedia",
                    //"ControlRigDeveloper",
                    "IKRig",
                    "Paper2D",
                    "ChaosCaching",
                    "ChaosCloth",
                    //"TakesCore",
                    //"TakeRecorder",
                    "AutomationUtils",
                    "Synthesis",
                    //"AnimationModifierLibrary",
                    //"CacheTrackRecorder",
                    //"SkeletalMeshModifiers",
                    //"InterchangeTests",
                }
            );
			
			//string PluginPath = "../Plugins/Angelscript";			
			//PrivateIncludePaths.Add(PluginPath + "/ASBinds");            
        }
	}
}

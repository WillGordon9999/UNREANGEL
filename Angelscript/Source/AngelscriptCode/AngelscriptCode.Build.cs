using System.IO;
using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
	public class AngelscriptCode : ModuleRules
	{
		public AngelscriptCode(ReadOnlyTargetRules Target) : base(Target)
		{
			PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

			/* Link to libraries used in core angelscript code */
			PublicDependencyModuleNames.AddRange(new string[]
			{
				"ApplicationCore",
				"Core",
				"CoreUObject",
				"Engine",
				"EngineSettings",
				"DeveloperSettings",
				"Json",
				"JsonUtilities",
				"GameplayTags",
				//WILL-EDIT
				
            });

			/* Link to libraries used in bindings */
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"AIModule",
				"NavigationSystem",
				"NetCore",
				"Landscape",
				"Networking",
				"Sockets",
				"InputCore",
				"SlateCore",
				"Slate",
				"UMG",
				"TraceLog",
				"AssetRegistry",
				"Projects",
				"PhysicsCore",
				"CoreOnline",
				"EnhancedInput",
				"GameplayAbilities",				
				"GameplayTasks",
				//"TargetingSystem",
				//"ResonanceAudio",
				//"TraceUtilities",
				//"GameFeatures",
				//"AndroidPermission",
				//"GeometryScriptingCore",
				//"GeometryFramework",
				//"AnimGraphRuntime",
				//"MRMesh",
				//"GameplayBehaviorsModule",
				//"OnlineSubsystemUtils",
				//"AnimationSharing",
				//"ControlRig",				
				//"GameplayCameras",
				//"ActorSequence",
				//"MetasoundEngine",
				//"AnimationWarpingRuntime",
				//"VariantManagerContent",
				//"MotionWarping",
				//"ModelingComponents",
				//"LocalizableMessageBlueprint",
				//"GeometryCache",
                //"AnimationLocomotionLibraryRuntime",
				//"LevelSequence",
				//"MediaAssets",
                //"GLTFExporter",
                //"NiagaraAnimNotifies",
                //"AnimationModifierLibrary",
				//"AssetTags",
				//"GooglePAD",
				//"SkeletalMeshModifiers",
                //"MovieSceneTracks",
            });

			if (Target.bBuildEditor)
			{
				PublicDependencyModuleNames.AddRange(new string[] 
				{
					"UnrealEd",
					"EditorSubsystem",
				});

				PrivateDependencyModuleNames.AddRange(new string[]
				{
					"UMGEditor",
				});
			}

            //var PluginPath = "../Plugins/Angelscript";
            //var PluginPath = "./Plugins/Angelscript";
            //var PluginPath = "./";

			/* Link to Angelscript */
			//PublicIncludePaths.Add(PluginPath + "/ThirdParty/include");
			//PublicIncludePaths.Add(PluginPath + "/ThirdParty/source");
		}
	}
}

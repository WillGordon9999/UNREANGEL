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
                }
            );
			
			//string PluginPath = "../Plugins/Angelscript";			
			//PrivateIncludePaths.Add(PluginPath + "/ASBinds");            
        }
	}
}

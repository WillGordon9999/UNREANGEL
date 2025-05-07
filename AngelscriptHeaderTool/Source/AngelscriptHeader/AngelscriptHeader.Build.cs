using System.IO;
using UnrealBuildTool;

namespace UnrealBuildTool.Rules
{
    public class AngelscriptHeader : ModuleRules
    {
        public AngelscriptHeader(ReadOnlyTargetRules Target) : base(Target)
        {
            PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

            PublicDependencyModuleNames.AddRange
            (
                new string[]
                {
                "Core",
                "CoreUObject"
                }
            );

            PrivateDependencyModuleNames.AddRange
            (
                new string[]
                {
                "Core",
                "CoreUObject"
                }
            );
        }
    }
}


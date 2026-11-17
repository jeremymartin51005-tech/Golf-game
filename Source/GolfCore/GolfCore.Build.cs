using UnrealBuildTool;

// Engine-independent simulation core (plain C++17, std library only). It is compiled and unit-tested
// outside Unreal by CoreHarness/CMakeLists.txt; this file only tells UnrealBuildTool how to build
// the same sources as a module.
public class GolfCore : ModuleRules
{
	public GolfCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseUnity = false;                 // files use same-named helpers in anonymous namespaces
		PublicDependencyModuleNames.Add("Core");
	}
}

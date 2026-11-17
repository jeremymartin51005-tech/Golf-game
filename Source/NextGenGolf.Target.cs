using UnrealBuildTool;

public class NextGenGolfTarget : TargetRules
{
	public NextGenGolfTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "NextGenGolf", "GolfCore" });
	}
}

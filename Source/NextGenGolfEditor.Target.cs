using UnrealBuildTool;

public class NextGenGolfEditorTarget : TargetRules
{
	public NextGenGolfEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "NextGenGolf", "GolfCore" });
	}
}

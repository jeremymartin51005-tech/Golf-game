// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#include "Settings/GolfGraphicsPresets.h"
#include "HAL/IConsoleManager.h"

namespace
{
	struct FPresetVar { const TCHAR* Name; float Values[3]; };   // Performance, Balanced, Quality

	const FPresetVar GPresetVars[] = {
		{TEXT("r.DynamicGlobalIlluminationMethod"), {0, 0, 1}},   // Lumen GI only on Quality
		{TEXT("r.ReflectionMethod"),               {0, 2, 1}},   // none / screen-space / Lumen
		{TEXT("r.Shadow.Virtual.Enable"),          {0, 0, 1}},   // cascaded shadow maps unless Quality
		{TEXT("r.Shadow.CSM.MaxCascades"),         {2, 3, 4}},
		{TEXT("r.ScreenPercentage"),               {60, 75, 100}},
		{TEXT("r.AntiAliasingMethod"),             {2, 4, 4}},   // TAA on Performance, TSR otherwise
		{TEXT("foliage.DensityScale"),             {0.4f, 0.7f, 1.0f}},
		{TEXT("grass.DensityScale"),               {0.3f, 0.6f, 1.0f}},
		{TEXT("r.ViewDistanceScale"),              {0.6f, 0.8f, 1.0f}},
		{TEXT("r.Streaming.PoolSize"),             {1500, 2500, 4000}},
	};
}

void UGolfGraphicsPresets::ApplyPreset(EGolfGraphicsPreset Preset)
{
	const int32 Index = int32(Preset);
	for (const FPresetVar& Var : GPresetVars)
	{
		if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Var.Name))
		{
			CVar->Set(Var.Values[Index], ECVF_SetByGameSetting);
		}
	}
}

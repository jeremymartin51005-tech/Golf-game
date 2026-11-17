// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GolfGraphicsPresets.generated.h"

UENUM(BlueprintType)
enum class EGolfGraphicsPreset : uint8 { Performance, Balanced, Quality };

/**
 * Starting hypotheses for the three presets on Apple Silicon. NONE of these values has been profiled
 * yet; each must be confirmed or changed from measurements on real MacBooks (see docs/ROADMAP.md).
 */
UCLASS()
class NEXTGENGOLF_API UGolfGraphicsPresets : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Golf|Graphics")
	static void ApplyPreset(EGolfGraphicsPreset Preset);
};

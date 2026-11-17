// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "golf/Round.h"
#include "GolfRoundSubsystem.generated.h"

/** Owns the current course and the hole in play. One per game instance; survives level changes. */
UCLASS()
class NEXTGENGOLF_API UGolfRoundSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	/** Environment 0-5 (Parkland, Highlands, Coastal, Alpine, Desert, Links); Difficulty 0-3; Tee 0-3. */
	UFUNCTION(BlueprintCallable, Category = "Golf|Course") bool GenerateRandomNine(int64 Seed, int32 Environment, int32 Difficulty, int32 TeeIndex);
	UFUNCTION(BlueprintCallable, Category = "Golf|Course") bool GenerateFromShareCode(const FString& Code);
	UFUNCTION(BlueprintCallable, Category = "Golf|Course") bool GenerateDaily();
	UFUNCTION(BlueprintCallable, Category = "Golf|Course") bool StartHole(int32 HoleIndex);

	UFUNCTION(BlueprintPure, Category = "Golf|Course") int32 GetHoleCount() const { return int32(Course.holes.size()); }
	UFUNCTION(BlueprintPure, Category = "Golf|Course") int32 GetCurrentHole() const { return CurrentHole; }
	UFUNCTION(BlueprintPure, Category = "Golf|Course") FString GetCourseName() const { return FString(UTF8_TO_TCHAR(Course.name.c_str())); }
	UFUNCTION(BlueprintPure, Category = "Golf|Course") FString GetShareCode() const;
	UFUNCTION(BlueprintPure, Category = "Golf|Round") int32 GetPar() const { return Play ? Play->result().par : 0; }
	UFUNCTION(BlueprintPure, Category = "Golf|Round") int32 GetStrokes() const { return Play ? Play->result().strokes : 0; }
	UFUNCTION(BlueprintPure, Category = "Golf|Round") bool IsHoled() const { return Play && Play->holed(); }
	UFUNCTION(BlueprintPure, Category = "Golf|Round") float GetDistanceToPinMetres() const { return Play ? float(Play->distanceToPin()) : 0.f; }

	const golf::Course& GetCourse() const { return Course; }
	golf::HolePlay* GetPlay() { return Play.Get(); }
	const golf::HoleLayout* GetHole() const { return CurrentHole >= 0 ? &Course.holes[size_t(CurrentHole)] : nullptr; }

private:
	bool Adopt(golf::Course&& NewCourse);

	golf::Course Course;
	TUniquePtr<golf::HolePlay> Play;
	int32 CurrentHole = -1;
};

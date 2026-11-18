// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "golf/Round.h"
#include "GolfPlayerPawn.generated.h"

class AGolfHoleActor;
class UCameraComponent;
class UGolfShotComponent;
class UStaticMeshComponent;
class SGolfSwingMeter;

/**
 * Minimal playable loop for the first in-engine milestone: generates a course if none exists, builds
 * the hole, puts a camera behind the ball and wires the mouse swing. Hold the left mouse button to
 * swing, A/D to aim, W/S to change club. Set as the Default Pawn Class of the game mode.
 */
UCLASS()
class NEXTGENGOLF_API AGolfPlayerPawn : public APawn
{
	GENERATED_BODY()
public:
	AGolfPlayerPawn();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* Input) override;

	UPROPERTY(VisibleAnywhere, Category = "Golf") TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(EditAnywhere, Category = "Golf") TSubclassOf<AGolfHoleActor> HoleActorClass;
	UPROPERTY(EditAnywhere, Category = "Golf") int32 SwingDifficulty = 0;

private:
	void BeginHole(int32 Index);
	void PrepareStroke();
	void SwingPressed();
	void SwingReleased();
	void ClubUp();
	void ClubDown();
	void AimLeftPressed();
	void AimRightPressed();
	void AimReleased();
	UFUNCTION() void HandleBallAtRest();

	UPROPERTY() TObjectPtr<AActor> Ball;
	UPROPERTY() TObjectPtr<UGolfShotComponent> Shot;
	UPROPERTY() TObjectPtr<AGolfHoleActor> HoleActor;
	TSharedPtr<SGolfSwingMeter> Meter;
	float ZonePower = 1.f;
	void UpdateMeter();
	UFUNCTION() void HandleShotStruck(float CarryMetres);
	std::vector<golf::ClubId> Bag;
	int32 ClubIndex = 0;
	double AimDeg = 0.0;
	float AimInput = 0.f;
};

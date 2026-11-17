// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "golf/Round.h"
#include "GolfShotComponent.generated.h"

class FGolfMouseSampler;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGolfShotStruck, float, CarryMetres);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGolfBallAtRest);

/**
 * Glue between input, the simulation core and the visible ball (its owning actor).
 * The whole shot is simulated the instant the club meets the ball (well under a millisecond), then
 * played back by interpolation. Per-frame cost is one lerp, cameras know the landing spot in advance,
 * and replays are just the stored path. Rules (penalties, drops, scoring) are applied by golf::HolePlay,
 * exactly as in the browser client.
 */
UCLASS(ClassGroup = Golf, meta = (BlueprintSpawnableComponent))
class NEXTGENGOLF_API UGolfShotComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGolfShotComponent();

	UPROPERTY(BlueprintAssignable) FGolfShotStruck OnShotStruck;
	UPROPERTY(BlueprintAssignable) FGolfBallAtRest OnBallAtRest;

	/** Arms the mouse swing for the next stroke of Play. Play must outlive the stroke. */
	void BeginSwing(golf::HolePlay* Play, golf::ClubId Club, const golf::ShotIntent& Intent, double AimDeg,
	                const golf::SwingSettings& Settings, const golf::PlayerAttributes& Attributes);
	void CancelSwing();

	UFUNCTION(BlueprintPure, Category = "Golf") float GetLivePower() const { return float(Swing.livePower()); }
	UFUNCTION(BlueprintPure, Category = "Golf") bool IsArmed() const { return bArmed; }
	UFUNCTION(BlueprintPure, Category = "Golf") bool IsBallMoving() const { return bPlaying; }
	const golf::SwingResult& GetSwingResult() const { return LastSwing; }
	const golf::ShotOutcome& GetOutcome() const { return Outcome; }

	/** Simulation metres (X right, Y downrange, Z up) to Unreal centimetres (X forward, Y right, Z up). */
	static FVector ToUnreal(const golf::Vec3& P) { return FVector(P.y * 100.0, P.x * 100.0, P.z * 100.0); }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void Strike();

	TSharedPtr<FGolfMouseSampler> Sampler;
	golf::MouseSwing Swing;
	golf::SwingResult LastSwing;
	golf::ShotOutcome Outcome;
	golf::HolePlay* ActivePlay = nullptr;
	golf::ClubId ActiveClub = golf::ClubId::Driver;
	golf::ShotIntent ActiveIntent;
	golf::SwingSettings ActiveSettings;
	golf::PlayerAttributes ActiveAttributes;
	double ActiveAim = 0.0;
	double PlaybackTime = 0.0;
	int32 PlaybackIndex = 0;
	bool bArmed = false;
	bool bPlaying = false;
};

// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "golf/BallFlight.h"
#include "GolfShotComponent.generated.h"

class FGolfMouseSampler;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGolfShotStruck, float, CarryMetres);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGolfBallAtRest);

/**
 * Glue between input, the simulation core and the visible ball.
 * The whole shot is simulated the instant the club meets the ball (well under a millisecond), then
 * played back by interpolation. Per-frame cost is one lerp, cameras know the landing spot in advance,
 * and replays are just the stored path.
 */
UCLASS(ClassGroup = Golf, meta = (BlueprintSpawnableComponent))
class NEXTGENGOLF_API UGolfShotComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGolfShotComponent();

	UPROPERTY(BlueprintAssignable) FGolfShotStruck OnShotStruck;
	UPROPERTY(BlueprintAssignable) FGolfBallAtRest OnBallAtRest;

	/** Arms the mouse swing. Terrain must outlive the shot. */
	void BeginSwing(const golf::ITerrain* Terrain, const golf::Conditions& Conditions, golf::ClubId Club,
	                const golf::ShotIntent& Intent, golf::Surface Lie, golf::Vec3 BallPosition, double AimDeg,
	                const golf::SwingSettings& Settings, const golf::PlayerAttributes& Attributes, golf::Cup Cup);

	UFUNCTION(BlueprintPure, Category = "Golf") float GetLivePower() const { return float(Swing.livePower()); }
	UFUNCTION(BlueprintPure, Category = "Golf") bool IsBallMoving() const { return bPlaying; }
	const golf::SwingResult& GetSwingResult() const { return Swing.result(); }
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
	golf::ShotOutcome Outcome;
	const golf::ITerrain* ActiveTerrain = nullptr;
	golf::Conditions ActiveConditions;
	golf::ClubId ActiveClub = golf::ClubId::Driver;
	golf::ShotIntent ActiveIntent;
	golf::Surface ActiveLie = golf::Surface::Tee;
	golf::Vec3 ActiveBall;
	golf::PlayerAttributes ActiveAttributes;
	golf::Cup ActiveCup;
	double ActiveAim = 0.0;
	double PlaybackTime = 0.0;
	int32 PlaybackIndex = 0;
	bool bArmed = false;
	bool bPlaying = false;
};

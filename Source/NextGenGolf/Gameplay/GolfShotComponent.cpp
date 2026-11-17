// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#include "Gameplay/GolfShotComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Input/GolfMouseSampler.h"

UGolfShotComponent::UGolfShotComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UGolfShotComponent::BeginPlay()
{
	Super::BeginPlay();
	Sampler = MakeShared<FGolfMouseSampler>();
	Sampler->OnSample = [this](const golf::MouseSample& Sample) { if (bArmed) { Swing.addSample(Sample); } };
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().RegisterInputPreProcessor(Sampler);
	}
}

void UGolfShotComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Sampler.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(Sampler);
	}
	Sampler.Reset();
	Super::EndPlay(Reason);
}

void UGolfShotComponent::BeginSwing(const golf::ITerrain* Terrain, const golf::Conditions& Conditions, golf::ClubId Club,
                                    const golf::ShotIntent& Intent, golf::Surface Lie, golf::Vec3 BallPosition, double AimDeg,
                                    const golf::SwingSettings& Settings, const golf::PlayerAttributes& Attributes, golf::Cup Cup)
{
	ActiveTerrain = Terrain;
	ActiveConditions = Conditions;
	ActiveClub = Club;
	ActiveIntent = Intent;
	ActiveLie = Lie;
	ActiveBall = BallPosition;
	ActiveAim = AimDeg;
	ActiveAttributes = Attributes;
	ActiveCup = Cup;
	Swing = golf::MouseSwing(Settings, Attributes);
	bArmed = true;
	bPlaying = false;
	if (Sampler.IsValid()) { Sampler->bCapturing = true; }
}

void UGolfShotComponent::Strike()
{
	bArmed = false;
	if (Sampler.IsValid()) { Sampler->bCapturing = false; }
	const golf::SwingResult& Result = Swing.result();
	if (!Result.valid || ActiveTerrain == nullptr) { return; }

	if (ActiveClub == golf::ClubId::Putter)
	{
		const golf::PuttLaunch Putt = golf::computePutt(Result, 6.0, ActiveAttributes);
		Outcome = golf::simulatePutt(Putt.speed, ActiveBall, ActiveAim + Putt.azimuthDeg, *ActiveTerrain, ActiveConditions, ActiveCup);
	}
	else
	{
		const golf::Launch Launch = golf::computeLaunch(Result, ActiveClub, ActiveIntent, ActiveLie, ActiveAttributes, ActiveConditions.wetness);
		Outcome = golf::simulateShot(Launch, ActiveBall, ActiveAim, *ActiveTerrain, ActiveConditions, ActiveCup);
	}
	PlaybackTime = 0.0;
	PlaybackIndex = 0;
	bPlaying = Outcome.path.size() >= 2;
	OnShotStruck.Broadcast(float(Outcome.carry));
}

void UGolfShotComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bArmed)
	{
		Swing.tick(FPlatformTime::Seconds());
		if (Swing.phase() == golf::SwingPhase::Done) { Strike(); }
		else if (Swing.phase() == golf::SwingPhase::Cancelled) { Swing.reset(); } // backed off: re-address
	}

	if (bPlaying)
	{
		PlaybackTime += DeltaTime;
		const std::vector<golf::TrajectoryPoint>& Path = Outcome.path;
		while (PlaybackIndex + 2 < int32(Path.size()) && Path[PlaybackIndex + 1].t < PlaybackTime) { ++PlaybackIndex; }
		const golf::TrajectoryPoint& A = Path[PlaybackIndex];
		const golf::TrajectoryPoint& B = Path[PlaybackIndex + 1];
		const double Alpha = B.t > A.t ? FMath::Clamp((PlaybackTime - A.t) / (B.t - A.t), 0.0, 1.0) : 1.0;
		GetOwner()->SetActorLocation(FMath::Lerp(ToUnreal(A.pos), ToUnreal(B.pos), Alpha));
		if (PlaybackTime >= Path.back().t)
		{
			bPlaying = false;
			OnBallAtRest.Broadcast();
		}
	}
}

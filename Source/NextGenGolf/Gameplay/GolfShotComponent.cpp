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

void UGolfShotComponent::BeginSwing(golf::HolePlay* Play, golf::ClubId Club, const golf::ShotIntent& Intent, double AimDeg,
                                    const golf::SwingSettings& Settings, const golf::PlayerAttributes& Attributes)
{
	if (Play == nullptr || Play->holed() || bPlaying) { return; }
	ActivePlay = Play;
	ActiveClub = Club;
	ActiveIntent = Intent;
	ActiveAim = AimDeg;
	ActiveSettings = Settings;
	ActiveAttributes = Attributes;
	Swing = golf::MouseSwing(Settings, Attributes);
	bArmed = true;
	if (Sampler.IsValid()) { Sampler->bCapturing = true; }
}

void UGolfShotComponent::CancelSwing()
{
	bArmed = false;
	if (Sampler.IsValid()) { Sampler->bCapturing = false; }
}

void UGolfShotComponent::Strike()
{
	CancelSwing();
	LastSwing = Swing.result();
	if (!LastSwing.valid || ActivePlay == nullptr) { return; }

	if (ActiveClub == golf::ClubId::Putter)
	{
		const golf::PuttLaunch Putt = golf::computePutt(LastSwing, 6.0, ActiveAttributes);
		Outcome = ActivePlay->playPutt(Putt.speed, ActiveAim + Putt.azimuthDeg);
	}
	else
	{
		golf::ShotIntent Intent = ActiveIntent;
		if (!golf::shotTypeAllowed(Intent.type, ActiveClub, ActivePlay->lie())) { Intent.type = golf::ShotType::Normal; }
		const golf::Launch Launch = golf::computeLaunch(LastSwing, ActiveClub, Intent, ActivePlay->lie(), ActiveAttributes,
		                                                ActivePlay->conditions().wetness, ActiveSettings.leftHanded);
		Outcome = ActivePlay->playShot(Launch, ActiveAim);
	}
	PlaybackTime = 0.0;
	PlaybackIndex = 0;
	bPlaying = Outcome.path.size() >= 2;
	OnShotStruck.Broadcast(float(Outcome.carry));
	if (!bPlaying) { OnBallAtRest.Broadcast(); }
}

void UGolfShotComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bArmed)
	{
		Swing.tick(FPlatformTime::Seconds());
		if (Swing.phase() == golf::SwingPhase::Done) { Strike(); }
		else if (Swing.phase() == golf::SwingPhase::Cancelled) { Swing = golf::MouseSwing(ActiveSettings, ActiveAttributes); } // backed off
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
			// After a penalty the ball is not where the path ended: trust the rules engine.
			GetOwner()->SetActorLocation(ToUnreal(ActivePlay->ball()));
			OnBallAtRest.Broadcast();
		}
	}
}

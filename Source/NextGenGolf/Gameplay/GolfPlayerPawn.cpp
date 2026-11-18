// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#include "Gameplay/GolfPlayerPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Gameplay/GolfHoleActor.h"
#include "Gameplay/GolfRoundSubsystem.h"
#include "Gameplay/GolfShotComponent.h"
#include "InputCoreTypes.h"
#include "Misc/DateTime.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "UI/SGolfSwingMeter.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"

AGolfPlayerPawn::AGolfPlayerPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	RootComponent = Camera;
	HoleActorClass = AGolfHoleActor::StaticClass();
	AutoPossessPlayer = EAutoReceiveInput::Player0;
	Bag = golf::Bag::standard().clubs;
}

void AGolfPlayerPawn::BeginPlay()
{
	Super::BeginPlay();
	UGolfRoundSubsystem* Round = GetGameInstance()->GetSubsystem<UGolfRoundSubsystem>();
	if (Round->GetHoleCount() == 0)
	{
		Round->GenerateRandomNine(FDateTime::UtcNow().GetTicks(), 0, 1, 1);
	}

	// The ball: an engine sphere scaled to regulation size, owning the shot component that moves it.
	Ball = GetWorld()->SpawnActor<AActor>(AActor::StaticClass());
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Ball, TEXT("BallMesh"));
	if (UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		Mesh->SetStaticMesh(Sphere);                    // 100 cm diameter at scale 1
	}
	Mesh->SetWorldScale3D(FVector(0.04267));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetMobility(EComponentMobility::Movable);
	Ball->SetRootComponent(Mesh);
	Mesh->RegisterComponent();
	Shot = NewObject<UGolfShotComponent>(Ball, TEXT("Shot"));
	Shot->RegisterComponent();
	Shot->OnBallAtRest.AddDynamic(this, &AGolfPlayerPawn::HandleBallAtRest);

	HoleActor = GetWorld()->SpawnActor<AGolfHoleActor>(HoleActorClass);
	Shot->OnShotStruck.AddDynamic(this, &AGolfPlayerPawn::HandleShotStruck);
	if (GEngine && GEngine->GameViewport)
	{
		SAssignNew(Meter, SGolfSwingMeter);
		GEngine->GameViewport->AddViewportWidgetContent(
			SNew(SBox).HAlign(HAlign_Right).VAlign(VAlign_Center).Padding(FMargin(0, 0, 46, 0))[Meter.ToSharedRef()], 10);
	}
	BeginHole(0);
}

void AGolfPlayerPawn::BeginHole(int32 Index)
{
	UGolfRoundSubsystem* Round = GetGameInstance()->GetSubsystem<UGolfRoundSubsystem>();
	if (!Round->StartHole(Index)) { return; }
	HoleActor->BuildFromHole(*Round->GetHole());
	PrepareStroke();
}

void AGolfPlayerPawn::PrepareStroke()
{
	UGolfRoundSubsystem* Round = GetGameInstance()->GetSubsystem<UGolfRoundSubsystem>();
	golf::HolePlay* Play = Round->GetPlay();
	if (Play == nullptr) { return; }
	Ball->SetActorLocation(UGolfShotComponent::ToUnreal(Play->ball()) + FVector(0, 0, 2.13));
	AimDeg = Play->headingTo(Play->hole().pin());
	if (Play->lie() == golf::Surface::Green) { ClubIndex = int32(Bag.size()) - 1; }      // putter
	else if (Play->lie() == golf::Surface::Tee && Play->hole().par > 3) { ClubIndex = 0; } // driver
}

void AGolfPlayerPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Ball == nullptr || Shot == nullptr) { return; }
	if (!Shot->IsBallMoving() && !Shot->IsArmed()) { AimDeg += AimInput * 25.0 * DeltaSeconds; }

	UpdateMeter();

	// Address camera: behind the ball on the aim line. (Flight and landing cameras come next.)
	const double Rad = FMath::DegreesToRadians(AimDeg);
	const FVector Forward(FMath::Cos(Rad), FMath::Sin(Rad), 0.0);   // Unreal X = downrange, Y = right
	const FVector BallPos = Ball->GetActorLocation();
	const FVector Wanted = BallPos - Forward * 700.0 + FVector(0, 0, 280.0);
	const FVector NewPos = FMath::VInterpTo(GetActorLocation(), Wanted, DeltaSeconds, 5.f);
	SetActorLocation(NewPos);
	const FRotator Look = (BallPos + Forward * 3000.0 - NewPos).Rotation();
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), Look, DeltaSeconds, 8.f));
}

void AGolfPlayerPawn::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);
	Input->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AGolfPlayerPawn::SwingPressed);
	Input->BindKey(EKeys::LeftMouseButton, IE_Released, this, &AGolfPlayerPawn::SwingReleased);
	Input->BindKey(EKeys::W, IE_Pressed, this, &AGolfPlayerPawn::ClubUp);
	Input->BindKey(EKeys::S, IE_Pressed, this, &AGolfPlayerPawn::ClubDown);
	Input->BindKey(EKeys::A, IE_Pressed, this, &AGolfPlayerPawn::AimLeftPressed);
	Input->BindKey(EKeys::A, IE_Released, this, &AGolfPlayerPawn::AimReleased);
	Input->BindKey(EKeys::D, IE_Pressed, this, &AGolfPlayerPawn::AimRightPressed);
	Input->BindKey(EKeys::D, IE_Released, this, &AGolfPlayerPawn::AimReleased);
}

void AGolfPlayerPawn::SwingPressed()
{
	if (Meter.IsValid()) { Meter->ClearResult(); }
	UGolfRoundSubsystem* Round = GetGameInstance()->GetSubsystem<UGolfRoundSubsystem>();
	if (Round->GetPlay() == nullptr || Shot->IsBallMoving()) { return; }
	const golf::SwingSettings Settings = golf::SwingSettings::forDifficulty(golf::SwingDifficulty(FMath::Clamp(SwingDifficulty, 0, 3)));
	Shot->BeginSwing(Round->GetPlay(), Bag[size_t(ClubIndex)], golf::ShotIntent{}, AimDeg, Settings, golf::PlayerAttributes{});
}

void AGolfPlayerPawn::SwingReleased() { if (Shot) { Shot->CancelSwing(); } }  // letting go before impact backs off
void AGolfPlayerPawn::ClubUp() { ClubIndex = FMath::Max(0, ClubIndex - 1); }
void AGolfPlayerPawn::ClubDown() { ClubIndex = FMath::Min(int32(Bag.size()) - 1, ClubIndex + 1); }
void AGolfPlayerPawn::AimLeftPressed() { AimInput = -1.f; }
void AGolfPlayerPawn::AimRightPressed() { AimInput = 1.f; }
void AGolfPlayerPawn::AimReleased() { AimInput = 0.f; }

void AGolfPlayerPawn::UpdateMeter()
{
	if (!Meter.IsValid() || Shot == nullptr) { return; }
	UGolfRoundSubsystem* Round = GetGameInstance()->GetSubsystem<UGolfRoundSubsystem>();
	golf::HolePlay* Play = Round->GetPlay();
	const float Power = Shot->IsArmed() ? Shot->GetLivePower() : 0.f;
	FText Carry, Prompt;
	if (Play != nullptr && !Shot->IsBallMoving())
	{
		const golf::ClubId Club = Bag[size_t(ClubIndex)];
		if (Club != golf::ClubId::Putter)
		{
			// Carry for the current backswing, from the real physics (a perfectly struck swing of that length).
			const golf::SwingSettings Sim = golf::SwingSettings::forDifficulty(golf::SwingDifficulty::Simulation);
			const golf::Launch L = golf::computeLaunch(golf::classicMeterSwing(FMath::Max(Power, 0.05f), 0.0, Sim, golf::PlayerAttributes{}), Club, golf::ShotIntent{}, Play->lie());
			golf::FlightParams FP; FP.dt = 1.0 / 120.0; FP.recordPath = false;
			const double CarryM = golf::simulateShot(L, Play->ball(), AimDeg, Play->hole(), Play->conditions(), {}, FP).carry;
			Carry = FText::FromString(FString::Printf(TEXT("%d yd"), FMath::RoundToInt(CarryM / 0.9144)));
		}
		Prompt = Shot->IsArmed()
			? (FMath::Abs(Power - ZonePower) <= 0.04f ? FText::FromString(TEXT("Now push through")) : FText::FromString(TEXT("Keep pulling back")))
			: FText::FromString(TEXT("Hold and pull back"));
	}
	Meter->SetLive(Power, ZonePower, 0.04f, Carry, Prompt);
}

void AGolfPlayerPawn::HandleShotStruck(float CarryMetres)
{
	if (!Meter.IsValid()) { return; }
	const golf::SwingResult& S = Shot->GetSwingResult();
	FText Grade = FText::FromString(TEXT("GOOD"));
	FLinearColor Color(0.66f, 0.94f, 0.72f);
	if (S.contact == golf::Contact::Thin) { Grade = FText::FromString(TEXT("THIN")); Color = FLinearColor(0.94f, 0.49f, 0.42f); }
	else if (S.contact == golf::Contact::Fat) { Grade = FText::FromString(TEXT("FAT")); Color = FLinearColor(0.94f, 0.49f, 0.42f); }
	else if (S.timingError <= -0.1) { Grade = FText::FromString(TEXT("EARLY")); Color = FLinearColor(1.f, 0.82f, 0.4f); }
	else if (S.timingError >= 0.1) { Grade = FText::FromString(TEXT("LATE")); Color = FLinearColor(1.f, 0.82f, 0.4f); }
	else if (FMath::Abs(S.timingError) < 0.02 && FMath::Abs(S.faceDeg) + 0.5 * FMath::Abs(S.pathDeg) < 1.2) { Grade = FText::FromString(TEXT("PERFECT")); Color = FLinearColor(0.44f, 0.94f, 0.6f); }
	Meter->SetResult(Grade, Color, float(S.timingError));
}

void AGolfPlayerPawn::HandleBallAtRest()
{
	UGolfRoundSubsystem* Round = GetGameInstance()->GetSubsystem<UGolfRoundSubsystem>();
	if (Round->IsHoled())
	{
		const int32 Next = Round->GetCurrentHole() + 1;
		if (Next < Round->GetHoleCount()) { BeginHole(Next); }
		return;
	}
	PrepareStroke();
}

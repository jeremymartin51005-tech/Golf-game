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

// UNVERIFIED: written without an Unreal toolchain available; not yet compiled.
#include "Gameplay/GolfRoundSubsystem.h"
#include "Misc/DateTime.h"

bool UGolfRoundSubsystem::Adopt(golf::Course&& NewCourse)
{
	Play.Reset();              // the play object points into the old course
	CurrentHole = -1;
	Course = MoveTemp(NewCourse);
	return !Course.holes.empty();
}

bool UGolfRoundSubsystem::GenerateRandomNine(int64 Seed, int32 Environment, int32 Difficulty, int32 TeeIndex)
{
	golf::CourseConfig Config;
	Config.seed = uint64(Seed);
	Config.environment = golf::Environment(FMath::Clamp(Environment, 0, int32(golf::Environment::Count) - 1));
	Config.difficulty = golf::Difficulty(FMath::Clamp(Difficulty, 0, 3));
	Config.teeIndex = FMath::Clamp(TeeIndex, 0, 3);
	return Adopt(golf::generateCourse(Config));
}

bool UGolfRoundSubsystem::GenerateFromShareCode(const FString& Code)
{
	golf::CourseConfig Config;
	if (!golf::decodeShareCode(std::string(TCHAR_TO_UTF8(*Code)), &Config)) { return false; }
	return Adopt(golf::generateCourse(Config));
}

bool UGolfRoundSubsystem::GenerateDaily()
{
	return Adopt(golf::generateCourse(golf::dailyConfig(golf::dateFromUnix(FDateTime::UtcNow().ToUnixTimestamp()))));
}

bool UGolfRoundSubsystem::StartHole(int32 HoleIndex)
{
	if (HoleIndex < 0 || HoleIndex >= GetHoleCount()) { return false; }
	CurrentHole = HoleIndex;
	Play = MakeUnique<golf::HolePlay>(Course.holes[size_t(HoleIndex)], Course.conditionsForHole(HoleIndex), Course.config.teeIndex);
	return true;
}

FString UGolfRoundSubsystem::GetShareCode() const
{
	return FString(UTF8_TO_TCHAR(golf::encodeShareCode(Course.config).c_str()));
}

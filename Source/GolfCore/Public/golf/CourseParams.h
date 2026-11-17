#pragma once
#include "Course.h"

namespace golf {

// Data-driven design rules. Tuning a difficulty or adding an environment is a table edit.
struct DifficultyParams {
    double fairwayHalfWidth;   // metres at the landing zone
    double lengthScale;
    double hazardMult;
    double greenScale;
    double maxGreenTiltPct;
    double maxPinSlopePct;
    double maxForcedCarry;     // longest water carry the design may demand
    double driveDistance;      // where the first landing zone sits from the back tee
    double stimpFeet;
};
struct EnvironmentParams {
    double hillAmp, detailAmp, elevRange, treeDensity, waterChance, bunkerMult;
    double windMin, windMax, teeRaise, widthMult, altitudeM, tempMin, tempMax;
};
const DifficultyParams& difficultyParams(Difficulty d);
const EnvironmentParams& environmentParams(Environment e);

} // namespace golf

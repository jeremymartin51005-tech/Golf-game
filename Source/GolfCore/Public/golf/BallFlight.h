#pragma once
#include <vector>
#include "Impact.h"
#include "Surface.h"

namespace golf {

struct TrajectoryPoint { double t; Vec3 pos; bool airborne; };

enum class RestReason { Stopped, Water, OutOfBounds, Holed, TimedOut };

struct ShotOutcome {
    Vec3 start, landing, rest;
    double carry = 0;           // horizontal distance to first ground contact
    double total = 0;           // horizontal distance to rest
    double apex = 0;            // max height above start
    double landingAngleDeg = 0;
    double flightTime = 0, totalTime = 0;
    double lateralAtRest = 0;   // metres right(+)/left(-) of aim line
    double curveM = 0;          // lateral movement in the air relative to start line, + = right
    double finalSpinRpm = 0;
    int bounces = 0;
    Surface restSurface = Surface::Fairway;
    RestReason reason = RestReason::Stopped;
    bool hitObstacle = false;
    Vec3 hazardEntry;           // last dry point before water / in-bounds point before OOB
    std::vector<TrajectoryPoint> path; // decimated, for tracers, cameras and replays
};

struct Cup { bool present = false; Vec2 pos; };

struct FlightParams {
    double dt = 1.0 / 240.0;
    double maxTime = 60.0;
    bool recordPath = true;
};

// aimDeg: compass-style heading of the aim line in the terrain frame, 0 = +Y, 90 = +X.
ShotOutcome simulateShot(const Launch& launch, Vec3 startPos, double aimDeg, const ITerrain& terrain,
                         const Conditions& cond, Cup cup = {}, const FlightParams& fp = {});

// Rolling-only simulation used by the putter. speed in m/s along aimDeg.
ShotOutcome simulatePutt(double speed, Vec3 startPos, double aimDeg, const ITerrain& terrain,
                         const Conditions& cond, Cup cup, const FlightParams& fp = {});

// Putter: backswing length selects pace, timing/path errors become face/pace errors.
struct PuttLaunch { double speed; double azimuthDeg; };
PuttLaunch computePutt(const SwingResult& swing, double maxPuttSpeed = 6.0, const PlayerAttributes& attr = {});

// Green reading aid: the aim offset and pace that would hole the putt (searches the real simulation).
struct PuttRead { bool found; double aimDeg; double speed; double breakM; double elevationM; };
PuttRead readPutt(Vec3 ballPos, Cup cup, const ITerrain& terrain, const Conditions& cond);

} // namespace golf

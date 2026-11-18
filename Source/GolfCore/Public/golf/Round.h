#pragma once
#include <string>
#include <vector>
#include "BallFlight.h"
#include "Course.h"

namespace golf {

// Rules of the mode being played. Traditional modes use `traditional()` and nothing can alter the
// physics inputs; Chaos Mode will supply its own rules object and its own shot modifiers.
struct GameRules {
    bool chaos = false;
    bool powerShotsAllowed = false;
    static constexpr GameRules traditional() { return {false, false}; }
};

struct HoleResult {
    int number = 0, par = 0, strokes = 0, putts = 0, penalties = 0, sandShots = 0;
    bool fairwayHit = false, fairwayApplies = false, gir = false, holed = false;
    double longestDrive = 0;
};

const char* scoreName(int strokes, int par);

// Plays one hole under the rules of golf: tracks the ball, the lie, penalties and statistics.
class HolePlay {
public:
    HolePlay(const HoleLayout& hole, const Conditions& cond, int teeIndex);
    const HoleLayout& hole() const { return hole_; }
    const Conditions& conditions() const { return cond_; }
    Vec3 ball() const { return ball_; }
    Surface lie() const { return lie_; }
    bool holed() const { return res_.holed; }
    double distanceToPin() const { return (hole_.pin() - ball_.xy()).length(); }
    double headingTo(Vec2 target) const;
    // Applies the slope of the lie to the launch before simulating (see applyLieSlope).
    ShotOutcome playShot(const Launch& launch, double aimDeg, bool leftHanded = false);
    LieSlope lieSlope(double aimDeg, bool leftHanded = false) const { return lieSlopeAt(hole_, ball_.xy(), aimDeg, leftHanded); }
    ShotOutcome playPutt(double speed, double aimDeg);
    const HoleResult& result() const { return res_; }
private:
    void settle(const ShotOutcome& o, bool wasPutt);
    const HoleLayout& hole_;
    Conditions cond_;
    Vec3 ball_;
    Surface lie_ = Surface::Tee;
    HoleResult res_;
};

struct Scorecard {
    std::vector<HoleResult> holes;
    int strokes() const;
    int par() const;
    int toPar() const { return strokes() - par(); }
    int putts() const;
    int fairwaysHit() const; int fairwaysPossible() const;
    int greensInRegulation() const;
    int countScore(int relativeToPar) const; // -1 birdies, 0 pars, +1 bogeys ...
    int penalties() const;
};

// AI golfer. skill 1.0 is an expert human (small, seeded execution error); lower skill adds more.
// Used to prove generated holes are playable, and later as the basis for AI opponents.
struct BotGolfer {
    double skill = 1.0;
    Rng rng{1};
    PlayerAttributes attr{1, 1, 1, 1, 1, 1};
    HoleResult playHole(HolePlay& play, int maxStrokes = 12);
};

Scorecard playRound(const Course& course, BotGolfer& bot);

} // namespace golf

#pragma once
#include <string>
#include <vector>
#include "Math.h"

namespace golf {

// One raw relative mouse report. dy > 0 means the mouse moved TOWARD the player (backswing).
struct MouseSample {
    double t;   // seconds, monotonic
    double dx;  // counts, + = right
    double dy;  // counts, + = toward player
};

enum class SwingDifficulty { Beginner, Intermediate, Advanced, Simulation };

struct SwingSettings {
    double sensitivity = 1.0;          // multiplies raw counts
    double backswingCounts = 600;      // scaled counts for a 100% backswing (calibrated per player)
    double idealDownswingTime = 0.28;  // seconds, "swing speed" calibration
    double idealTempo = 3.0;           // backswing time : downswing time
    double assist = 0.0;               // 0..1, scales every execution error toward zero
    double timingWindow = 0.08;        // fractional timing error that is forgiven completely
    double pathDeadzoneDeg = 1.0;      // lateral wobble forgiven completely
    double pathGain = 0.50;            // degrees of club path per degree of mouse-path angle
    double timingFaceGainDeg = 14.0;   // degrees of face per 100% timing error
    double releaseFaceGainDeg = 0.30;  // degrees of face per degree of late lateral drift
    bool leftHanded = false;
    static SwingSettings forDifficulty(SwingDifficulty d);
};

struct PlayerAttributes {            // 0..1 each; 0.5 is an average created golfer
    double power = 0.5;
    double accuracy = 0.5;
    double shortGame = 0.5;
    double putting = 0.5;
    double recovery = 0.5;
    double consistency = 0.5;
};

enum class Contact { Pure, Thin, Fat };
enum class SwingPhase { Address, Backswing, Downswing, Done, Cancelled };

// What the golfer's body did. Deliberately free of ball physics: the impact model turns this into a launch.
struct SwingResult {
    bool valid = false;
    double power = 0;            // 0..1 intended (backswing length, capped)
    double overswing = 0;        // backswing beyond 100%, as a fraction
    double speedFactor = 0;      // clubhead speed relative to the club's stock speed
    double pathDeg = 0;          // club path, + = in-to-out (right of target for RH)
    double faceDeg = 0;          // face angle relative to target, + = open (right for RH)
    double timingError = 0;      // (actual - ideal)/ideal downswing time; <0 early/fast, >0 late/slow
    double tempoRatio = 0;       // backswing time / downswing time
    double backswingTime = 0, downswingTime = 0;
    Contact contact = Contact::Pure;
    double contactQuality = 1;   // 1 = centre strike
    std::vector<std::string> feedback; // plain-language causes, most important first
};

// Streaming analyser: feed samples as they arrive (one per input event, not per frame).
class MouseSwing {
public:
    explicit MouseSwing(SwingSettings s = {}, PlayerAttributes a = {});
    void reset();
    void addSample(const MouseSample& s);
    // Call with the current time when no input arrives, so a stalled swing can resolve.
    void tick(double now);
    SwingPhase phase() const { return phase_; }
    bool finished() const { return phase_ == SwingPhase::Done || phase_ == SwingPhase::Cancelled; }
    // Live values for the swing indicator.
    double livePower() const;
    double liveLateral() const { return pos_.x; }
    const SwingResult& result() const { return result_; }
    const SwingSettings& settings() const { return set_; }

private:
    struct Pt { double t; Vec2 p; };
    void finish(bool reachedBall);
    SwingSettings set_;
    PlayerAttributes attr_;
    SwingPhase phase_ = SwingPhase::Address;
    Vec2 pos_;                 // scaled counts relative to address; y + = back
    double topY_ = 0, topT_ = 0, startT_ = 0, lastT_ = 0, lastMoveT_ = 0;
    Vec2 topPos_;
    std::vector<Pt> down_;
    SwingResult result_;
};

// Classic three-click meter and one-click assisted swing produce the same SwingResult,
// so every downstream system is identical regardless of control scheme.
// power 0..1.1 at second click; accuracy -1..1 at third click (0 = perfect, - early, + late).
SwingResult classicMeterSwing(double power, double accuracy, const SwingSettings& s, const PlayerAttributes& a);
SwingResult assistedSwing(double power, const PlayerAttributes& a);

// Test/bot helper: synthesise the mouse samples of a swing.
struct SyntheticSwing {
    double power = 1.0;           // backswing as fraction of backswingCounts
    double downTimeScale = 1.0;   // 1 = ideal, <1 rushed, >1 slow
    double pathAngleDeg = 0;      // mouse-space angle of the downswing, + = drifting right
    double lateReleaseDeg = 0;    // extra lateral drift in the final 20% before impact
    double stopShortFrac = 0;     // >0: stop this fraction of the backswing before the ball
    double decelerate = 0;        // 0..1: slow down through the hitting zone
    double rateHz = 1000;
};
std::vector<MouseSample> synthesizeSwing(const SyntheticSwing& p, const SwingSettings& s);
SwingResult runSwing(const std::vector<MouseSample>& samples, const SwingSettings& s, const PlayerAttributes& a = {});

} // namespace golf

#pragma once
#include "Clubs.h"
#include "Surface.h"
#include "Swing.h"

namespace golf {

enum class ShotType { Normal, High, Low, Punch, Knockdown, Stinger, Pitch, Chip, Flop, Lob, Bunker, Recovery, Count };
enum class SwingStyle { Full, ThreeQuarter, Half, Controlled };

struct ShotTypeSpec {
    const char* name;
    double speedMult;     // ball speed at full power
    double launchDelta;   // degrees added to stock launch
    double launchMult;    // multiplies stock launch (applied before delta)
    double spinMult;
    double errorMult;     // execution errors (a punch is easier to control than a flop)
};
const ShotTypeSpec& shotTypeSpec(ShotType t);
bool shotTypeAllowed(ShotType t, ClubId club, Surface lie);

// Player's intended shape, chosen in the shot menu. curve: -1 full draw .. +1 full fade (RH).
// trajectory: -1 low .. +1 high. These bias path/face/launch; execution errors add on top.
struct ShotIntent {
    ShotType type = ShotType::Normal;
    SwingStyle style = SwingStyle::Full;
    double curve = 0;
    double trajectory = 0;
    // Strike point on the ball, as chosen in the shot menu: -1 = top of the ball (low spin, ball
    // releases and runs), +1 = bottom of the ball (maximum backspin, ball checks or spins back).
    // Strongest with wedges, weakest with the driver; needs clean contact to work.
    double spin = 0;
};

struct Launch {
    double ballSpeed = 0;      // m/s
    double launchDeg = 0;      // vertical
    double azimuthDeg = 0;     // start direction relative to aim, + = right
    double spinRpm = 0;        // total
    double spinAxisDeg = 0;    // tilt, + = curves right
    double headSpeed = 0;
    double faceToPathDeg = 0;
};

// Slope of the ground the ball sits on, relative to the shot. Uphill lies launch higher and fly
// shorter; downhill lies come out lower. Ball above the feet starts and curves left (for a right-
// hander), ball below the feet goes right. Percent grade.
struct LieSlope { double uphillPct = 0; double ballAboveFeetPct = 0; };
LieSlope lieSlopeAt(const ITerrain& terrain, Vec2 ball, double aimDeg, bool leftHanded = false);
void applyLieSlope(Launch& launch, const LieSlope& slope, bool leftHanded = false);

// Named shape of a finished shot, derived from physics, for the HUD ("Push Draw").
const char* describeShape(double startAzimuthDeg, double spinAxisDeg, bool leftHanded = false);

Launch computeLaunch(const SwingResult& swing, ClubId club, const ShotIntent& intent, Surface lie,
                     const PlayerAttributes& attr = {}, double wetness = 0.0, bool leftHanded = false);

} // namespace golf

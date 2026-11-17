#include "golf/Impact.h"

namespace golf {

namespace {
const ShotTypeSpec kShots[] = {
    // name        speed launchΔ launch× spin× err×
    {"Normal",     1.00,  0.0,   1.00,  1.00, 1.00},
    {"High",       0.98,  3.5,   1.00,  1.10, 1.10},
    {"Low",        0.98,  0.0,   0.75,  0.90, 1.00},
    {"Punch",      0.80,  0.0,   0.55,  0.75, 0.80},
    {"Knockdown",  0.90,  0.0,   0.75,  0.85, 0.85},
    {"Stinger",    0.95,  0.0,   0.50,  0.80, 1.20},
    {"Pitch",      0.55,  4.0,   1.00,  0.95, 0.90},
    {"Chip",       0.30,  0.0,   0.60,  0.50, 0.70},
    {"Flop",       0.50, 16.0,   1.00,  0.90, 1.50},
    {"Lob",        0.60,  9.0,   1.00,  1.00, 1.20},
    {"Bunker",     0.50,  8.0,   1.00,  0.70, 1.20},
    {"Recovery",   0.92,  0.0,   0.80,  0.80, 0.90},
};
}

const ShotTypeSpec& shotTypeSpec(ShotType t) { return kShots[int(t)]; }

bool shotTypeAllowed(ShotType t, ClubId club, Surface lie) {
    ClubClass c = clubSpec(club).cls;
    if (c == ClubClass::Putter) return false; // the putter has its own system
    switch (t) {
        case ShotType::Stinger: return c == ClubClass::Wood || c == ClubClass::Hybrid || (c == ClubClass::Iron && clubSpec(club).loftDeg <= 26.0);
        case ShotType::Flop:
        case ShotType::Lob: return club == ClubId::SandWedge || club == ClubId::LobWedge;
        case ShotType::Pitch: return c == ClubClass::Wedge;
        case ShotType::Chip: return c == ClubClass::Wedge || c == ClubClass::Iron;
        case ShotType::Bunker: return lie == Surface::Sand && c == ClubClass::Wedge;
        case ShotType::Punch:
        case ShotType::Knockdown: return c != ClubClass::Driver;
        default: return true;
    }
}

const char* describeShape(double az, double axis, bool lh) {
    if (lh) { az = -az; axis = -axis; } // name shapes from the golfer's point of view
    int start = az > 1.2 ? 1 : (az < -1.2 ? -1 : 0);
    int curve = axis > 16 ? 2 : axis > 3.5 ? 1 : axis < -16 ? -2 : axis < -3.5 ? -1 : 0;
    static const char* names[3][5] = {
        {"Pull Hook", "Pull Draw", "Pull", "Pull Fade", "Pull Slice"},
        {"Hook", "Draw", "Straight", "Fade", "Slice"},
        {"Push Hook", "Push Draw", "Push", "Push Fade", "Push Slice"},
    };
    return names[start + 1][curve + 2];
}

Launch computeLaunch(const SwingResult& sw, ClubId club, const ShotIntent& intent, Surface lie,
                     const PlayerAttributes& attr, double wetness, bool leftHanded) {
    Launch L;
    if (!sw.valid) return L;
    const ClubSpec& c = clubSpec(club);
    const ShotTypeSpec& st = shotTypeSpec(intent.type);
    SurfaceProps lp = surfaceProps(lie, 10.5, 0.0);

    static const double styleSpeed[] = {1.00, 0.88, 0.72, 0.93};
    static const double styleErr[] = {1.00, 0.90, 0.80, 0.70};
    int si = int(intent.style);

    // --- Direction: face and path ------------------------------------------------------------
    double err = st.errorMult * styleErr[si] * lp.lieDispersion * (1.20 - 0.40 * attr.accuracy);
    double curve = clampd(intent.curve, -1.0, 1.0) * (leftHanded ? -1.0 : 1.0);
    double path = sw.pathDeg * err - curve * 4.0;   // a draw swings in-to-out...
    double face = sw.faceDeg * err - curve * 1.5;   // ...with the face closed to that path, open to the target
    L.faceToPathDeg = face - path;
    L.azimuthDeg = c.faceWeight * face + (1.0 - c.faceWeight) * path;

    // --- Speed --------------------------------------------------------------------------------
    double lieSpeed = lp.lieSpeed, lieSpin = lp.lieSpin;
    if (intent.type == ShotType::Bunker && lie == Surface::Sand) { lieSpeed = 1.0; lieSpin = 1.0; } // designed for sand
    if (intent.type == ShotType::Recovery) { lieSpeed = 1.0 - (1.0 - lieSpeed) * 0.4; }
    if ((c.cls == ClubClass::Driver || c.cls == ClubClass::Wood) &&
        (lie == Surface::Rough || lie == Surface::DeepRough || lie == Surface::Sand)) lieSpeed *= 0.85;
    lieSpeed = 1.0 - (1.0 - lieSpeed) * (1.20 - 0.40 * attr.recovery);

    double headFrac = sw.speedFactor * styleSpeed[si] * st.speedMult;
    L.headSpeed = c.headSpeed * headFrac;
    double q = 1.0 - (1.0 - sw.contactQuality) * (1.0 - 0.5 * c.forgiveness);
    double contactSpeed = 1.0, contactSpin = 1.0;
    if (sw.contact == Contact::Thin) { contactSpeed = 0.55 + 0.45 * q; contactSpin = 0.60 + 0.40 * q; }
    if (sw.contact == Contact::Fat)  { contactSpeed = 0.35 + 0.65 * q; contactSpin = 0.70 + 0.30 * q; }
    L.ballSpeed = c.ballSpeed * headFrac * contactSpeed * lieSpeed;

    // --- Launch and spin ----------------------------------------------------------------------
    double traj = clampd(intent.trajectory, -1.0, 1.0);
    double launch = c.launchDeg * st.launchMult + st.launchDelta + traj * 3.5;
    if (sw.contact == Contact::Thin) launch *= 0.4 + 0.6 * q;
    if (sw.contact == Contact::Fat) launch += 2.0;
    L.launchDeg = clampd(launch, 1.0, 62.0);

    L.spinRpm = c.spinRpm * st.spinMult * headFrac * lieSpin * contactSpin * (1.0 + 0.12 * traj) * (1.0 - 0.15 * clampd(wetness, 0, 1));

    // D-plane: the spin axis tilts by how far the face points away from the path, relative to spin loft.
    double spinLoft = c.spinLoftDeg * clampd(L.launchDeg / std::max(1.0, c.launchDeg), 0.5, 1.6);
    L.spinAxisDeg = clampd(rad2deg(std::atan2(std::sin(deg2rad(L.faceToPathDeg)), std::sin(deg2rad(spinLoft)))), -45.0, 45.0);
    return L;
}

} // namespace golf

#pragma once
#include <string>
#include <vector>

namespace golf {

enum class ClubId { Driver, Wood3, Wood5, Hybrid3, Hybrid4, Iron3, Iron4, Iron5, Iron6, Iron7, Iron8, Iron9,
                    PitchingWedge, GapWedge, SandWedge, LobWedge, Putter, Count };

enum class ClubClass { Driver, Wood, Hybrid, Iron, Wedge, Putter };

// Stock (full, neutral swing, fairway lie) numbers for a tour-level golfer.
struct ClubSpec {
    ClubId id;
    ClubClass cls;
    const char* name;
    const char* shortName;
    double loftDeg;
    double headSpeed;     // m/s
    double ballSpeed;     // m/s  (smash factor = ballSpeed / headSpeed)
    double launchDeg;
    double spinRpm;
    double spinLoftDeg;   // dynamic loft minus attack angle; governs how much face-to-path tilts the spin axis
    double faceWeight;    // share of start direction that comes from the face (rest from path)
    double forgiveness;   // 0..1, shrinks the penalty for poor contact
};

const ClubSpec& clubSpec(ClubId id);
const std::vector<ClubId>& allClubs();

struct Bag {
    std::vector<ClubId> clubs;      // max 14
    static Bag standard();
    bool valid(std::string* why = nullptr) const;
    bool has(ClubId id) const;
};

} // namespace golf

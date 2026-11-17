#include "golf/Clubs.h"

namespace golf {

namespace {
constexpr double MPH = 0.44704;
// Stock numbers follow published launch-monitor tour averages (wedges below PW are estimated).
const ClubSpec kSpecs[] = {
    //  id                    class             name              short loft  head       ball       launch spin   sLoft faceW forgive
    {ClubId::Driver,        ClubClass::Driver, "Driver",         "DR", 10.5, 113 * MPH, 167 * MPH, 10.9,  2686, 12.0, 0.82, 0.70},
    {ClubId::Wood3,         ClubClass::Wood,   "3 Wood",         "3W", 15.0, 107 * MPH, 158 * MPH,  9.2,  3655, 13.5, 0.80, 0.60},
    {ClubId::Wood5,         ClubClass::Wood,   "5 Wood",         "5W", 18.0, 103 * MPH, 152 * MPH,  9.4,  4350, 15.5, 0.80, 0.62},
    {ClubId::Hybrid3,       ClubClass::Hybrid, "3 Hybrid",       "3H", 19.0, 101 * MPH, 148 * MPH, 10.0,  4400, 16.5, 0.78, 0.68},
    {ClubId::Hybrid4,       ClubClass::Hybrid, "4 Hybrid",       "4H", 22.0, 100 * MPH, 146 * MPH, 10.2,  4437, 17.5, 0.78, 0.70},
    {ClubId::Iron3,         ClubClass::Iron,   "3 Iron",         "3i", 20.0,  98 * MPH, 142 * MPH, 10.4,  4630, 19.0, 0.76, 0.35},
    {ClubId::Iron4,         ClubClass::Iron,   "4 Iron",         "4i", 23.0,  96 * MPH, 137 * MPH, 11.0,  4836, 20.0, 0.76, 0.40},
    {ClubId::Iron5,         ClubClass::Iron,   "5 Iron",         "5i", 26.0,  94 * MPH, 132 * MPH, 12.1,  5361, 22.0, 0.75, 0.45},
    {ClubId::Iron6,         ClubClass::Iron,   "6 Iron",         "6i", 30.0,  92 * MPH, 127 * MPH, 14.1,  6231, 25.0, 0.75, 0.50},
    {ClubId::Iron7,         ClubClass::Iron,   "7 Iron",         "7i", 34.0,  90 * MPH, 120 * MPH, 16.3,  7097, 28.0, 0.74, 0.55},
    {ClubId::Iron8,         ClubClass::Iron,   "8 Iron",         "8i", 38.0,  87 * MPH, 115 * MPH, 18.1,  7998, 31.0, 0.73, 0.58},
    {ClubId::Iron9,         ClubClass::Iron,   "9 Iron",         "9i", 42.0,  85 * MPH, 109 * MPH, 20.4,  8647, 34.0, 0.72, 0.60},
    {ClubId::PitchingWedge, ClubClass::Wedge,  "Pitching Wedge", "PW", 46.0,  83 * MPH, 102 * MPH, 24.2,  9304, 37.0, 0.71, 0.60},
    {ClubId::GapWedge,      ClubClass::Wedge,  "Gap Wedge",      "GW", 52.0,  80 * MPH,  94 * MPH, 27.5,  9800, 41.0, 0.70, 0.58},
    {ClubId::SandWedge,     ClubClass::Wedge,  "Sand Wedge",     "SW", 56.0,  78 * MPH,  87 * MPH, 30.5, 10200, 45.0, 0.69, 0.60},
    {ClubId::LobWedge,      ClubClass::Wedge,  "Lob Wedge",      "LW", 60.0,  75 * MPH,  79 * MPH, 33.5, 10500, 49.0, 0.68, 0.50},
    {ClubId::Putter,        ClubClass::Putter, "Putter",         "PT",  3.0,   0,         0,        0,       0,  3.0, 0.90, 0.80},
};
} // namespace

const ClubSpec& clubSpec(ClubId id) { return kSpecs[int(id)]; }

const std::vector<ClubId>& allClubs() {
    static const std::vector<ClubId> v = [] {
        std::vector<ClubId> c;
        for (int i = 0; i < int(ClubId::Count); ++i) c.push_back(ClubId(i));
        return c;
    }();
    return v;
}

Bag Bag::standard() {
    return {{ClubId::Driver, ClubId::Wood3, ClubId::Wood5, ClubId::Hybrid4, ClubId::Iron5, ClubId::Iron6, ClubId::Iron7,
             ClubId::Iron8, ClubId::Iron9, ClubId::PitchingWedge, ClubId::GapWedge, ClubId::SandWedge, ClubId::LobWedge,
             ClubId::Putter}};
}

bool Bag::has(ClubId id) const {
    for (ClubId c : clubs) if (c == id) return true;
    return false;
}

bool Bag::valid(std::string* why) const {
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    if (clubs.size() > 14) return fail("A bag may hold at most 14 clubs.");
    if (clubs.empty()) return fail("The bag is empty.");
    if (!has(ClubId::Putter)) return fail("The bag needs a putter.");
    for (size_t i = 0; i < clubs.size(); ++i)
        for (size_t j = i + 1; j < clubs.size(); ++j)
            if (clubs[i] == clubs[j]) return fail("Duplicate club in bag.");
    return true;
}

} // namespace golf

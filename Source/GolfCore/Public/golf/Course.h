#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Rng.h"
#include "Surface.h"

namespace golf {

enum class Difficulty { Beginner, Intermediate, Advanced, Championship };
enum class Environment { Parkland, Highlands, Coastal, Alpine, Desert, Links, Count };
enum class Weather { Sunny, Cloudy, Overcast, LightRain, Windy, Foggy, Count };
enum class HoleTemplate { Par3Open, Par3Carry, Par3Guarded, Par4Straight, Par4DoglegLeft, Par4DoglegRight, Par4Short,
                          Par5Straight, Par5DoubleDogleg, Par5Cape };

const char* difficultyName(Difficulty d);
const char* environmentName(Environment e);
const char* weatherName(Weather w);
const char* templateName(HoleTemplate t);

struct Ellipse {
    Vec2 c; double rx = 1, ry = 1, rotDeg = 0;
    double norm(Vec2 p) const;                 // <1 inside, 1 on the edge
    bool contains(Vec2 p, double grow = 0) const;
};

enum class BunkerKind { Fairway, Greenside };
struct Bunker { Ellipse shape; BunkerKind kind; };
struct Tree { Vec2 pos; double radius, height; };
struct TeeBox { const char* name; Vec2 pos; double lengthM; };
struct LandingZone { Vec2 pos; double radius; };
struct Green { Ellipse shape; double tiltHeadingDeg = 0, tiltPct = 0; std::vector<Vec2> pins; };

// One generated hole in its own local frame: back tee at the origin, first shot toward +Y.
class HoleLayout : public ITerrain {
public:
    int number = 1, par = 4;
    HoleTemplate tmpl = HoleTemplate::Par4Straight;
    std::vector<Vec2> centerline;       // tee -> landing zones -> green centre
    double lengthM = 0;                 // along the centreline from the back tee
    double fairwayStart = 0, fairwayEnd = 0, fairwayHalfWidth = 18, corridorHalfWidth = 60;
    Green green;
    int pinIndex = 0;
    std::vector<Bunker> bunkers;
    std::vector<Ellipse> water;
    std::vector<Tree> trees;
    std::vector<TeeBox> tees;           // Championship, Back, Middle, Forward
    std::vector<LandingZone> landingZones;
    double elevationChange = 0;         // green minus tee, metres
    // Placement on the course plan.
    Vec2 worldOrigin; double worldHeadingDeg = 0;

    // Heightfield
    Vec2 gridMin; double cell = 2.0; int nx = 0, ny = 0;
    std::vector<float> heights;

    Vec2 pin() const { return green.pins[size_t(pinIndex)]; }
    Vec3 teePosition(int teeIndex) const;
    double centerlineLength() const;
    Vec2 pointAt(double s) const;                               // point at arc length s
    double project(Vec2 p, double* lateral = nullptr) const;    // arc length of nearest centreline point
    double halfWidthAt(double s) const;
    Vec2 toWorld(Vec2 local) const;

    double height(Vec2 p) const override;
    Surface surface(Vec2 p) const override;
    double obstacleHeight(Vec2 p) const override;
};

struct CourseConfig {
    uint64_t seed = 1;
    Difficulty difficulty = Difficulty::Intermediate;
    Environment environment = Environment::Parkland;
    int holes = 9;
    int hazardDensity = 1;      // 0 sparse, 1 normal, 2 heavy, 3 brutal
    int pinDifficulty = 1;      // 0 easy (centre) .. 3 tucked
    int teeIndex = 1;           // 0 Championship .. 3 Forward
    // Optional overrides; negative = decide from the seed.
    double windSpeed = -1, stimpFeet = -1;
    int weather = -1;
    int par3Count = -1, par5Count = -1;
};

struct Course {
    CourseConfig config;
    std::string name;
    std::vector<HoleLayout> holes;
    Conditions conditions;        // wind heading is in the course (world) frame
    Weather weather = Weather::Sunny;
    int totalPar = 0;
    int generationAttempts = 0;
    Conditions conditionsForHole(int index) const; // wind rotated into the hole's local frame
    int lengthM(int teeIndex) const;
};

struct ValidationReport {
    bool ok = true;
    std::vector<std::string> errors, warnings;
    std::vector<int> botStrokes;
    void fail(const std::string& m) { ok = false; errors.push_back(m); }
};

// Always returns a course that passed validation (it regenerates deterministically until one does).
Course generateCourse(const CourseConfig& cfg);
HoleLayout generateHole(Rng& rng, int par, int number, const CourseConfig& cfg);

ValidationReport validateHole(const HoleLayout& h, const CourseConfig& cfg, const Conditions& cond, bool runBot = true);
ValidationReport validateCourse(const Course& c, bool runBot = true);

// Shareable codes: "R9-XXXX-XXXX-XXXX-XXXX" carries the seed and the layout-defining settings.
std::string encodeShareCode(const CourseConfig& cfg);
bool decodeShareCode(const std::string& code, CourseConfig* out);

// Daily Course: one seed per UTC calendar day, identical for every player.
struct Date { int year, month, day; };
Date dateFromUnix(int64_t unixSeconds);
int64_t secondsUntilNextDaily(int64_t unixSeconds);
CourseConfig dailyConfig(Date d);
std::string dailyId(Date d);

} // namespace golf

#include "golf/CourseParams.h"

namespace golf {

Conditions Course::conditionsForHole(int index) const {
    Conditions c = conditions;
    c.wind.headingDeg = conditions.wind.headingDeg - holes[size_t(index)].worldHeadingDeg;
    return c;
}
int Course::lengthM(int teeIndex) const {
    double l = 0;
    for (const HoleLayout& h : holes) l += h.tees[size_t(clampd(teeIndex, 0, 3))].lengthM;
    return int(std::round(l));
}

namespace {

std::vector<int> parOrder(Rng& rng, const CourseConfig& cfg) {
    // Par mixes that total 34-37 for nine holes: {par 3s, par 5s}.
    static const int mixes[][2] = {{2, 2}, {2, 2}, {2, 2}, {2, 1}, {3, 2}, {1, 2}, {3, 1}};
    int m = rng.rangeInt(0, 6);
    int n3 = cfg.par3Count >= 0 ? cfg.par3Count : mixes[m][0];
    int n5 = cfg.par5Count >= 0 ? cfg.par5Count : mixes[m][1];
    n3 = int(clampd(n3, 0, 4)); n5 = int(clampd(n5, 0, 4));
    while (36 - n3 + n5 > 37) (n5 > 0 ? --n5 : ++n3);
    while (36 - n3 + n5 < 34) (n3 > 0 ? --n3 : ++n5);
    std::vector<int> pars(9, 4);
    // Open with a par 4, never play par 3s (or par 5s) back to back.
    for (int tries = 0; tries < 200; ++tries) {
        std::vector<int> p(9, 4);
        std::vector<int> slots = {1, 2, 3, 4, 5, 6, 7, 8};
        for (size_t i = slots.size() - 1; i > 0; --i) std::swap(slots[i], slots[size_t(rng.rangeInt(0, int(i)))]);
        for (int i = 0; i < n3; ++i) p[size_t(slots[size_t(i)])] = 3;
        for (int i = 0; i < n5; ++i) p[size_t(slots[size_t(n3 + i)])] = 5;
        bool ok = true;
        for (int i = 1; i < 9; ++i) if (p[size_t(i)] != 4 && p[size_t(i)] == p[size_t(i - 1)]) ok = false;
        pars = p;
        if (ok) break;
    }
    return pars;
}

bool routeHoles(Rng& rng, std::vector<HoleLayout>& holes) {
    auto sample = [](const HoleLayout& h) {
        std::vector<Vec2> pts;
        double L = h.centerlineLength();
        for (double s = 0; s <= L; s += 10.0) pts.push_back(h.toWorld(h.pointAt(s)));
        pts.push_back(h.toWorld(h.centerline.back()));
        return pts;
    };
    std::vector<std::vector<Vec2>> placed;
    double prevHeading = rng.range(0, 360);
    Vec2 prevGreen{0, 0};
    for (size_t i = 0; i < holes.size(); ++i) {
        HoleLayout& h = holes[i];
        bool ok = false;
        for (int t = 0; t < 60 && !ok; ++t) {
            // Turn gently at first; allow sharper turns (even doubling back) if the easy routings are blocked.
            double spread = t < 20 ? 100.0 : 170.0;
            h.worldHeadingDeg = prevHeading + rng.range(-spread, spread);
            double walkDir = h.worldHeadingDeg + rng.range(-60, 60);
            Vec2 off = i == 0 ? Vec2{0, 0} : Vec2{std::sin(deg2rad(walkDir)), std::cos(deg2rad(walkDir))} * rng.range(42, 75);
            h.worldOrigin = prevGreen + off;
            std::vector<Vec2> line = sample(h);
            ok = true;
            for (size_t j = 0; j < placed.size() && ok; ++j) {
                double need = j + 1 == i ? 22.0 : 56.0;
                for (Vec2 a : line) { for (Vec2 b : placed[j]) if ((a - b).length() < need) { ok = false; break; } if (!ok) break; }
            }
            if (ok) placed.push_back(line);
        }
        if (!ok) return false;
        prevGreen = placed.back().back();
        // Heading of the final approach, in world terms.
        Vec2 a = h.centerline[h.centerline.size() - 2], b = h.centerline.back();
        prevHeading = h.worldHeadingDeg + rad2deg(std::atan2(b.x - a.x, b.y - a.y));
    }
    return true;
}

std::string courseName(Rng& rng, Environment e) {
    static const char* first[] = {"Whispering", "Silver", "Hidden", "Royal", "Old", "Wild", "Golden", "Stone", "Falcon",
                                  "Copper", "Blue", "Lone", "Twin", "Broken", "Raven", "Amber"};
    static const char* second[][5] = {
        {"Oaks", "Meadow", "Willows", "Brook", "Orchard"},     // Parkland
        {"Glen", "Heath", "Moor", "Crag", "Tarn"},             // Highlands
        {"Cove", "Dunes", "Bay", "Point", "Tides"},            // Coastal
        {"Ridge", "Summit", "Pass", "Pines", "Peak"},          // Alpine
        {"Mesa", "Canyon", "Arroyo", "Springs", "Butte"},      // Desert
        {"Links", "Strand", "Burn", "Warren", "Sands"},        // Links
    };
    // One draw per statement: argument evaluation order differs between compilers, and a shared seed
    // must produce the same course everywhere.
    int a = rng.rangeInt(0, 15);
    int b = rng.rangeInt(0, 4);
    return std::string(first[a]) + " " + second[int(e)][b];
}

} // namespace

uint64_t courseFingerprint(const Course& c) {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](double v) { h ^= uint64_t(int64_t(std::llround(v * 100.0))); h *= 1099511628211ULL; };
    for (const HoleLayout& hole : c.holes) {
        mix(hole.par); mix(hole.lengthM); mix(hole.pin().x); mix(hole.pin().y); mix(hole.worldHeadingDeg);
        mix(double(hole.bunkers.size())); mix(double(hole.water.size())); mix(double(hole.trees.size()));
        for (const Bunker& b : hole.bunkers) { mix(b.shape.c.x); mix(b.shape.c.y); }
        for (size_t i = 0; i < hole.heights.size(); i += 97) mix(hole.heights[i]);
    }
    mix(c.conditions.wind.speed); mix(c.conditions.wind.headingDeg); mix(c.conditions.stimpFeet);
    for (char ch : c.name) mix(ch);
    return h;
}

Course generateCourse(const CourseConfig& cfgIn) {
    CourseConfig cfg = cfgIn;
    cfg.holes = cfg.holes == 18 ? 18 : 9;
    const DifficultyParams& dp = difficultyParams(cfg.difficulty);
    const EnvironmentParams& ep = environmentParams(cfg.environment);

    for (int attempt = 0; attempt < 40; ++attempt) {
        Rng rng(cfg.seed ^ (uint64_t(attempt) * 0xA24BAED4963EE407ULL));
        Course c;
        c.config = cfg;
        c.generationAttempts = attempt + 1;

        // Conditions come from the seed too, so a shared code reproduces the whole round.
        Rng cr = rng.fork(11);
        c.name = courseName(cr, cfg.environment);
        c.weather = cfg.weather >= 0 ? Weather(int(clampd(cfg.weather, 0, int(Weather::Count) - 1))) : Weather(cr.rangeInt(0, int(Weather::Count) - 1));
        double wind = cr.range(ep.windMin, ep.windMax);
        if (c.weather == Weather::Windy) wind = wind * 1.4 + 2.0;
        if (c.weather == Weather::Foggy) wind *= 0.4;
        c.conditions.wind.speed = cfg.windSpeed >= 0 ? cfg.windSpeed : wind;
        c.conditions.wind.headingDeg = cr.range(0, 360);
        c.conditions.temperatureC = cr.range(ep.tempMin, ep.tempMax);
        c.conditions.altitudeM = ep.altitudeM;
        c.conditions.wetness = c.weather == Weather::LightRain ? 0.5 : c.weather == Weather::Foggy ? 0.2 : 0.0;
        c.conditions.stimpFeet = cfg.stimpFeet > 0 ? cfg.stimpFeet : dp.stimpFeet + cr.range(-0.5, 0.5) - 1.0 * c.conditions.wetness;

        std::vector<int> pars;
        for (int nine = 0; nine < cfg.holes / 9; ++nine) {
            Rng pr = rng.fork(100 + uint64_t(nine));
            std::vector<int> p = parOrder(pr, cfg);
            pars.insert(pars.end(), p.begin(), p.end());
        }

        // Design each hole; a hole that fails validation is redesigned, not shipped.
        Conditions calm = c.conditions;
        calm.wind.speed = 0;
        bool ok = true;
        for (size_t i = 0; i < pars.size() && ok; ++i) {
            bool holeOk = false;
            for (int t = 0; t < 14 && !holeOk; ++t) {
                Rng hr = rng.fork(1000 + i * 37 + uint64_t(t) * 7919);
                HoleLayout h = generateHole(hr, pars[i], int(i) + 1, cfg);
                if (validateHole(h, cfg, calm, true).ok) { c.holes.push_back(std::move(h)); holeOk = true; }
            }
            ok = holeOk;
        }
        if (!ok) continue;

        Rng rr = rng.fork(5000);
        if (!routeHoles(rr, c.holes)) continue;
        for (const HoleLayout& h : c.holes) c.totalPar += h.par;

        // Final gate: the whole course, in its real wind, played by the AI golfer.
        if (validateCourse(c, true).ok) return c;
    }
    // Unreachable in practice (see tests); return an empty course rather than an unvalidated one.
    Course none;
    none.config = cfg;
    return none;
}

} // namespace golf

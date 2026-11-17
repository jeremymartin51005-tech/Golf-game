#include "golf/CourseParams.h"
#include "golf/Round.h"

namespace golf {

namespace {
double slopePct(const ITerrain& t, Vec2 p) {
    Vec3 n = t.normal(p);
    return 100.0 * std::sqrt(std::max(0.0, 1.0 - n.z * n.z)) / std::max(1e-6, n.z);
}
std::string hole(const HoleLayout& h, const std::string& m) { return "Hole " + std::to_string(h.number) + ": " + m; }
}

ValidationReport validateHole(const HoleLayout& h, const CourseConfig& cfg, const Conditions& cond, bool runBot) {
    ValidationReport r;
    const DifficultyParams& dp = difficultyParams(cfg.difficulty);
    auto fail = [&](const std::string& m) { r.fail(hole(h, m)); };

    // Length must suit the par.
    static const double lenMin[] = {0, 0, 0, 90, 235, 385}, lenMax[] = {0, 0, 0, 235, 455, 570};
    if (h.par < 3 || h.par > 5) { fail("par must be 3, 4 or 5"); return r; }
    if (h.lengthM < lenMin[h.par] || h.lengthM > lenMax[h.par]) fail("length does not suit its par");
    if (std::fabs(h.centerlineLength() - h.lengthM) > 1.0) fail("centreline and stated length disagree");
    if (int(h.centerline.size()) != h.par - 1) fail("a par " + std::to_string(h.par) + " needs " + std::to_string(h.par - 2) + " landing zone(s)");
    if (std::fabs(h.elevationChange) > 26.0) fail("elevation change is unwalkable");

    // Tees.
    if (h.tees.size() != 4) fail("expected four tee boxes");
    for (const TeeBox& t : h.tees) {
        if (h.surface(t.pos) != Surface::Tee) fail(std::string(t.name) + " tee is not on a tee surface");
        if (slopePct(h, t.pos) > 3.0) fail(std::string(t.name) + " tee is not level");
    }

    // Green: all of it must be green, and puttable.
    const Ellipse& g = h.green.shape;
    Vec2 major{std::sin(deg2rad(g.rotDeg)), std::cos(deg2rad(g.rotDeg))};
    double worst = 0;
    for (double u = -0.9; u <= 0.901; u += 0.3)
        for (double v = -0.9; v <= 0.901; v += 0.3) {
            if (u * u + v * v > 0.85) continue;
            Vec2 p = g.c + major.perp() * (u * g.rx) + major * (v * g.ry);
            if (h.surface(p) != Surface::Green) fail("a hazard overlaps the putting surface");
            worst = std::max(worst, slopePct(h, p));
        }
    if (worst > 6.5) fail("green is too severe to putt");
    if (std::min(g.rx, g.ry) < 8.0) fail("green is too small");
    if (h.green.pins.empty() || h.pinIndex < 0 || h.pinIndex >= int(h.green.pins.size())) { fail("no pin"); return r; }
    Vec2 pin = h.pin();
    if (g.norm(pin) > 1.0 - 3.5 / std::min(g.rx, g.ry)) fail("pin is closer than 3.5 m to the edge");
    if (slopePct(h, pin) > dp.maxPinSlopePct + 0.5) fail("pin is cut on too steep a slope");

    // Landing zones are real, findable fairway.
    for (const LandingZone& z : h.landingZones) {
        int good = 0, total = 0;
        for (int i = 0; i < 12; ++i) {
            Vec2 p = z.pos + Vec2{std::sin(i * kPi / 6), std::cos(i * kPi / 6)} * (z.radius * 0.7);
            Surface s = h.surface(p);
            ++total;
            if (s == Surface::Fairway || s == Surface::FirstCut) ++good;
        }
        if (h.surface(z.pos) != Surface::Fairway) fail("a landing zone is not fairway");
        if (good * 10 < total * 7) fail("a landing zone is mostly hazard");
    }

    // Walk the centreline: the fairway must be continuous and any forced carry fair.
    Vec3 tee = h.teePosition(cfg.teeIndex);
    double teeS = h.project(tee.xy());
    double run = 0, longest = 0;
    for (double s = teeS; s < h.lengthM; s += 3.0) {
        Surface sf = h.surface(h.pointAt(s));
        if (sf == Surface::Water) { run += 3.0; longest = std::max(longest, s - teeS); }
        else run = 0;
        if (s >= h.fairwayStart + 1.0 && s <= h.fairwayEnd - 1.0 && sf != Surface::Fairway) fail("the fairway is interrupted by " + std::string(surfaceName(sf)));
        if (sf == Surface::OutOfBounds) fail("the line of play leaves the property");
    }
    if (longest > dp.maxForcedCarry) fail("forced carry over water is too long for this difficulty");

    // Each leg must be a shot a golfer on these tees can actually hit.
    double legMax = cfg.difficulty == Difficulty::Beginner ? 215 : 255;
    Vec2 prev = tee.xy();
    for (size_t i = 1; i < h.centerline.size(); ++i) {
        if ((h.centerline[i] - prev).length() > legMax) fail("a leg of the hole is longer than a full shot");
        if (i >= 2) {
            Vec2 a = (h.centerline[i - 1] - h.centerline[i - 2]).normalized(), b = (h.centerline[i] - h.centerline[i - 1]).normalized();
            if (a.dot(b) < std::cos(deg2rad(46.0))) fail("dogleg is too sharp");
        }
        prev = h.centerline[i];
    }

    for (const Tree& t : h.trees) {
        Surface s = h.surface(t.pos);
        if (s != Surface::Rough && s != Surface::DeepRough) { fail("a tree stands on a maintained surface"); break; }
    }

    // The proof: a scratch AI golfer plays the hole with the real physics and must finish sensibly.
    if (r.ok && runBot) {
        BotGolfer bot;
        bot.rng = Rng(0x5EED0000ULL + uint64_t(h.number));
        HolePlay play(h, cond, cfg.teeIndex);
        HoleResult res = bot.playHole(play);
        r.botStrokes.push_back(res.strokes);
        if (!res.holed) fail("the AI golfer could not finish the hole");
        else if (res.strokes > h.par + 2) fail("the AI golfer needed " + std::to_string(res.strokes) + " strokes");
        if (res.penalties > 1) fail("the AI golfer took repeated penalties");
    }
    return r;
}

ValidationReport validateCourse(const Course& c, bool runBot) {
    ValidationReport r;
    int holes = int(c.holes.size());
    if (holes != 9 && holes != 18) r.fail("a course has 9 or 18 holes");
    int par = 0, p3 = 0, p5 = 0;
    for (int i = 0; i < holes; ++i) {
        const HoleLayout& h = c.holes[size_t(i)];
        par += h.par; p3 += h.par == 3; p5 += h.par == 5;
        if (h.number != i + 1) r.fail("holes are misnumbered");
        ValidationReport hr = validateHole(h, c.config, c.conditionsForHole(i), runBot);
        for (auto& e : hr.errors) r.fail(e);
        for (int s : hr.botStrokes) r.botStrokes.push_back(s);
        if (i > 0 && c.holes[size_t(i - 1)].par == 3 && h.par == 3) r.warnings.push_back("back-to-back par 3s at hole " + std::to_string(i + 1));
    }
    int nines = holes / 9;
    if (par != c.totalPar) r.fail("total par is inconsistent");
    if (nines && (par < 34 * nines || par > 37 * nines)) r.fail("total par " + std::to_string(par) + " is outside 34-37 per nine");
    if (c.config.par3Count < 0 && p3 < nines) r.fail("needs at least one par 3 per nine");
    if (c.config.par5Count < 0 && p5 < nines) r.fail("needs at least one par 5 per nine");

    // Routing: holes must not cross or crowd each other, and the walk to the next tee must be short.
    auto sample = [](const HoleLayout& h) {
        std::vector<Vec2> pts;
        double L = h.centerlineLength();
        for (double s = 0; s <= L; s += 10.0) pts.push_back(h.toWorld(h.pointAt(s)));
        pts.push_back(h.toWorld(h.centerline.back()));
        return pts;
    };
    std::vector<std::vector<Vec2>> lines;
    for (const HoleLayout& h : c.holes) lines.push_back(sample(h));
    for (int i = 0; i < holes; ++i) {
        for (int j = i + 1; j < holes; ++j) {
            double need = j == i + 1 ? 18.0 : 50.0;
            double best = 1e9;
            for (Vec2 a : lines[size_t(i)]) for (Vec2 b : lines[size_t(j)]) best = std::min(best, (a - b).length());
            if (best < need) r.fail("holes " + std::to_string(i + 1) + " and " + std::to_string(j + 1) + " overlap");
        }
        if (i + 1 < holes) {
            double walk = (lines[size_t(i)].back() - lines[size_t(i + 1)].front()).length();
            if (walk > 130.0) r.fail("walk from green " + std::to_string(i + 1) + " to the next tee is too long");
        }
    }
    return r;
}

} // namespace golf

#include "test.h"
#include "golf/CourseParams.h"
#include "golf/Round.h"
using namespace golf;

static uint64_t fingerprint(const Course& c) {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](double v) { h ^= uint64_t(int64_t(std::llround(v * 1000.0))); h *= 1099511628211ULL; };
    for (const HoleLayout& hole : c.holes) {
        mix(hole.par); mix(hole.lengthM); mix(hole.pin().x); mix(hole.pin().y); mix(hole.worldHeadingDeg);
        for (const Bunker& b : hole.bunkers) { mix(b.shape.c.x); mix(b.shape.c.y); }
        for (size_t i = 0; i < hole.heights.size(); i += 97) mix(hole.heights[i]);
    }
    mix(c.conditions.wind.speed); mix(c.conditions.wind.headingDeg); mix(c.conditions.stimpFeet);
    return h;
}

TEST(course_same_seed_same_course_different_seed_different_course) {
    CourseConfig a; a.seed = 20261117;
    CourseConfig b = a; b.seed = 20261118;
    Course c1 = generateCourse(a), c2 = generateCourse(a), c3 = generateCourse(b);
    CHECK(!c1.holes.empty());
    CHECK(fingerprint(c1) == fingerprint(c2));
    CHECK(fingerprint(c1) != fingerprint(c3));
}

TEST(course_every_environment_and_difficulty_generates_valid_nines) {
    int n = 0;
    for (int e = 0; e < int(Environment::Count); ++e)
        for (int d = 0; d < 4; ++d) {
            CourseConfig cfg;
            cfg.seed = 1000 + uint64_t(e) * 17 + uint64_t(d);
            cfg.environment = Environment(e);
            cfg.difficulty = Difficulty(d);
            cfg.teeIndex = 3 - d; // beginners forward, championship from the tips
            Course c = generateCourse(cfg);
            CHECK(c.holes.size() == 9);
            if (c.holes.size() != 9) continue;
            CHECK_RANGE(c.totalPar, 34, 37);
            ValidationReport r = validateCourse(c, false);
            CHECK(r.ok);
            for (auto& m : r.errors) std::printf("    %s/%s: %s\n", environmentName(cfg.environment), difficultyName(cfg.difficulty), m.c_str());
            CHECK(c.generationAttempts <= 5);
            ++n;
        }
    CHECK(n == 24);
}

TEST(course_design_rules_hold) {
    CourseConfig cfg; cfg.seed = 777; cfg.environment = Environment::Highlands; cfg.difficulty = Difficulty::Advanced;
    Course c = generateCourse(cfg);
    CHECK(c.holes[0].par == 4); // gentle opening hole
    for (const HoleLayout& h : c.holes) {
        CHECK(h.surface(h.pin()) == Surface::Green);
        CHECK(h.surface(h.teePosition(1).xy()) == Surface::Tee);
        CHECK(int(h.landingZones.size()) == h.par - 3);
        for (const LandingZone& z : h.landingZones) CHECK(h.surface(z.pos) == Surface::Fairway);
        // The front of every green is open: walking in along the approach line never crosses sand.
        Vec2 a = h.centerline[h.centerline.size() - 2], g = h.centerline.back();
        Vec2 dir = (g - a).normalized();
        bool waterCarry = h.tmpl == HoleTemplate::Par3Carry;
        for (double back = 5; back < 40; back += 2.5) {
            Surface s = h.surface(g - dir * (std::max(h.green.shape.rx, h.green.shape.ry) + back));
            CHECK(s != Surface::Sand);
            if (!waterCarry && back < 15) CHECK(s != Surface::Water);
        }
        CHECK(!h.bunkers.empty() || h.par != 3);
    }
}

TEST(course_difficulty_changes_the_architecture) {
    const DifficultyParams& b = difficultyParams(Difficulty::Beginner);
    const DifficultyParams& c = difficultyParams(Difficulty::Championship);
    CHECK(b.fairwayHalfWidth > c.fairwayHalfWidth && b.maxForcedCarry < c.maxForcedCarry && b.stimpFeet < c.stimpFeet);
    CourseConfig easy; easy.seed = 5; easy.difficulty = Difficulty::Beginner;
    CourseConfig hard = easy; hard.difficulty = Difficulty::Championship; hard.hazardDensity = 2;
    Course e = generateCourse(easy), h = generateCourse(hard);
    size_t eh = 0, hh = 0;
    for (auto& x : e.holes) eh += x.bunkers.size() + x.water.size();
    for (auto& x : h.holes) hh += x.bunkers.size() + x.water.size();
    CHECK(hh > eh);
    CHECK(h.lengthM(0) > e.lengthM(0));
}

TEST(course_composition_override) {
    CourseConfig cfg; cfg.seed = 31; cfg.par3Count = 3; cfg.par5Count = 1;
    Course c = generateCourse(cfg);
    int p3 = 0, p5 = 0;
    for (auto& h : c.holes) { p3 += h.par == 3; p5 += h.par == 5; }
    CHECK(p3 == 3 && p5 == 1 && c.totalPar == 34);
}

TEST(share_code_round_trip_and_rejects_typos) {
    CourseConfig cfg; cfg.seed = 0xDEADBEEFCAFEF00DULL; cfg.difficulty = Difficulty::Championship;
    cfg.environment = Environment::Links; cfg.hazardDensity = 3; cfg.pinDifficulty = 2; cfg.teeIndex = 0;
    std::string code = encodeShareCode(cfg);
    CourseConfig out;
    CHECK(decodeShareCode(code, &out));
    CHECK(out.seed == cfg.seed && out.difficulty == cfg.difficulty && out.environment == cfg.environment);
    CHECK(out.hazardDensity == 3 && out.pinDifficulty == 2 && out.teeIndex == 0 && out.holes == 9);
    std::string lower = code;
    for (char& ch : lower) ch = char(std::tolower(ch));
    CHECK(decodeShareCode(lower, &out) && out.seed == cfg.seed);
    std::string bad = code;
    bad[5] = bad[5] == 'A' ? 'B' : 'A';
    CHECK(!decodeShareCode(bad, nullptr));
    CHECK(!decodeShareCode("hello", nullptr));
}

TEST(daily_course_is_shared_and_changes_at_utc_midnight) {
    CHECK(dailyId(dateFromUnix(1700000000)) == "2023-11-14");
    CHECK(dailyId(dateFromUnix(951782400)) == "2000-02-29");
    int64_t t = 1794787200; // 2026-11-16 00:00:00 UTC
    CHECK(dailyId(dateFromUnix(t)) == "2026-11-16");
    CHECK(secondsUntilNextDaily(t) == 86400 && secondsUntilNextDaily(t + 86399) == 1);
    CourseConfig morning = dailyConfig(dateFromUnix(t + 60)), night = dailyConfig(dateFromUnix(t + 86000));
    CourseConfig tomorrow = dailyConfig(dateFromUnix(t + 86400));
    CHECK(morning.seed == night.seed && morning.seed != tomorrow.seed);
    Course a = generateCourse(morning), b = generateCourse(night);
    CHECK(!a.holes.empty() && fingerprint(a) == fingerprint(b));
}

TEST(round_bots_finish_every_hole_and_skill_matters) {
    CourseConfig cfg; cfg.seed = 4242; cfg.environment = Environment::Coastal; cfg.difficulty = Difficulty::Advanced;
    Course c = generateCourse(cfg);
    int expertTotal = 0, hackerTotal = 0;
    for (int r = 0; r < 3; ++r) {
        BotGolfer expert; expert.rng = Rng(uint64_t(r) + 1);
        BotGolfer hacker; hacker.skill = 0.2; hacker.rng = Rng(uint64_t(r) + 50);
        Scorecard a = playRound(c, expert), b = playRound(c, hacker);
        for (auto& h : a.holes) CHECK(h.holed);
        for (auto& h : b.holes) CHECK(h.holed);
        CHECK(a.putts() >= 9);
        expertTotal += a.strokes(); hackerTotal += b.strokes();
    }
    CHECK(hackerTotal > expertTotal + 9);
}

// A flat test hole with water in front of the green and out-of-bounds everywhere else.
struct RulesHole : HoleLayout {
    RulesHole() {
        par = 4; number = 1; lengthM = 300;
        centerline = {{0, 0}, {0, 200}, {0, 300}};
        fairwayStart = 20; fairwayEnd = 285; fairwayHalfWidth = 20; corridorHalfWidth = 40;
        green.shape = {{0, 300}, 12, 12, 0};
        green.pins = {{0, 300}};
        tees = {{"Championship", {0, 0}, 300}, {"Back", {0, 0}, 300}, {"Middle", {0, 0}, 300}, {"Forward", {0, 0}, 300}};
        landingZones = {{{0, 200}, 18}};
        water.push_back({{0, 250}, 60, 15, 0});
        gridMin = {-60, -60}; cell = 4; nx = 31; ny = 106;
        heights.assign(size_t(nx) * ny, 0.0f);
    }
};

TEST(rules_water_penalty_drop_and_out_of_bounds) {
    RulesHole h;
    Conditions cond;
    Launch L; L.ballSpeed = clubSpec(ClubId::Driver).ballSpeed; L.launchDeg = 11; L.spinRpm = 2700;
    {   // Driver straight at a pond 235-265 m out.
        HolePlay play(h, cond, 0);
        ShotOutcome o = play.playShot(L, 0);
        CHECK(o.reason == RestReason::Water);
        CHECK(play.result().strokes == 2 && play.result().penalties == 1);
        CHECK(h.surface(play.ball().xy()) != Surface::Water);
        CHECK(play.ball().y < 236 && play.ball().y > 150);
        CHECK(!play.result().fairwayHit);
    }
    {   // 60 degrees right: out of bounds, replay from the tee.
        HolePlay play(h, cond, 0);
        ShotOutcome o = play.playShot(L, 60);
        CHECK(o.reason == RestReason::OutOfBounds);
        CHECK(play.result().strokes == 2 && play.ball().xy().length() < 0.01 && play.lie() == Surface::Tee);
    }
    {   // A 7-iron finds the fairway.
        HolePlay play(h, cond, 0);
        Launch I; I.ballSpeed = 53.6; I.launchDeg = 16.3; I.spinRpm = 7100;
        play.playShot(I, 0);
        CHECK(play.result().strokes == 1 && play.result().fairwayHit && play.lie() == Surface::Fairway);
    }
}

TEST(rules_green_in_regulation_needs_two_strokes_in_hand) {
    RulesHole h;                       // par 4, pin at (0, 300)
    Conditions cond;
    auto chipIn = [&](int strokesBefore) {
        HolePlay play(h, cond, 0);
        Launch lay; lay.ballSpeed = 30; lay.launchDeg = 30; lay.spinRpm = 6000;   // short lay-ups down the fairway
        for (int i = 0; i < strokesBefore; ++i) play.playShot(lay, 0);
        return play;
    };
    {   // Reaching the green with the second shot on a par 4 is a GIR.
        HolePlay play(h, cond, 0);
        Launch drive; drive.ballSpeed = 62; drive.launchDeg = 12; drive.spinRpm = 3000;
        play.playShot(drive, 0);
        CHECK(play.result().strokes == 1 && !play.result().gir);
    }
    {   // Three strokes used and still not on the green: whatever happens next is not a GIR.
        HolePlay play = chipIn(3);
        CHECK(play.result().strokes == 3 && !play.result().gir);
        Launch chip; chip.ballSpeed = 8; chip.launchDeg = 20; chip.spinRpm = 2000;
        for (int i = 0; i < 6 && play.lie() != Surface::Green && !play.holed(); ++i) play.playShot(chip, play.headingTo(h.pin()));
        CHECK(!play.result().gir);
    }
}

TEST(putting_tap_ins_are_playable_with_a_real_stroke) {
    // The smallest stroke the swing analyser accepts must be a gentle tap, and a 60% stroke a long lag.
    SwingSettings s = SwingSettings::forDifficulty(SwingDifficulty::Simulation);
    SyntheticSwing tiny; tiny.power = 0.12;
    SyntheticSwing big; big.power = 0.6;
    FlatTerrain g(Surface::Green);
    SwingResult a = runSwing(synthesizeSwing(tiny, s), s), b = runSwing(synthesizeSwing(big, s), s);
    CHECK(a.valid && b.valid);
    double shortRoll = simulatePutt(computePutt(a).speed, {0, 0, 0}, 0, g, {}, {}).total;
    double longRoll = simulatePutt(computePutt(b).speed, {0, 0, 0}, 0, g, {}, {}).total;
    CHECK_RANGE(shortRoll, 0.01, 0.6);
    CHECK_RANGE(longRoll, 4.0, 14.0);
}

TEST(scoring_names_and_scorecard_totals) {
    CHECK(std::string(scoreName(1, 3)) == "Hole in One");
    CHECK(std::string(scoreName(3, 5)) == "Eagle");
    CHECK(std::string(scoreName(3, 4)) == "Birdie");
    CHECK(std::string(scoreName(4, 4)) == "Par");
    CHECK(std::string(scoreName(6, 4)) == "Double Bogey");
    Scorecard s;
    HoleResult a; a.par = 4; a.strokes = 3; a.putts = 1; a.holed = true; a.gir = true; a.fairwayApplies = true; a.fairwayHit = true;
    HoleResult b; b.par = 3; b.strokes = 4; b.putts = 2; b.holed = true;
    s.holes = {a, b};
    CHECK(s.strokes() == 7 && s.par() == 7 && s.toPar() == 0 && s.putts() == 3);
    CHECK(s.countScore(-1) == 1 && s.countScore(1) == 1 && s.greensInRegulation() == 1);
    CHECK(s.fairwaysHit() == 1 && s.fairwaysPossible() == 1);
}

TEST(traditional_rules_never_enable_chaos) {
    constexpr GameRules r = GameRules::traditional();
    static_assert(!r.chaos && !r.powerShotsAllowed, "traditional golf must stay traditional");
    CHECK(!r.chaos);
}

// ---- Terrain realism ----------------------------------------------------------------------------

static double slopeAt(const ITerrain& t, Vec2 p) {
    Vec3 n = t.normal(p);
    return 100.0 * std::sqrt(std::max(0.0, 1.0 - n.z * n.z)) / n.z;
}

TEST(terrain_greens_have_real_contours_and_puttable_pins) {
    int greens = 0, tiered = 0;
    for (int d = 0; d < 4; ++d) {
        CourseConfig cfg; cfg.seed = 31337 + uint64_t(d); cfg.difficulty = Difficulty(d); cfg.environment = Environment(d % 6);
        Course c = generateCourse(cfg);
        const DifficultyParams& dp = difficultyParams(cfg.difficulty);
        for (const HoleLayout& h : c.holes) {
            const Ellipse& g = h.green.shape;
            // Height relief across the green after removing its overall tilt.
            Vec2 up{std::sin(deg2rad(h.green.tiltHeadingDeg)), std::cos(deg2rad(h.green.tiltHeadingDeg))};
            double lo = 1e9, hi = -1e9, steep = 0;
            int n = 0, over2 = 0;
            for (double u = -0.85; u <= 0.85; u += 0.1)
                for (double v = -0.85; v <= 0.85; v += 0.1) {
                    if (u * u + v * v > 0.72) continue;
                    Vec2 p = g.c + Vec2{u * g.rx, v * g.ry};
                    double z = h.height(p) - (p - g.c).dot(up) * h.green.tiltPct * 0.01;
                    lo = std::min(lo, z); hi = std::max(hi, z);
                    double sl = slopeAt(h, p);
                    steep = std::max(steep, sl);
                    ++n; if (sl > 2.0) ++over2;
                }
            ++greens;
            CHECK(hi - lo > (d == 0 ? 0.08 : 0.15));          // not a flat plate
            if (hi - lo > 0.35) ++tiered;
            CHECK(steep < 11.0);
            if (d >= 1) CHECK(over2 > n / 10);                // a real share of the green has putt-bending slope (Beginner greens are gentle by design)
            CHECK(slopeAt(h, h.pin()) <= dp.maxPinSlopePct + 0.6);
            // Pins are spread over the green, not stacked in one spot.
            double spread = 0;
            for (Vec2 p : h.green.pins) spread = std::max(spread, (p - h.green.pins[0]).length());
            CHECK(spread > 4.0);
        }
    }
    CHECK(tiered * 5 > greens);   // plenty of greens with big contour changes
}

TEST(terrain_fairways_roll_but_stay_playable) {
    for (int e = 0; e < int(Environment::Count); ++e) {
        CourseConfig cfg; cfg.seed = 4400 + uint64_t(e); cfg.environment = Environment(e);
        Course c = generateCourse(cfg);
        double steepest = 0, sumDev = 0; int samples = 0;
        for (const HoleLayout& h : c.holes) {
            if (h.par == 3) continue;
            // Detrended height along the fairway centreline: how much it rises and falls.
            double a = h.fairwayStart + 10, b = h.fairwayEnd - 10;
            double za = h.height(h.pointAt(a)), zb = h.height(h.pointAt(b));
            for (double s = a; s <= b; s += 4) {
                Vec2 p = h.pointAt(s);
                double trend = lerp(za, zb, (s - a) / (b - a));
                sumDev += std::fabs(h.height(p) - trend); ++samples;
                if (h.surface(p) == Surface::Fairway) steepest = std::max(steepest, slopeAt(h, p));
            }
        }
        double meanDev = sumDev / samples;
        CHECK(meanDev > 0.25);        // you can see the ground move
        CHECK(steepest < 16.0);       // but a ball still comes to rest on the fairway
    }
}

TEST(terrain_height_is_continuous_at_the_green_patch_edge) {
    CourseConfig cfg; cfg.seed = 77; cfg.environment = Environment::Highlands;
    Course c = generateCourse(cfg);
    for (const HoleLayout& h : c.holes) {
        double x0 = h.fineMin.x, x1 = h.fineMin.x + (h.fnx - 1) * h.fineCell;
        for (double y = h.fineMin.y + 1; y < h.fineMin.y + (h.fny - 1) * h.fineCell - 1; y += 1.7)
            for (double x : {x0, x1})
                for (double e : {-0.01, 0.01})
                    CHECK(std::fabs(h.height({x + e, y}) - h.height({x - e, y})) < 0.02);
    }
}

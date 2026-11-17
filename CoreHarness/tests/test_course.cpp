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

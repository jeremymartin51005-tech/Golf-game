#include "test.h"
#include "golf/Round.h"
using namespace golf;

static bool finite(const ShotOutcome& o) {
    return std::isfinite(o.rest.x) && std::isfinite(o.rest.y) && std::isfinite(o.rest.z) && std::isfinite(o.carry) && std::isfinite(o.total);
}

TEST(robust_swing_analyser_survives_garbage_input) {
    Rng rng(123);
    int valid = 0;
    for (int trial = 0; trial < 3000; ++trial) {
        SwingSettings s = SwingSettings::forDifficulty(SwingDifficulty(trial % 4));
        s.sensitivity = rng.range(0.2, 4.0);
        MouseSwing sw(s);
        double t = rng.range(0, 1000);
        int n = rng.rangeInt(1, 400);
        for (int i = 0; i < n && !sw.finished(); ++i) {
            t += rng.chance(0.05) ? 0.0 : rng.range(0.0005, 0.05);          // duplicate timestamps happen
            double scale = rng.chance(0.02) ? 5000.0 : 40.0;                // and so do huge spikes
            sw.addSample({t, rng.signedUnit() * scale, rng.signedUnit() * scale + (i < n / 2 ? 6.0 : -10.0)});
            if (rng.chance(0.1)) sw.tick(t + rng.range(0, 0.3));
        }
        sw.tick(t + 1.0);
        const SwingResult& r = sw.result();
        if (!r.valid) continue;
        ++valid;
        CHECK(std::isfinite(r.power) && r.power >= 0 && r.power <= 1.0);
        CHECK(std::isfinite(r.speedFactor) && r.speedFactor >= 0 && r.speedFactor < 1.3);
        CHECK(std::isfinite(r.faceDeg) && std::fabs(r.faceDeg) <= 12.0 && std::fabs(r.pathDeg) <= 10.0);
        CHECK(r.contactQuality > 0.2 && r.contactQuality <= 1.0);
        CHECK(!r.feedback.empty());
        Launch L = computeLaunch(r, ClubId(trial % 16), {}, Surface(trial % 9));
        CHECK(std::isfinite(L.ballSpeed) && L.ballSpeed >= 0 && std::isfinite(L.spinAxisDeg) && std::isfinite(L.launchDeg));
    }
    CHECK(valid > 100);
}

TEST(robust_any_shot_on_a_generated_hole_ends_cleanly) {
    CourseConfig cfg; cfg.seed = 606; cfg.environment = Environment::Alpine; cfg.difficulty = Difficulty::Championship; cfg.hazardDensity = 2;
    Course c = generateCourse(cfg);
    Rng rng(9);
    int timedOut = 0, shots = 0;
    for (const HoleLayout& h : c.holes) {
        for (int i = 0; i < 60; ++i) {
            // Start anywhere in the corridor, hit anything in any direction.
            Vec2 p = h.pointAt(rng.range(0, h.lengthM)) + Vec2{rng.range(-40, 40), rng.range(-10, 10)};
            if (h.surface(p) == Surface::Water || h.surface(p) == Surface::OutOfBounds) continue;
            Launch L;
            L.ballSpeed = rng.range(0.5, 80); L.launchDeg = rng.range(1, 60); L.spinRpm = rng.range(0, 11000); L.spinAxisDeg = rng.range(-45, 45);
            ShotOutcome o = simulateShot(L, {p.x, p.y, h.height(p)}, rng.range(0, 360), h, c.conditionsForHole(h.number - 1), Cup{true, h.pin()});
            ++shots;
            CHECK(finite(o));
            if (o.reason == RestReason::TimedOut) ++timedOut;
            if (o.reason == RestReason::Stopped) CHECK(h.surface(o.rest.xy()) != Surface::Water && h.surface(o.rest.xy()) != Surface::OutOfBounds);
            if (o.reason == RestReason::Water) CHECK(h.surface(o.hazardEntry.xy()) != Surface::Water);
            ShotOutcome putt = simulatePutt(rng.range(0.05, 8), {p.x, p.y, 0}, rng.range(0, 360), h, c.conditionsForHole(h.number - 1), Cup{true, h.pin()});
            CHECK(finite(putt));
        }
    }
    CHECK(shots > 300);
    CHECK(timedOut * 50 < shots); // a ball that never comes to rest must be rare
}

TEST(robust_weak_golfers_always_finish_a_round) {
    for (uint64_t seed : {11ull, 12ull, 13ull}) {
        CourseConfig cfg; cfg.seed = seed; cfg.environment = Environment(seed % 6); cfg.difficulty = Difficulty::Championship; cfg.hazardDensity = 3; cfg.pinDifficulty = 3;
        Course c = generateCourse(cfg);
        CHECK(c.holes.size() == 9);
        BotGolfer hacker; hacker.skill = 0.0; hacker.attr = PlayerAttributes{0.1, 0.1, 0.1, 0.1, 0.1, 0.1}; hacker.rng = Rng(seed);
        Scorecard card = playRound(c, hacker);
        CHECK(card.holes.size() == 9);
        for (const HoleResult& h : card.holes) { CHECK(h.strokes >= 1 && h.strokes <= 14); CHECK(h.putts <= h.strokes); }
    }
}

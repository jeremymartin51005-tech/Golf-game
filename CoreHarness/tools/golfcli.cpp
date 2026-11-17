// Command-line front end to the simulation core: calibration tables and (later) course tools.
#include <cstdio>
#include <cstring>
#include <chrono>
#include <cstdlib>
#include "golf/BallFlight.h"
#include "golf/Round.h"

using namespace golf;

static void distances() {
    FlatTerrain range(Surface::Fairway);
    Conditions cond;
    SwingSettings s;
    PlayerAttributes scratch{1, 1, 1, 1, 1, 1};
    std::printf("%-15s %7s %7s %7s %6s %7s %6s %6s\n", "club", "ball", "launch", "spin", "apex", "carry", "total", "land");
    for (ClubId id : allClubs()) {
        if (id == ClubId::Putter) continue;
        SwingResult sw = runSwing(synthesizeSwing({}, s), s, scratch);
        Launch L = computeLaunch(sw, id, {}, Surface::Fairway, scratch);
        ShotOutcome o = simulateShot(L, {0, 0, 0}, 0, range, cond);
        std::printf("%-15s %6.1f  %6.1f  %6.0f  %5.1f  %6.1f  %5.1f  %5.1f   (%.0f yd carry)\n", clubSpec(id).name,
                    L.ballSpeed, L.launchDeg, L.spinRpm, o.apex, o.carry, o.total, o.landingAngleDeg, o.carry / 0.9144);
    }
}

static void bench() {
    using clock = std::chrono::steady_clock;
    CourseConfig cfg; cfg.seed = 99; cfg.environment = Environment::Alpine;
    Course c = generateCourse(cfg);
    const HoleLayout& h = c.holes[0];
    Launch L; L.ballSpeed = clubSpec(ClubId::Driver).ballSpeed; L.launchDeg = 10.9; L.spinRpm = 2686; L.spinAxisDeg = 4;
    FlightParams fp; fp.recordPath = false;
    int n = 300; double simTime = 0;
    auto t0 = clock::now();
    for (int i = 0; i < n; ++i) simTime += simulateShot(L, h.teePosition(1), 0, h, c.conditions, {}, fp).totalTime;
    double ms = std::chrono::duration<double, std::milli>(clock::now() - t0).count();
    double perStepUs = ms * 1000.0 / (simTime * 240.0);
    std::printf("driver on generated terrain: %.3f ms per whole shot, %.2f us per 240 Hz physics step\n", ms / n, perStepUs);
    std::printf("  -> %.3f%% of a 16.67 ms frame when stepped live at 240 Hz (4 steps/frame)\n", perStepUs * 4 / 16670.0 * 100.0);
    SwingSettings s; auto samples = synthesizeSwing({}, s);
    t0 = clock::now();
    for (int i = 0; i < 2000; ++i) runSwing(samples, s);
    double us = std::chrono::duration<double, std::micro>(clock::now() - t0).count() / 2000;
    std::printf("swing analyser: %.1f us per complete swing of %zu samples (%.3f us per mouse event)\n", us, samples.size(), us / samples.size());
    t0 = clock::now();
    for (int i = 0; i < 10; ++i) { cfg.seed = 500 + i; generateCourse(cfg); }
    std::printf("nine-hole course generate + validate: %.0f ms average\n", std::chrono::duration<double, std::milli>(clock::now() - t0).count() / 10);
}

static void printCourse(const Course& c, bool play) {
    if (c.holes.empty()) { std::printf("generation failed\n"); return; }
    std::printf("%s  [%s]\n%s, %s, par %d, %d m from the %s tees  (attempt %d)\n", c.name.c_str(), encodeShareCode(c.config).c_str(),
                environmentName(c.config.environment), difficultyName(c.config.difficulty), c.totalPar,
                c.lengthM(c.config.teeIndex), c.holes[0].tees[size_t(c.config.teeIndex)].name, c.generationAttempts);
    std::printf("%s, wind %.1f m/s toward %.0f deg, %.0f C, stimp %.1f\n\n", weatherName(c.weather), c.conditions.wind.speed,
                c.conditions.wind.headingDeg, c.conditions.temperatureC, c.conditions.stimpFeet);
    BotGolfer scratch, amateur;
    amateur.skill = 0.55; amateur.rng = Rng(c.config.seed ^ 99);
    Scorecard a, b;
    if (play) { a = playRound(c, scratch); b = playRound(c, amateur); }
    std::printf("%-3s %-3s %-5s %-5s %-26s %-4s %-4s %-5s %-5s %s\n", "#", "par", "len", "elev", "design", "bnk", "wtr", "trees", "tilt%", play ? "expert / amateur" : "");
    for (size_t i = 0; i < c.holes.size(); ++i) {
        const HoleLayout& h = c.holes[i];
        std::printf("%-3d %-3d %-5.0f %+-5.1f %-26s %-4zu %-4zu %-5zu %-5.1f ", h.number, h.par, h.tees[size_t(c.config.teeIndex)].lengthM,
                    h.elevationChange, templateName(h.tmpl), h.bunkers.size(), h.water.size(), h.trees.size(), h.green.tiltPct);
        if (play) std::printf("%d (%s) / %d", a.holes[i].strokes, scoreName(a.holes[i].strokes, h.par), b.holes[i].strokes);
        std::printf("\n");
    }
    if (play) {
        std::printf("\nexpert bot:  %d (%+d)  putts %d  fairways %d/%d  GIR %d/%zu  penalties %d\n", a.strokes(), a.toPar(), a.putts(),
                    a.fairwaysHit(), a.fairwaysPossible(), a.greensInRegulation(), a.holes.size(), a.penalties());
        std::printf("amateur bot: %d (%+d)  putts %d  fairways %d/%d  GIR %d/%zu  penalties %d\n", b.strokes(), b.toPar(), b.putts(),
                    b.fairwaysHit(), b.fairwaysPossible(), b.greensInRegulation(), b.holes.size(), b.penalties());
    }
}

int main(int argc, char** argv) {
    if (argc > 1 && !std::strcmp(argv[1], "distances")) { distances(); return 0; }
    if (argc > 1 && !std::strcmp(argv[1], "bench")) { bench(); return 0; }
    if (argc > 1 && !std::strcmp(argv[1], "fingerprints")) { // compared against the WebAssembly build
        for (int i = 0; i < 12; ++i) {
            CourseConfig cfg; cfg.seed = 9000 + uint64_t(i) * 7; cfg.environment = Environment(i % 6); cfg.difficulty = Difficulty(i % 4);
            Course c = generateCourse(cfg);
            std::printf("%d %016llx %s\n", i, (unsigned long long)courseFingerprint(c), c.name.c_str());
        }
        return 0;
    }
    if (argc > 2 && !std::strcmp(argv[1], "course")) {
        CourseConfig cfg;
        if (!decodeShareCode(argv[2], &cfg)) {
            cfg.seed = std::strtoull(argv[2], nullptr, 10);
            if (argc > 3) cfg.environment = Environment(std::atoi(argv[3]) % int(Environment::Count));
            if (argc > 4) cfg.difficulty = Difficulty(std::atoi(argv[4]) % 4);
        }
        auto t0 = std::chrono::steady_clock::now();
        Course c = generateCourse(cfg);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        printCourse(c, true);
        std::printf("generated and validated in %.0f ms\n", ms);
        return c.holes.empty() ? 1 : 0;
    }
    if (argc > 2 && !std::strcmp(argv[1], "daily")) {
        Date d = dateFromUnix(std::strtoll(argv[2], nullptr, 10));
        std::printf("Daily Course %s\n", dailyId(d).c_str());
        printCourse(generateCourse(dailyConfig(d)), true);
        return 0;
    }
    std::printf("usage: golfcli distances | bench | course <seed|share-code> [environment 0-5] [difficulty 0-3] | daily <unix-time>\n");
    return 1;
}

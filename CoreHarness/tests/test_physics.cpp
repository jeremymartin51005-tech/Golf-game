#include "test.h"
#include "golf/BallFlight.h"
using namespace golf;

static Launch stock(ClubId id) {
    const ClubSpec& c = clubSpec(id);
    Launch L; L.ballSpeed = c.ballSpeed; L.launchDeg = c.launchDeg; L.spinRpm = c.spinRpm;
    return L;
}
static ShotOutcome fly(const Launch& L, Conditions c = {}, Surface s = Surface::Fairway) {
    FlatTerrain t(s);
    return simulateShot(L, {0, 0, 0}, 0, t, c);
}
static double yd(double m) { return m / 0.9144; }

TEST(physics_carry_matches_tour_launch_monitor_data) {
    struct Ref { ClubId id; double carryYd; } refs[] = {
        {ClubId::Driver, 275}, {ClubId::Wood3, 243}, {ClubId::Wood5, 230}, {ClubId::Iron3, 212}, {ClubId::Iron5, 194},
        {ClubId::Iron7, 172}, {ClubId::Iron9, 148}, {ClubId::PitchingWedge, 136}};
    for (const Ref& r : refs) {
        ShotOutcome o = fly(stock(r.id));
        CHECK_RANGE(yd(o.carry), r.carryYd - 10, r.carryYd + 10);
        CHECK_RANGE(o.apex, 20, 36);
    }
}

TEST(physics_clubs_are_gapped_in_order) {
    double prev = 1e9;
    for (ClubId id : Bag::standard().clubs) {
        if (id == ClubId::Putter) continue;
        double c = fly(stock(id)).carry;
        CHECK(c < prev - 3.0);
        prev = c;
    }
    CHECK(Bag::standard().valid());
    Bag b = Bag::standard(); b.clubs.push_back(ClubId::Iron3);
    CHECK(!b.valid());
}

TEST(physics_wind_temperature_altitude) {
    Launch L = stock(ClubId::Iron7);
    double calm = fly(L).carry;
    Conditions head; head.wind = {8, 180};
    Conditions tail; tail.wind = {8, 0};
    Conditions cross; cross.wind = {8, 90};
    CHECK(fly(L, head).carry < calm - 12);
    CHECK(fly(L, tail).carry > calm + 6);
    CHECK(calm - fly(L, head).carry > fly(L, tail).carry - calm); // headwind hurts more than tailwind helps
    CHECK(fly(L, cross).lateralAtRest > 8);
    Conditions cold; cold.temperatureC = 2;
    Conditions high; high.altitudeM = 1600;
    Launch D = stock(ClubId::Driver);
    CHECK(fly(D, cold).carry < fly(D).carry);
    CHECK(fly(D, high).carry > fly(D).carry + 8);
}

TEST(physics_spin_axis_curves_the_ball) {
    Launch L = stock(ClubId::Driver);
    L.spinAxisDeg = 10;
    ShotOutcome fade = fly(L);
    L.spinAxisDeg = -10;
    ShotOutcome draw = fly(L);
    L.spinAxisDeg = 30;
    ShotOutcome slice = fly(L);
    CHECK(fade.curveM > 8 && draw.curveM < -8);
    CHECK_RANGE(fade.curveM + draw.curveM, -0.01, 0.01);
    CHECK(slice.curveM > fade.curveM * 2 && slice.carry < fade.carry);
}

TEST(physics_surfaces_behave_differently) {
    Launch L = stock(ClubId::Driver);
    double fw = fly(L, {}, Surface::Fairway).total - fly(L, {}, Surface::Fairway).carry;
    double rough = fly(L, {}, Surface::Rough).total - fly(L, {}, Surface::Rough).carry;
    double sand = fly(L, {}, Surface::Sand).total - fly(L, {}, Surface::Sand).carry;
    double hard = fly(L, {}, Surface::HardGround).total - fly(L, {}, Surface::HardGround).carry;
    CHECK_RANGE(fw, 8, 35);
    CHECK(rough < fw * 0.7);
    CHECK(sand < 3.0);
    CHECK(hard > fw * 1.3);
    Conditions wet; wet.wetness = 1;
    CHECK(fly(L, wet).total - fly(L, wet).carry < fw);
    // A wedge into a green checks up; a low-spin shot releases.
    Launch W = stock(ClubId::SandWedge);
    ShotOutcome spinner = fly(W, {}, Surface::Green);
    W.spinRpm = 2500;
    ShotOutcome dead = fly(W, {}, Surface::Green);
    CHECK(spinner.total - spinner.carry < dead.total - dead.carry - 2.0);
}

TEST(physics_slope_affects_roll) {
    Launch L = stock(ClubId::Iron7);
    FlatTerrain down(Surface::Fairway, 0, -0.06), up(Surface::Fairway, 0, 0.06);
    double rd = simulateShot(L, {0, 0, 0}, 0, down, {}).total;
    double ru = simulateShot(L, {0, 0, 0}, 0, up, {}).total;
    CHECK(rd > ru + 10);
}

TEST(physics_timestep_convergence) {
    Launch L = stock(ClubId::Driver); L.spinAxisDeg = 8;
    FlatTerrain t;
    FlightParams fine; fine.dt = 1.0 / 1000.0;
    FlightParams game; game.dt = 1.0 / 120.0;
    ShotOutcome a = simulateShot(L, {0, 0, 0}, 0, t, {}, {}, fine);
    ShotOutcome b = simulateShot(L, {0, 0, 0}, 0, t, {}, {}, game);
    CHECK_RANGE(std::fabs(a.carry - b.carry), 0, 1.0);
}

// ---- Putting ------------------------------------------------------------------------------------

TEST(putting_stimp_definition_holds) {
    for (double stimp : {8.0, 10.5, 13.0}) {
        FlatTerrain g(Surface::Green);
        Conditions c; c.stimpFeet = stimp;
        ShotOutcome o = simulatePutt(1.83, {0, 0, 0}, 0, g, c, {});
        CHECK_RANGE(o.total / 0.3048, stimp - 0.3, stimp + 0.3);
    }
}

TEST(putting_breaks_downhill_and_runs_out) {
    FlatTerrain side(Surface::Green, 0.03, 0);   // high side on the right, ball falls left
    ShotOutcome o = simulatePutt(3.0, {0, 0, 0}, 0, side, {}, {});
    CHECK(o.rest.x < -0.3);
    FlatTerrain down(Surface::Green, 0, -0.03), up(Surface::Green, 0, 0.03);
    CHECK(simulatePutt(2.5, {0, 0, 0}, 0, down, {}, {}).total > simulatePutt(2.5, {0, 0, 0}, 0, up, {}, {}).total * 1.5);
}

TEST(putting_cup_capture_and_lip_out) {
    FlatTerrain g(Surface::Green);
    Cup cup{true, {0, 3.0}};
    Conditions c;
    auto putt = [&](double speed, double offsetM) {
        double aim = rad2deg(std::atan2(offsetM, 3.0));
        return simulatePutt(speed, {0, 0, 0}, aim, g, c, cup);
    };
    CHECK(putt(2.0, 0.0).reason == RestReason::Holed);        // good pace, dead centre
    CHECK(putt(1.6, 0.0).reason != RestReason::Holed);        // short
    CHECK(putt(5.0, 0.0).reason != RestReason::Holed);        // far too fast: goes over the hole
    CHECK(putt(2.0, 0.08).reason != RestReason::Holed);       // wide
    CHECK(putt(1.95, 0.04).reason == RestReason::Holed);      // dies in the side door
    ShotOutcome lip = putt(3.0, 0.045);                       // firm and on the edge: lips out
    CHECK(lip.reason != RestReason::Holed);
    ShotOutcome noCup = simulatePutt(3.0, {0, 0, 0}, rad2deg(std::atan2(0.045, 3.0)), g, c, {});
    CHECK(std::fabs(lip.rest.x - noCup.rest.x) > 0.05);       // ...and is visibly deflected by the rim
}

TEST(putting_read_finds_the_line_on_a_breaking_putt) {
    FlatTerrain g(Surface::Green, 0.025, 0.01);
    Cup cup{true, {0.5, 6.0}};
    PuttRead r = readPutt({0, 0, 0}, cup, g, {});
    CHECK(r.found);
    CHECK(r.breakM > 0.15); // must aim uphill (right) of the hole
    // Aiming straight at the hole with the same pace misses low.
    double direct = rad2deg(std::atan2(0.5, 6.0));
    CHECK(simulatePutt(r.speed, {0, 0, 0}, direct, g, {}, cup).reason != RestReason::Holed);
}

TEST(putting_stroke_maps_to_pace_and_line) {
    SwingSettings s = SwingSettings::forDifficulty(SwingDifficulty::Simulation);
    SyntheticSwing soft; soft.power = 0.25;
    SyntheticSwing firm; firm.power = 0.5;
    SyntheticSwing pushed = soft; pushed.pathAngleDeg = 15;
    PuttLaunch a = computePutt(runSwing(synthesizeSwing(soft, s), s));
    PuttLaunch b = computePutt(runSwing(synthesizeSwing(firm, s), s));
    PuttLaunch c = computePutt(runSwing(synthesizeSwing(pushed, s), s));
    CHECK(b.speed > a.speed * 1.7);
    CHECK(std::fabs(a.azimuthDeg) < 0.05 && c.azimuthDeg > 0.3);
}

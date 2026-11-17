#include "test.h"
#include "golf/BallFlight.h"
using namespace golf;

static SwingSettings sim() { return SwingSettings::forDifficulty(SwingDifficulty::Simulation); }
static const PlayerAttributes kPro{1, 1, 1, 1, 1, 1};

static ShotOutcome hit(const SyntheticSwing& p, ClubId club = ClubId::Driver, ShotIntent intent = {},
                       SwingSettings s = sim(), Launch* launchOut = nullptr, SwingResult* swOut = nullptr) {
    SwingResult sw = runSwing(synthesizeSwing(p, s), s, kPro);
    Launch L = computeLaunch(sw, club, intent, Surface::Fairway, kPro);
    if (launchOut) *launchOut = L;
    if (swOut) *swOut = sw;
    FlatTerrain range;
    return simulateShot(L, {0, 0, 0}, 0, range, Conditions{});
}

TEST(swing_perfect_is_pure_and_straight) {
    SwingResult sw; Launch L;
    ShotOutcome o = hit({}, ClubId::Driver, {}, sim(), &L, &sw);
    CHECK(sw.valid);
    CHECK(sw.contact == Contact::Pure);
    CHECK_RANGE(sw.power, 0.98, 1.0);
    CHECK_RANGE(sw.tempoRatio, 2.7, 3.3);
    CHECK_RANGE(std::fabs(sw.faceDeg), 0, 0.2);
    CHECK_RANGE(std::fabs(o.lateralAtRest), 0, 2.0);
    CHECK(std::string(describeShape(L.azimuthDeg, L.spinAxisDeg)) == "Straight");
}

TEST(swing_is_deterministic_and_rate_independent) {
    // Same motion sampled by a 125 Hz office mouse and a 1000 Hz gaming mouse must give the same shot.
    SyntheticSwing a; a.pathAngleDeg = 6; a.downTimeScale = 0.85;
    SyntheticSwing b = a; b.rateHz = 125;
    ShotOutcome o1 = hit(a), o2 = hit(a), o3 = hit(b);
    CHECK(o1.rest.x == o2.rest.x && o1.rest.y == o2.rest.y);
    CHECK_RANGE(std::fabs(o1.carry - o3.carry), 0, 4.0);
    CHECK_RANGE(std::fabs(o1.lateralAtRest - o3.lateralAtRest), 0, 5.0);
}

TEST(swing_sensitivity_calibration_cancels_out) {
    SwingSettings s = sim(); s.sensitivity = 2.5;
    SwingResult a = runSwing(synthesizeSwing({}, sim()), sim(), kPro);
    SwingResult b = runSwing(synthesizeSwing({}, s), s, kPro);
    CHECK_RANGE(std::fabs(a.power - b.power), 0, 0.01);
    CHECK_RANGE(std::fabs(a.speedFactor - b.speedFactor), 0, 0.01);
}

TEST(swing_backswing_length_controls_distance) {
    double prev = 0;
    for (double p : {0.3, 0.5, 0.7, 0.85, 1.0}) {
        SyntheticSwing s; s.power = p;
        ShotOutcome o = hit(s, ClubId::Iron7);
        CHECK(o.carry > prev + 5);
        prev = o.carry;
    }
}

TEST(swing_rushed_hooks_and_slow_slices) {
    SyntheticSwing fast; fast.downTimeScale = 0.70;
    SyntheticSwing slow; slow.downTimeScale = 1.35;
    SwingResult swF, swS; Launch LF, LS;
    ShotOutcome oF = hit(fast, ClubId::Driver, {}, sim(), &LF, &swF);
    ShotOutcome oS = hit(slow, ClubId::Driver, {}, sim(), &LS, &swS);
    ShotOutcome ok = hit({});
    CHECK(swF.timingError < 0 && swF.faceDeg < -1.0);
    CHECK(swS.timingError > 0 && swS.faceDeg > 1.0);
    CHECK(oF.lateralAtRest < -10);  // left
    CHECK(oS.lateralAtRest > 10);   // right
    CHECK(oS.carry < ok.carry - 10); // a lazy swing also loses distance
    CHECK(LF.spinAxisDeg < -3 && LS.spinAxisDeg > 3);
}

TEST(swing_path_creates_push_draw_and_pull_fade) {
    SyntheticSwing inOut; inOut.pathAngleDeg = 12;
    SyntheticSwing outIn; outIn.pathAngleDeg = -12;
    Launch a, b;
    ShotOutcome oa = hit(inOut, ClubId::Iron5, {}, sim(), &a);
    ShotOutcome ob = hit(outIn, ClubId::Iron5, {}, sim(), &b);
    CHECK(a.azimuthDeg > 1.2 && a.spinAxisDeg < -3.5);   // starts right, curves left
    CHECK(b.azimuthDeg < -1.2 && b.spinAxisDeg > 3.5);   // starts left, curves right
    CHECK(std::string(describeShape(a.azimuthDeg, a.spinAxisDeg)) == "Push Draw");
    CHECK(std::string(describeShape(b.azimuthDeg, b.spinAxisDeg)) == "Pull Fade");
    CHECK(oa.curveM < -2 && ob.curveM > 2);
    // Mirror-image inputs give mirror-image shots.
    CHECK_RANGE(std::fabs(oa.lateralAtRest + ob.lateralAtRest), 0, 0.5);
}

TEST(swing_fat_and_thin_have_clear_causes) {
    SyntheticSwing decel; decel.decelerate = 1.0;
    SyntheticSwing shortStop; shortStop.stopShortFrac = 0.3;
    SwingResult swFat, swThin; Launch LThin;
    ShotOutcome fat = hit(decel, ClubId::Iron7, {}, sim(), nullptr, &swFat);
    ShotOutcome thin = hit(shortStop, ClubId::Iron7, {}, sim(), &LThin, &swThin);
    ShotOutcome ok = hit({}, ClubId::Iron7);
    CHECK(swFat.contact == Contact::Fat);
    CHECK(swThin.contact == Contact::Thin);
    CHECK(fat.carry < ok.carry * 0.85);
    CHECK(thin.carry < ok.carry * 0.9);
    CHECK(LThin.launchDeg < 14.0);
    CHECK(swFat.feedback[0].find("Fat") == 0);
    CHECK(swThin.feedback[0].find("Thin") == 0);
}

TEST(swing_small_waggle_or_slow_return_cancels) {
    SwingSettings s = sim();
    SyntheticSwing tiny; tiny.power = 0.05;
    CHECK(!runSwing(synthesizeSwing(tiny, s), s).valid);
    SyntheticSwing backOff; backOff.downTimeScale = 6.0;
    CHECK(!runSwing(synthesizeSwing(backOff, s), s).valid);
}

TEST(swing_difficulty_scales_the_same_mistake) {
    SyntheticSwing bad; bad.downTimeScale = 0.72; bad.pathAngleDeg = 8;
    double prev = -1;
    for (SwingDifficulty d : {SwingDifficulty::Beginner, SwingDifficulty::Intermediate, SwingDifficulty::Advanced, SwingDifficulty::Simulation}) {
        ShotOutcome o = hit(bad, ClubId::Driver, {}, SwingSettings::forDifficulty(d));
        CHECK(std::fabs(o.lateralAtRest) > prev);
        prev = std::fabs(o.lateralAtRest);
    }
}

TEST(swing_classic_and_assisted_share_the_pipeline) {
    SwingResult c = classicMeterSwing(1.0, 0.0, sim(), kPro);
    SwingResult e = classicMeterSwing(1.0, -0.6, sim(), kPro);
    SwingResult a = assistedSwing(1.0, kPro);
    CHECK(c.valid && std::fabs(c.faceDeg) < 0.01);
    CHECK(e.faceDeg < -1.0);
    CHECK(a.valid && a.faceDeg == 0.0 && a.speedFactor < c.speedFactor);
}

TEST(swing_left_handed_mirrors) {
    SwingSettings lh = sim(); lh.leftHanded = true;
    SyntheticSwing p; p.pathAngleDeg = 10;
    SwingResult r = runSwing(synthesizeSwing(p, sim()), sim(), kPro);
    SwingResult l = runSwing(synthesizeSwing(p, lh), lh, kPro);
    CHECK_RANGE(std::fabs(r.pathDeg + l.pathDeg), 0, 1e-9);
}

TEST(shape_intent_draw_fade_work_with_perfect_swing) {
    ShotIntent draw; draw.curve = -1;
    ShotIntent fade; fade.curve = 1;
    Launch d, f;
    ShotOutcome od = hit({}, ClubId::Iron6, draw, sim(), &d);
    ShotOutcome of = hit({}, ClubId::Iron6, fade, sim(), &f);
    CHECK(d.azimuthDeg > 0 && od.curveM < -3);
    CHECK(f.azimuthDeg < 0 && of.curveM > 3);
    CHECK_RANGE(std::fabs(od.lateralAtRest), 0, 12); // a stock draw finishes near the target line
}

TEST(shot_types_change_real_ball_flight) {
    ShotOutcome normal = hit({}, ClubId::Iron5);
    auto with = [](ShotType t, ClubId c) { ShotIntent i; i.type = t; return hit({}, c, i); };
    ShotOutcome punch = with(ShotType::Punch, ClubId::Iron5);
    ShotOutcome stinger = with(ShotType::Stinger, ClubId::Iron5);
    ShotOutcome high = with(ShotType::High, ClubId::Iron5);
    CHECK(punch.apex < normal.apex * 0.5 && punch.carry < normal.carry);
    CHECK(stinger.apex < normal.apex * 0.55);
    CHECK(high.apex > normal.apex + 3 && high.landingAngleDeg > normal.landingAngleDeg);
    ShotOutcome chip = with(ShotType::Chip, ClubId::PitchingWedge);
    ShotOutcome flop = with(ShotType::Flop, ClubId::LobWedge);
    ShotOutcome pitch = with(ShotType::Pitch, ClubId::SandWedge);
    CHECK((chip.total - chip.carry) / chip.total > 0.25);       // chip: mostly roll
    CHECK(flop.landingAngleDeg > 50 && (flop.total - flop.carry) < 4.0); // flop: high and soft
    CHECK(pitch.carry > 30 && pitch.carry < 80);
    CHECK(!shotTypeAllowed(ShotType::Flop, ClubId::Iron5, Surface::Fairway));
    CHECK(!shotTypeAllowed(ShotType::Bunker, ClubId::SandWedge, Surface::Fairway));
    CHECK(shotTypeAllowed(ShotType::Bunker, ClubId::SandWedge, Surface::Sand));
}

TEST(lie_affects_the_shot) {
    SwingSettings s = sim();
    SwingResult sw = runSwing(synthesizeSwing({}, s), s, kPro);
    FlatTerrain range;
    auto carry = [&](Surface lie, ShotType t = ShotType::Normal) {
        ShotIntent i; i.type = t;
        return simulateShot(computeLaunch(sw, ClubId::Iron7, i, lie, kPro), {0, 0, 0}, 0, range, Conditions{}).carry;
    };
    CHECK(carry(Surface::Rough) < carry(Surface::Fairway));
    CHECK(carry(Surface::DeepRough) < carry(Surface::Rough));
    CHECK(carry(Surface::Sand) < carry(Surface::Fairway));
    CHECK(carry(Surface::DeepRough, ShotType::Recovery) > carry(Surface::DeepRough) * 0.9);
}

TEST(spin_control_changes_spin_and_what_the_ball_does_on_the_green) {
    SwingSettings s = sim();
    SwingResult sw = runSwing(synthesizeSwing({}, s), s, kPro);
    FlatTerrain green(Surface::Green);
    auto shot = [&](ClubId club, double spin, ShotType t) {
        ShotIntent in; in.spin = spin; in.type = t;
        Launch L = computeLaunch(sw, club, in, Surface::Fairway, kPro);
        return std::make_pair(L, simulateShot(L, {0, 0, 0}, 0, green, Conditions{}));
    };
    auto [lo, oLo] = shot(ClubId::SandWedge, -1, ShotType::Normal);
    auto [mid, oMid] = shot(ClubId::SandWedge, 0, ShotType::Normal);
    auto [hi, oHi] = shot(ClubId::SandWedge, 1, ShotType::Normal);
    CHECK(hi.spinRpm > mid.spinRpm * 1.35 && lo.spinRpm < mid.spinRpm * 0.65);
    double rollLo = oLo.total - oLo.carry, rollMid = oMid.total - oMid.carry, rollHi = oHi.total - oHi.carry;
    CHECK(rollLo > rollMid + 1.0);          // low spin releases
    CHECK(rollHi < rollMid - 0.5);          // max backspin checks up (or spins back)
    // Wedges have far more authority over spin than the driver.
    auto [dLo, dLoO] = shot(ClubId::Driver, -1, ShotType::Normal);
    auto [dHi, dHiO] = shot(ClubId::Driver, 1, ShotType::Normal);
    double wedgeRange = hi.spinRpm / lo.spinRpm, driverRange = dHi.spinRpm / dLo.spinRpm;
    CHECK(wedgeRange > driverRange * 1.5);
    // Spin needs clean contact: a thin strike gets less of it.
    SyntheticSwing thinP; thinP.stopShortFrac = 0.3;
    SwingResult thin = runSwing(synthesizeSwing(thinP, s), s, kPro);
    ShotIntent in; in.spin = 1;
    ShotIntent none;
    double thinGain = computeLaunch(thin, ClubId::SandWedge, in, Surface::Fairway, kPro).spinRpm / computeLaunch(thin, ClubId::SandWedge, none, Surface::Fairway, kPro).spinRpm;
    CHECK(thinGain < hi.spinRpm / mid.spinRpm);
}

TEST(spin_back_is_possible_with_a_full_wedge_onto_a_soft_green) {
    SwingSettings s = sim();
    SwingResult sw = runSwing(synthesizeSwing({}, s), s, kPro);
    FlatTerrain green(Surface::Green);
    Conditions soft; soft.wetness = 0.4;
    ShotIntent in; in.spin = 1;
    ShotOutcome o = simulateShot(computeLaunch(sw, ClubId::LobWedge, in, Surface::Fairway, kPro), {0, 0, 0}, 0, green, soft);
    std::printf("    lob wedge max spin: carry %.1f m, total %.1f m (%+.2f m after landing)\n", o.carry, o.total, o.total - o.carry);
    CHECK(o.total - o.carry < 0.5);
}

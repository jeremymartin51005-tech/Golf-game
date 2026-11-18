#include "golf/Round.h"

namespace golf {

const char* scoreName(int strokes, int par) {
    if (strokes == 1) return "Hole in One";
    switch (strokes - par) {
        case -4: return "Condor";
        case -3: return "Albatross";
        case -2: return "Eagle";
        case -1: return "Birdie";
        case 0: return "Par";
        case 1: return "Bogey";
        case 2: return "Double Bogey";
        case 3: return "Triple Bogey";
        default: return strokes < par ? "Under Par" : "Over Par";
    }
}

HolePlay::HolePlay(const HoleLayout& hole, const Conditions& cond, int teeIndex) : hole_(hole), cond_(cond) {
    ball_ = hole.teePosition(teeIndex);
    lie_ = Surface::Tee;
    res_.number = hole.number;
    res_.par = hole.par;
    res_.fairwayApplies = hole.par >= 4;
}

double HolePlay::headingTo(Vec2 target) const {
    Vec2 d = target - ball_.xy();
    return rad2deg(std::atan2(d.x, d.y));
}

ShotOutcome HolePlay::playShot(const Launch& launch, double aimDeg, bool leftHanded) {
    Launch L = launch;
    if (lie_ != Surface::Tee) applyLieSlope(L, lieSlope(aimDeg, leftHanded), leftHanded);
    ShotOutcome o = simulateShot(L, ball_, aimDeg, hole_, cond_, Cup{true, hole_.pin()});
    settle(o, false);
    return o;
}

ShotOutcome HolePlay::playPutt(double speed, double aimDeg) {
    ShotOutcome o = simulatePutt(speed, ball_, aimDeg, hole_, cond_, Cup{true, hole_.pin()});
    settle(o, true);
    return o;
}

void HolePlay::settle(const ShotOutcome& o, bool wasPutt) {
    bool teeShot = res_.strokes == 0;
    ++res_.strokes;
    if (wasPutt && lie_ == Surface::Green) ++res_.putts;
    if (lie_ == Surface::Sand) ++res_.sandShots;
    if (teeShot && hole_.par >= 4) res_.longestDrive = o.total;

    switch (o.reason) {
        case RestReason::Holed:
            res_.holed = true;
            ball_ = o.rest;
            lie_ = Surface::Green;
            break;
        case RestReason::Water:
            // Penalty area: one stroke, drop where the ball last crossed dry ground.
            ++res_.penalties; ++res_.strokes;
            ball_ = o.hazardEntry;
            ball_.z = hole_.height(ball_.xy());
            lie_ = hole_.surface(ball_.xy());
            break;
        case RestReason::OutOfBounds:
            // Stroke and distance: replay from the same spot.
            ++res_.penalties; ++res_.strokes;
            break;
        default:
            ball_ = o.rest;
            lie_ = hole_.surface(ball_.xy());
            break;
    }
    if (teeShot && hole_.par >= 4) res_.fairwayHit = o.reason == RestReason::Stopped && (lie_ == Surface::Fairway || lie_ == Surface::Green);
    // Green in regulation: on the putting surface (or in the hole) with at least two strokes left for par.
    if (!res_.gir && (lie_ == Surface::Green || res_.holed) && res_.strokes <= hole_.par - 2) res_.gir = true;
}

int Scorecard::strokes() const { int s = 0; for (auto& h : holes) s += h.strokes; return s; }
int Scorecard::par() const { int s = 0; for (auto& h : holes) s += h.par; return s; }
int Scorecard::putts() const { int s = 0; for (auto& h : holes) s += h.putts; return s; }
int Scorecard::penalties() const { int s = 0; for (auto& h : holes) s += h.penalties; return s; }
int Scorecard::fairwaysHit() const { int s = 0; for (auto& h : holes) s += h.fairwayApplies && h.fairwayHit; return s; }
int Scorecard::fairwaysPossible() const { int s = 0; for (auto& h : holes) s += h.fairwayApplies; return s; }
int Scorecard::greensInRegulation() const { int s = 0; for (auto& h : holes) s += h.gir; return s; }
int Scorecard::countScore(int rel) const { int s = 0; for (auto& h : holes) s += h.holed && h.strokes - h.par == rel; return s; }

// ---- AI golfer ------------------------------------------------------------------------------------

namespace {

struct Plan { ClubId club; ShotIntent intent; double power; double aim; };

const SwingSettings& botSettings() {
    static const SwingSettings s = SwingSettings::forDifficulty(SwingDifficulty::Simulation);
    return s;
}

ShotOutcome trial(const HolePlay& play, const Plan& p, const PlayerAttributes& attr) {
    SwingResult sw = classicMeterSwing(p.power, 0.0, botSettings(), attr);
    Launch L = computeLaunch(sw, p.club, p.intent, play.lie(), attr, play.conditions().wetness);
    if (play.lie() != Surface::Tee) applyLieSlope(L, play.lieSlope(p.aim));
    FlightParams fp; fp.dt = 1.0 / 120.0; fp.recordPath = false;
    return simulateShot(L, play.ball(), p.aim, play.hole(), play.conditions(), {}, fp);
}

double along(const HolePlay& play, const ShotOutcome& o, Vec2 dir) { return (o.rest.xy() - play.ball().xy()).dot(dir); }

Plan planShot(const HolePlay& play, const PlayerAttributes& attr) {
    const HoleLayout& h = play.hole();
    Vec2 ball = play.ball().xy();
    Surface lie = play.lie();
    double toPin = play.distanceToPin();

    // Clubs, shortest first, restricted by lie.
    std::vector<ClubId> clubs = {ClubId::LobWedge, ClubId::SandWedge, ClubId::GapWedge, ClubId::PitchingWedge, ClubId::Iron9,
                                 ClubId::Iron8, ClubId::Iron7, ClubId::Iron6, ClubId::Iron5, ClubId::Hybrid4};
    if (lie == Surface::Tee || lie == Surface::Fairway || lie == Surface::FirstCut) { clubs.push_back(ClubId::Wood5); clubs.push_back(ClubId::Wood3); }
    if (lie == Surface::Tee) clubs.push_back(ClubId::Driver);
    if (lie == Surface::Sand || lie == Surface::DeepRough) clubs.resize(7);

    auto maxReach = [&](Vec2 target) {
        Plan p{clubs.back(), {}, 1.0, play.headingTo(target)};
        return along(play, trial(play, p, attr), (target - ball).normalized());
    };

    // Choose a target: the pin if it is in range, otherwise the furthest landing zone we can reach,
    // otherwise as far down the centreline as the longest club goes.
    Vec2 target = h.pin();
    bool atPin = true;
    if (toPin > maxReach(h.pin()) * 0.99) {
        atPin = false;
        double sb = h.project(ball);
        double reach = maxReach(h.pointAt(std::min(sb + 150.0, h.lengthM)));
        double bestS = -1;
        for (const LandingZone& z : h.landingZones) {
            double zs = h.project(z.pos);
            if (zs > sb + 40.0 && (z.pos - ball).length() <= reach * 0.99 && zs > bestS) { bestS = zs; target = z.pos; }
        }
        if (bestS < 0) {
            double s = std::min(sb + 0.9 * reach, h.lengthM - 70.0);
            for (int i = 0; i < 8; ++i) {
                Surface sf = h.surface(h.pointAt(s));
                if (sf == Surface::Fairway || sf == Surface::FirstCut || sf == Surface::Rough) break;
                s -= 12.0;
            }
            target = h.pointAt(std::max(s, sb + 20.0));
        }
    }
    double d = (target - ball).length();
    Vec2 dir = (target - ball).normalized();

    // Candidate shots, most delicate first; take the first that can cover the distance.
    std::vector<Plan> options;
    double aim0 = play.headingTo(target);
    auto add = [&](ClubId c, ShotType t) { ShotIntent i; i.type = t; if (shotTypeAllowed(t, c, lie)) options.push_back({c, i, 1.0, aim0}); };
    if (atPin && lie == Surface::Sand && d < 45) add(ClubId::SandWedge, ShotType::Bunker);
    if (atPin && lie != Surface::Sand && d < 35) add(ClubId::PitchingWedge, ShotType::Chip);
    if (d < 95) { add(ClubId::LobWedge, ShotType::Pitch); add(ClubId::SandWedge, ShotType::Pitch); add(ClubId::GapWedge, ShotType::Pitch); }
    for (ClubId c : clubs) add(c, ShotType::Normal);

    Plan best = options.back();
    for (const Plan& p : options) {
        if (along(play, trial(play, p, attr), dir) >= d) { best = p; break; }
    }
    // Dial in power, then correct the aim for wind, slope and bounce.
    for (int pass = 0; pass < 2; ++pass) {
        double lo = 0.12, hi = 1.0;
        for (int i = 0; i < 9; ++i) {
            best.power = 0.5 * (lo + hi);
            if (along(play, trial(play, best, attr), dir) < d) lo = best.power; else hi = best.power;
        }
        best.power = 0.5 * (lo + hi);
        ShotOutcome o = trial(play, best, attr);
        double miss = (o.rest.xy() - ball).dot(dir.perp());
        best.aim -= rad2deg(std::atan2(miss, std::max(5.0, d)));
    }
    return best;
}

} // namespace

HoleResult BotGolfer::playHole(HolePlay& play, int maxStrokes) {
    // Even skill 1.0 is a very good human, not a robot: nobody repeats a swing exactly.
    const double err = 0.4 + 0.6 * clampd(1.0 - skill, 0.0, 1.0);
    while (!play.holed() && play.result().strokes < maxStrokes) {
        if (play.lie() == Surface::Green) {
            Cup cup{true, play.hole().pin()};
            PuttRead r = readPutt(play.ball(), cup, play.hole(), play.conditions());
            double aim = r.aimDeg + rng.signedUnit() * 5.0 * err;
            double speed = r.speed * (1.0 + rng.signedUnit() * 0.15 * err);
            play.playPutt(speed, aim);
        } else {
            Plan p = planShot(play, attr);
            double power = clampd(p.power * (1.0 + rng.signedUnit() * 0.06 * err), 0.1, 1.0);
            double accuracy = rng.signedUnit() * err;
            double aim = p.aim + rng.signedUnit() * 2.0 * err;
            SwingResult sw = classicMeterSwing(power, accuracy, botSettings(), attr);
            Launch L = computeLaunch(sw, p.club, p.intent, play.lie(), attr, play.conditions().wetness);
            play.playShot(L, aim);
        }
    }
    return play.result();
}

Scorecard playRound(const Course& course, BotGolfer& bot) {
    Scorecard card;
    for (size_t i = 0; i < course.holes.size(); ++i) {
        HolePlay play(course.holes[i], course.conditionsForHole(int(i)), course.config.teeIndex);
        card.holes.push_back(bot.playHole(play));
    }
    return card;
}

} // namespace golf

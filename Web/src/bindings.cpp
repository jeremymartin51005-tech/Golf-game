// WebAssembly interface to GolfCore for the browser client. Numbers cross the boundary through one
// shared double buffer; strings through static C strings. No game logic lives here.
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "golf/CourseParams.h"
#include "golf/Round.h"

using namespace golf;

#define EXPORT(name) __attribute__((export_name(#name))) name

namespace {

double gBuf[2048];
char gStr[512];
std::string gText;

Course gCourse;
std::unique_ptr<HolePlay> gPlay;
int gHole = -1;
bool gRange = false;
std::vector<uint8_t> gSurfaceMap;
std::vector<double> gPath;

PlayerAttributes gAttr;           // default created golfer
SwingSettings gSettings;
std::unique_ptr<MouseSwing> gSwing;
SwingResult gLastSwing;
Launch gLastLaunch;
ShotOutcome gLastOutcome;
bool gLastWasPutt = false;

// A flat practice range that goes through exactly the same pipeline as a real hole.
struct RangeHole : HoleLayout {
    RangeHole() {
        number = 0; par = 4; lengthM = 330; tmpl = HoleTemplate::Par4Straight;
        centerline = {{0, 0}, {0, 230}, {0, 330}};
        fairwayStart = 12; fairwayEnd = 312; fairwayHalfWidth = 42; corridorHalfWidth = 95;
        green.shape = {{0, 330}, 14, 14, 0};
        green.pins = {{0, 330}};
        tees = {{"Championship", {0, 0}, 330}, {"Back", {0, 0}, 330}, {"Middle", {0, 0}, 330}, {"Forward", {0, 0}, 330}};
        landingZones = {{{0, 230}, 18}};
        gridMin = {-110, -40}; cell = 4; nx = 56; ny = 106;
        heights.assign(size_t(nx) * ny, 0.0f);
    }
};
RangeHole gRangeHole;

const HoleLayout& hole(int i) { return gRange ? gRangeHole : gCourse.holes[size_t(i)]; }
Conditions condFor(int i) {
    if (gRange) { Conditions c; return c; }
    return gCourse.conditionsForHole(i);
}

double gSpin = 0;   // strike point from the shot menu; applies to every full shot until changed

ShotIntent makeIntent(int type, int style, double curve, double traj) {
    ShotIntent in;
    in.spin = gSpin;
    in.type = ShotType(int(clampd(type, 0, int(ShotType::Count) - 1)));
    in.style = SwingStyle(int(clampd(style, 0, 3)));
    in.curve = curve;
    in.trajectory = traj;
    return in;
}

void storeOutcome(const ShotOutcome& o) {
    gLastOutcome = o;
    gPath.clear();
    gPath.reserve(o.path.size() * 5);
    for (const TrajectoryPoint& p : o.path) {
        gPath.push_back(p.t); gPath.push_back(p.pos.x); gPath.push_back(p.pos.y); gPath.push_back(p.pos.z);
        gPath.push_back(p.airborne ? 1.0 : 0.0);
    }
}

int strikeWith(const SwingResult& sw, int club, int type, int style, double curve, double traj, double aimDeg) {
    if (!gPlay || !sw.valid || gPlay->holed()) return 0;
    gLastSwing = sw;
    ClubId id = ClubId(int(clampd(club, 0, int(ClubId::Count) - 1)));
    if (id == ClubId::Putter) {
        PuttLaunch p = computePutt(sw, 6.0, gAttr);
        gLastLaunch = Launch{};
        gLastLaunch.ballSpeed = p.speed;
        gLastLaunch.azimuthDeg = p.azimuthDeg;
        gLastWasPutt = true;
        storeOutcome(gPlay->playPutt(p.speed, aimDeg + p.azimuthDeg));
    } else {
        ShotIntent in = makeIntent(type, style, curve, traj);
        if (!shotTypeAllowed(in.type, id, gPlay->lie())) in.type = ShotType::Normal;
        gLastLaunch = computeLaunch(sw, id, in, gPlay->lie(), gAttr, gPlay->conditions().wetness, gSettings.leftHanded);
        gLastWasPutt = false;
        storeOutcome(gPlay->playShot(gLastLaunch, aimDeg));
    }
    return 1;
}

int finishGenerate() {
    gPlay.reset();
    gHole = -1;
    gRange = false;
    return int(gCourse.holes.size());
}

} // namespace

extern "C" {

double* EXPORT(ng_buf)() { return gBuf; }
void EXPORT(ng_set_spin)(double spin) { gSpin = clampd(spin, -1.0, 1.0); }
char* EXPORT(ng_strbuf)() { return gStr; }

// Created-golfer attributes, each 0..1 (0.5 = average). They scale mistakes and speed; they never hit the shot.
void EXPORT(ng_set_attributes)(double power, double accuracy, double shortGame, double putting, double recovery, double consistency) {
    gAttr.power = clampd(power, 0, 1); gAttr.accuracy = clampd(accuracy, 0, 1); gAttr.shortGame = clampd(shortGame, 0, 1);
    gAttr.putting = clampd(putting, 0, 1); gAttr.recovery = clampd(recovery, 0, 1); gAttr.consistency = clampd(consistency, 0, 1);
}

int EXPORT(ng_generate)(double seedHi, double seedLo, int env, int diff, int tee, int haz, int pin) {
    CourseConfig cfg;
    cfg.seed = (uint64_t(uint32_t(seedHi)) << 32) | uint64_t(uint32_t(seedLo));
    cfg.environment = Environment(int(clampd(env, 0, int(Environment::Count) - 1)));
    cfg.difficulty = Difficulty(int(clampd(diff, 0, 3)));
    cfg.teeIndex = int(clampd(tee, 0, 3));
    cfg.hazardDensity = int(clampd(haz, 0, 3));
    cfg.pinDifficulty = int(clampd(pin, 0, 3));
    gCourse = generateCourse(cfg);
    return finishGenerate();
}

int EXPORT(ng_generate_daily)(double unixSeconds) {
    gCourse = generateCourse(dailyConfig(dateFromUnix(int64_t(unixSeconds))));
    gText = dailyId(dateFromUnix(int64_t(unixSeconds)));
    return finishGenerate();
}

int EXPORT(ng_generate_code)() {
    gStr[sizeof(gStr) - 1] = 0;
    CourseConfig cfg;
    if (!decodeShareCode(gStr, &cfg)) return 0;
    gCourse = generateCourse(cfg);
    return finishGenerate();
}

double EXPORT(ng_seconds_to_next_daily)(double unixSeconds) { return double(secondsUntilNextDaily(int64_t(unixSeconds))); }
const char* EXPORT(ng_daily_id)(double unixSeconds) { gText = dailyId(dateFromUnix(int64_t(unixSeconds))); return gText.c_str(); }
const char* EXPORT(ng_fingerprint)() {
    char tmp[32];
    unsigned long long f = courseFingerprint(gCourse);
    static const char* hex = "0123456789abcdef";
    for (int k = 0; k < 16; ++k) tmp[k] = hex[(f >> (60 - 4 * k)) & 15];
    tmp[16] = 0;
    gText = tmp;
    return gText.c_str();
}
const char* EXPORT(ng_course_name)() { return gCourse.name.c_str(); }
const char* EXPORT(ng_share_code)() { gText = encodeShareCode(gCourse.config); return gText.c_str(); }
const char* EXPORT(ng_weather_name)() { return weatherName(gCourse.weather); }
const char* EXPORT(ng_environment_name)(int e) { return environmentName(Environment(int(clampd(e, 0, int(Environment::Count) - 1)))); }

void EXPORT(ng_course_info)() {
    double* b = gBuf;
    b[0] = double(gCourse.holes.size()); b[1] = gCourse.totalPar; b[2] = gCourse.conditions.wind.speed;
    b[3] = gCourse.conditions.wind.headingDeg; b[4] = gCourse.conditions.temperatureC; b[5] = gCourse.conditions.stimpFeet;
    b[6] = int(gCourse.weather); b[7] = gCourse.conditions.wetness; b[8] = int(gCourse.config.environment);
    b[9] = int(gCourse.config.difficulty); b[10] = gCourse.config.teeIndex;
    b[11] = gCourse.holes.empty() ? 0 : gCourse.lengthM(gCourse.config.teeIndex);
}

void EXPORT(ng_use_range)(int on) { gRange = on != 0; gPlay.reset(); gHole = -1; }

void EXPORT(ng_hole_info)(int i) {
    const HoleLayout& h = hole(i);
    int tee = gRange ? 0 : gCourse.config.teeIndex;
    Vec3 t = h.teePosition(tee);
    Conditions c = condFor(i);
    double* b = gBuf;
    b[0] = h.par; b[1] = h.tees[size_t(tee)].lengthM; b[2] = h.nx; b[3] = h.ny; b[4] = h.cell; b[5] = h.gridMin.x; b[6] = h.gridMin.y;
    b[7] = h.pin().x; b[8] = h.pin().y; b[9] = h.height(h.pin());
    b[10] = t.x; b[11] = t.y; b[12] = t.z;
    b[13] = h.green.shape.c.x; b[14] = h.green.shape.c.y; b[15] = h.green.shape.rx; b[16] = h.green.shape.ry; b[17] = h.green.shape.rotDeg;
    b[18] = double(h.trees.size()); b[19] = int(h.tmpl); b[20] = h.elevationChange;
    b[21] = c.wind.headingDeg; b[22] = c.wind.speed; b[23] = c.stimpFeet;
    b[24] = double(h.water.size());
    b[25] = double(h.centerline.size());
    for (size_t k = 0; k < h.centerline.size() && k < 8; ++k) { b[26 + 2 * k] = h.centerline[k].x; b[27 + 2 * k] = h.centerline[k].y; }
}
// Exact physics ground height (includes the fine green patch), for placing anything on the ground.
double EXPORT(ng_height)(int i, double x, double y) { return hole(i).height({x, y}); }

// Fine green patch: gBuf = minX, minY, cell, nx, ny.
void EXPORT(ng_hole_green_patch)(int i) {
    const HoleLayout& h = hole(i);
    gBuf[0] = h.fineMin.x; gBuf[1] = h.fineMin.y; gBuf[2] = h.fineCell; gBuf[3] = h.fnx; gBuf[4] = h.fny;
}

// Water: per pond cx, cy, rx, ry, rotDeg, surface height.
int EXPORT(ng_hole_water)(int i) {
    const HoleLayout& h = hole(i);
    size_t n = std::min<size_t>(h.water.size(), 32);
    for (size_t k = 0; k < n; ++k) {
        const Ellipse& w = h.water[k];
        gBuf[k * 6] = w.c.x; gBuf[k * 6 + 1] = w.c.y; gBuf[k * 6 + 2] = w.rx; gBuf[k * 6 + 3] = w.ry; gBuf[k * 6 + 4] = w.rotDeg;
        gBuf[k * 6 + 5] = h.height(w.c) + 0.12;
    }
    return int(n);
}

float* EXPORT(ng_hole_heights)(int i) { return const_cast<float*>(hole(i).heights.data()); }
const char* EXPORT(ng_hole_template)(int i) { return gRange ? "Practice range" : templateName(hole(i).tmpl); }

int EXPORT(ng_hole_trees)(int i) {
    const HoleLayout& h = hole(i);
    size_t n = std::min<size_t>(h.trees.size(), 400);
    for (size_t k = 0; k < n; ++k) {
        const Tree& t = h.trees[k];
        gBuf[k * 5] = t.pos.x; gBuf[k * 5 + 1] = t.pos.y; gBuf[k * 5 + 2] = h.height(t.pos); gBuf[k * 5 + 3] = t.radius; gBuf[k * 5 + 4] = t.height;
    }
    return int(n);
}

// Rasterises the surface type of every `res` metres of the hole; the client paints its terrain texture from this.
uint8_t* EXPORT(ng_surface_map)(int i, double res) {
    const HoleLayout& h = hole(i);
    int w = int(std::ceil((h.nx - 1) * h.cell / res)), ht = int(std::ceil((h.ny - 1) * h.cell / res));
    gSurfaceMap.resize(size_t(w) * ht);
    for (int y = 0; y < ht; ++y)
        for (int x = 0; x < w; ++x)
            gSurfaceMap[size_t(y) * w + x] = uint8_t(h.surface({h.gridMin.x + (x + 0.5) * res, h.gridMin.y + (y + 0.5) * res}));
    gBuf[0] = w; gBuf[1] = ht;
    return gSurfaceMap.data();
}

void EXPORT(ng_start_hole)(int i) {
    gHole = i;
    gPlay = std::make_unique<HolePlay>(hole(i), condFor(i), gRange ? 0 : gCourse.config.teeIndex);
}

void EXPORT(ng_state)() {
    if (!gPlay) return;
    const HoleResult& r = gPlay->result();
    Vec3 p = gPlay->ball();
    double* b = gBuf;
    b[0] = p.x; b[1] = p.y; b[2] = p.z; b[3] = int(gPlay->lie()); b[4] = r.strokes; b[5] = r.holed ? 1 : 0;
    b[6] = gPlay->distanceToPin(); b[7] = r.putts; b[8] = r.penalties; b[9] = r.par;
    b[10] = r.fairwayApplies ? 1 : 0; b[11] = r.fairwayHit ? 1 : 0; b[12] = r.gir ? 1 : 0; b[13] = r.longestDrive;
    b[14] = gPlay->headingTo(gPlay->hole().pin());
    b[15] = gPlay->hole().height(gPlay->hole().pin()) - p.z;
}
const char* EXPORT(ng_lie_name)() { return gPlay ? surfaceName(gPlay->lie()) : ""; }

// ---- swing ----------------------------------------------------------------------------------------

void EXPORT(ng_swing_begin)(int difficulty, double sensitivity, double backswingCounts, double downswingTime, int leftHanded) {
    gSettings = SwingSettings::forDifficulty(SwingDifficulty(int(clampd(difficulty, 0, 3))));
    gSettings.sensitivity = sensitivity;
    gSettings.backswingCounts = backswingCounts;
    gSettings.idealDownswingTime = downswingTime;
    gSettings.leftHanded = leftHanded != 0;
    gSwing = std::make_unique<MouseSwing>(gSettings, gAttr);
}
int EXPORT(ng_swing_sample)(double t, double dx, double dy) {
    if (!gSwing) return 0;
    gSwing->addSample({t, dx, dy});
    return int(gSwing->phase());
}
int EXPORT(ng_swing_tick)(double t) {
    if (!gSwing) return 0;
    gSwing->tick(t);
    return int(gSwing->phase());
}
double EXPORT(ng_swing_live_power)() { return gSwing ? gSwing->livePower() : 0.0; }
double EXPORT(ng_swing_live_lateral)() { return gSwing ? gSwing->liveLateral() / gSettings.backswingCounts : 0.0; }

int EXPORT(ng_strike)(int club, int type, int style, double curve, double traj, double aimDeg) {
    if (!gSwing || gSwing->phase() != SwingPhase::Done) return 0;
    SwingResult sw = gSwing->result();
    gSwing.reset();
    return strikeWith(sw, club, type, style, curve, traj, aimDeg);
}
int EXPORT(ng_strike_classic)(double power, double accuracy, int club, int type, int style, double curve, double traj, double aimDeg) {
    return strikeWith(classicMeterSwing(power, accuracy, gSettings, gAttr), club, type, style, curve, traj, aimDeg);
}
int EXPORT(ng_strike_assisted)(double power, int club, int type, int style, double curve, double traj, double aimDeg) {
    return strikeWith(assistedSwing(power, gAttr), club, type, style, curve, traj, aimDeg);
}

void EXPORT(ng_outcome)() {
    const ShotOutcome& o = gLastOutcome;
    const Launch& L = gLastLaunch;
    const SwingResult& s = gLastSwing;
    double* b = gBuf;
    b[0] = o.carry; b[1] = o.total; b[2] = o.apex; b[3] = o.landingAngleDeg; b[4] = o.lateralAtRest; b[5] = o.curveM;
    b[6] = int(o.reason); b[7] = int(o.restSurface); b[8] = o.bounces; b[9] = L.ballSpeed; b[10] = L.launchDeg; b[11] = L.azimuthDeg;
    b[12] = L.spinRpm; b[13] = L.spinAxisDeg; b[14] = L.headSpeed; b[15] = s.power; b[16] = s.pathDeg; b[17] = s.faceDeg;
    b[18] = s.timingError; b[19] = s.tempoRatio; b[20] = int(s.contact); b[21] = s.contactQuality; b[22] = double(gPath.size() / 5);
    b[23] = o.landing.x; b[24] = o.landing.y; b[25] = o.landing.z; b[26] = gLastWasPutt ? 1 : 0; b[27] = o.hitObstacle ? 1 : 0;
    b[28] = o.totalTime; b[29] = s.overswing;
}
double* EXPORT(ng_path)() { return gPath.data(); }
const char* EXPORT(ng_feedback)() {
    gText.clear();
    for (const std::string& f : gLastSwing.feedback) { if (!gText.empty()) gText += '\n'; gText += f; }
    return gText.c_str();
}
const char* EXPORT(ng_shape_name)() {
    return gLastWasPutt ? "Putt" : describeShape(gLastLaunch.azimuthDeg, gLastLaunch.spinAxisDeg, gSettings.leftHanded);
}
const char* EXPORT(ng_score_name)(int strokes, int par) { return scoreName(strokes, par); }

// ---- aids -----------------------------------------------------------------------------------------

// Where a perfectly executed shot with these settings would go (does not change the game state).
void EXPORT(ng_preview)(int club, int type, int style, double curve, double traj, double aimDeg, double power) {
    double* b = gBuf;
    for (int k = 0; k < 10; ++k) b[k] = 0;
    if (!gPlay) return;
    ClubId id = ClubId(int(clampd(club, 0, int(ClubId::Count) - 1)));
    if (id == ClubId::Putter) return;
    ShotIntent in = makeIntent(type, style, curve, traj);
    if (!shotTypeAllowed(in.type, id, gPlay->lie())) in.type = ShotType::Normal;
    SwingSettings s = SwingSettings::forDifficulty(SwingDifficulty::Simulation);
    Launch L = computeLaunch(classicMeterSwing(power, 0.0, s, gAttr), id, in, gPlay->lie(), gAttr, gPlay->conditions().wetness, gSettings.leftHanded);
    FlightParams fp; fp.dt = 1.0 / 120.0; fp.recordPath = false;
    ShotOutcome o = simulateShot(L, gPlay->ball(), aimDeg, gPlay->hole(), gPlay->conditions(), {}, fp);
    b[0] = o.landing.x; b[1] = o.landing.y; b[2] = o.landing.z; b[3] = o.rest.x; b[4] = o.rest.y; b[5] = o.rest.z;
    b[6] = o.carry; b[7] = o.total; b[8] = o.apex; b[9] = int(o.reason);
}

int EXPORT(ng_shot_allowed)(int type, int club) {
    if (!gPlay) return 0;
    return shotTypeAllowed(ShotType(int(clampd(type, 0, int(ShotType::Count) - 1))), ClubId(int(clampd(club, 0, int(ClubId::Count) - 1))), gPlay->lie()) ? 1 : 0;
}

// Caddie: the club (and shot type in gBuf[0]) that covers the distance to the pin from this lie.
int EXPORT(ng_recommend)() {
    if (!gPlay) return 0;
    Surface lie = gPlay->lie();
    double d = gPlay->distanceToPin();
    gBuf[0] = int(ShotType::Normal);
    if (lie == Surface::Green) return int(ClubId::Putter);
    if (lie == Surface::Sand && d < 40) { gBuf[0] = int(ShotType::Bunker); return int(ClubId::SandWedge); }
    if (d < 28 && lie != Surface::Sand) { gBuf[0] = int(ShotType::Chip); return int(ClubId::PitchingWedge); }
    std::vector<ClubId> bag = Bag::standard().clubs;
    SwingSettings s = SwingSettings::forDifficulty(SwingDifficulty::Simulation);
    FlightParams fp; fp.dt = 1.0 / 120.0; fp.recordPath = false;
    double aim = gPlay->headingTo(gPlay->hole().pin());
    Vec2 dir = (gPlay->hole().pin() - gPlay->ball().xy()).normalized();
    ClubId best = lie == Surface::Tee ? ClubId::Driver : ClubId::Wood3;
    for (auto it = bag.rbegin(); it != bag.rend(); ++it) { // shortest club first
        if (*it == ClubId::Putter) continue;
        if (*it == ClubId::Driver && lie != Surface::Tee) continue;
        Launch L = computeLaunch(classicMeterSwing(1.0, 0.0, s, gAttr), *it, {}, lie, gAttr, gPlay->conditions().wetness);
        ShotOutcome o = simulateShot(L, gPlay->ball(), aim, gPlay->hole(), gPlay->conditions(), {}, fp);
        if ((o.rest.xy() - gPlay->ball().xy()).dot(dir) >= d) { best = *it; break; }
    }
    if (d < 75 && clubSpec(best).cls == ClubClass::Wedge) gBuf[0] = int(ShotType::Pitch);
    return int(best);
}

void EXPORT(ng_putt_read)() {
    double* b = gBuf;
    b[0] = 0;
    if (!gPlay) return;
    PuttRead r = readPutt(gPlay->ball(), Cup{true, gPlay->hole().pin()}, gPlay->hole(), gPlay->conditions());
    b[0] = r.found ? 1 : 0; b[1] = r.aimDeg; b[2] = r.speed; b[3] = r.breakM; b[4] = r.elevationM;
    // The line a correctly read putt follows, for the on-green aid.
    FlightParams fp; fp.recordPath = true;
    ShotOutcome o = simulatePutt(r.speed, gPlay->ball(), r.aimDeg, gPlay->hole(), gPlay->conditions(), {}, fp);
    size_t n = std::min<size_t>(o.path.size(), 300);
    b[5] = double(n);
    for (size_t k = 0; k < n; ++k) { b[6 + k * 3] = o.path[k].pos.x; b[7 + k * 3] = o.path[k].pos.y; b[8 + k * 3] = o.path[k].pos.z; }
}

const char* EXPORT(ng_club_name)(int id) { return clubSpec(ClubId(int(clampd(id, 0, int(ClubId::Count) - 1)))).name; }
const char* EXPORT(ng_club_short)(int id) { return clubSpec(ClubId(int(clampd(id, 0, int(ClubId::Count) - 1)))).shortName; }
const char* EXPORT(ng_shot_type_name)(int t) { return shotTypeSpec(ShotType(int(clampd(t, 0, int(ShotType::Count) - 1)))).name; }
int EXPORT(ng_bag)() {
    Bag b = Bag::standard();
    for (size_t k = 0; k < b.clubs.size(); ++k) gBuf[k] = int(b.clubs[k]);
    return int(b.clubs.size());
}

} // extern "C"

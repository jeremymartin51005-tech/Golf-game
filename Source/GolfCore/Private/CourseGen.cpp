#include "golf/CourseParams.h"
#include <cstdio>

namespace golf {

const char* difficultyName(Difficulty d) {
    static const char* n[] = {"Beginner", "Intermediate", "Advanced", "Championship"};
    return n[int(d)];
}
const char* environmentName(Environment e) {
    static const char* n[] = {"Parkland", "Highlands", "Coastal", "Alpine", "Desert", "Links"};
    return n[int(e)];
}
const char* weatherName(Weather w) {
    static const char* n[] = {"Sunny", "Cloudy", "Overcast", "Light Rain", "Windy", "Foggy"};
    return n[int(w)];
}
const char* templateName(HoleTemplate t) {
    static const char* n[] = {"Open par 3", "Water-carry par 3", "Guarded par 3", "Straight par 4", "Dogleg-left par 4",
                              "Dogleg-right par 4", "Short risk-reward par 4", "Straight par 5", "Double-dogleg par 5",
                              "Cape par 5"};
    return n[int(t)];
}

const DifficultyParams& difficultyParams(Difficulty d) {
    static const DifficultyParams p[] = {
        // halfW  len   haz   green tilt  pin   carry drive stimp
        // halfW  len   haz   green tilt  pin   carry drive stimp contour
        {24.0, 0.86, 0.50, 1.18, 1.5, 2.0,  80, 200,  9.0, 0.55},
        {20.0, 0.93, 0.80, 1.08, 2.0, 2.5, 120, 220, 10.0, 0.80},
        {17.0, 1.00, 1.00, 1.00, 2.5, 3.0, 160, 235, 11.0, 1.00},
        {14.5, 1.04, 1.25, 0.93, 3.0, 3.5, 185, 245, 12.0, 1.20},
    };
    return p[int(d)];
}
const EnvironmentParams& environmentParams(Environment e) {
    static const EnvironmentParams p[] = {
        // hill detail elev trees water bunk  windMin windMax tee  width alt  tMin tMax
        // hill detail elev trees water bunk  windMin windMax tee  width alt  tMin tMax  roll wave
        {3.0, 0.5,  6.5, 1.00, 0.35, 1.0, 0.0,  5.0, 0.5, 1.00,   80, 14, 28, 0.95, 48}, // Parkland
        {5.5, 0.8, 11.0, 0.60, 0.25, 1.2, 2.0,  7.0, 1.0, 1.00,  350, 10, 22, 1.10, 45}, // Highlands
        {4.0, 1.2,  8.0, 0.20, 0.45, 1.0, 4.0, 10.0, 1.8, 1.05,   10, 12, 24, 1.05, 40}, // Coastal
        {7.0, 1.0, 18.0, 1.20, 0.20, 0.9, 1.0,  6.0, 2.2, 0.90, 1500,  6, 20, 0.95, 45}, // Alpine
        {3.0, 0.7,  7.0, 0.15, 0.10, 1.3, 1.0,  6.0, 1.0, 1.00,  600, 22, 38, 0.90, 45}, // Desert
        {2.5, 1.3,  4.0, 0.05, 0.15, 1.5, 5.0, 11.0, 0.5, 1.10,    5,  9, 19, 1.15, 36}, // Links
    };
    return p[int(e)];
}

// ---- geometry -------------------------------------------------------------------------------------

static Vec2 headingDir(double deg) { return {std::sin(deg2rad(deg)), std::cos(deg2rad(deg))}; }

double Ellipse::norm(Vec2 p) const {
    Vec2 d = headingDir(rotDeg);
    Vec2 r = p - c;
    double v = r.dot(d), u = r.dot(d.perp());
    return std::sqrt((u / rx) * (u / rx) + (v / ry) * (v / ry));
}
bool Ellipse::contains(Vec2 p, double grow) const {
    Ellipse e = *this;
    e.rx += grow; e.ry += grow;
    if (e.rx <= 0 || e.ry <= 0) return false;
    return e.norm(p) <= 1.0;
}
static double ellipseRadiusAlong(const Ellipse& e, Vec2 dir) { return 1.0 / e.norm(e.c + dir); }

double HoleLayout::centerlineLength() const {
    double l = 0;
    for (size_t i = 1; i < centerline.size(); ++i) l += (centerline[i] - centerline[i - 1]).length();
    return l;
}
Vec2 HoleLayout::pointAt(double s) const {
    for (size_t i = 1; i < centerline.size(); ++i) {
        double l = (centerline[i] - centerline[i - 1]).length();
        if (s <= l || i + 1 == centerline.size()) return centerline[i - 1] + (centerline[i] - centerline[i - 1]) * (s / l);
        s -= l;
    }
    return centerline.back();
}
double HoleLayout::project(Vec2 p, double* lateral) const {
    double best = 1e18, bestS = 0, acc = 0, bestLat = 0;
    for (size_t i = 1; i < centerline.size(); ++i) {
        Vec2 a = centerline[i - 1], b = centerline[i];
        double l = (b - a).length(), t;
        double d = distToSegment(p, a, b, &t);
        if (d < best) {
            best = d; bestS = acc + t * l;
            Vec2 dir = (b - a).normalized();
            bestLat = (p - a).dot(dir.perp()) >= 0 ? d : -d;
        }
        acc += l;
    }
    if (lateral) *lateral = bestLat;
    return bestS;
}
double HoleLayout::halfWidthAt(double s) const {
    // Fairways breathe: generous at the landing zones, pinched between them.
    double w = 0.82;
    for (const LandingZone& z : landingZones) {
        double zs = project(z.pos);
        w = std::max(w, 0.82 + 0.28 * (1.0 - smoothstep(15.0, 70.0, std::fabs(s - zs))));
    }
    return fairwayHalfWidth * w;
}
Vec2 HoleLayout::toWorld(Vec2 local) const { return worldOrigin + local.rotated(-deg2rad(worldHeadingDeg)); }
Vec3 HoleLayout::teePosition(int teeIndex) const {
    Vec2 p = tees[size_t(clampd(teeIndex, 0, double(tees.size() - 1)))].pos;
    return {p.x, p.y, height(p)};
}

double HoleLayout::coarseHeight(Vec2 p) const {
    double fx = clampd((p.x - gridMin.x) / cell, 0.0, nx - 1.001);
    double fy = clampd((p.y - gridMin.y) / cell, 0.0, ny - 1.001);
    int ix = int(fx), iy = int(fy);
    double tx = fx - ix, ty = fy - iy;
    auto at = [&](int x, int y) { return double(heights[size_t(y) * nx + x]); };
    return lerp(lerp(at(ix, iy), at(ix + 1, iy), tx), lerp(at(ix, iy + 1), at(ix + 1, iy + 1), tx), ty);
}

double HoleLayout::height(Vec2 p) const {
    if (fnx < 2 || p.x <= fineMin.x || p.y <= fineMin.y) return coarseHeight(p);
    double fx = (p.x - fineMin.x) / fineCell, fy = (p.y - fineMin.y) / fineCell;
    if (fx >= fnx - 1 || fy >= fny - 1) return coarseHeight(p);
    int ix = int(fx), iy = int(fy);
    double tx = fx - ix, ty = fy - iy;
    auto at = [&](int x, int y) { return double(fineHeights[size_t(y) * fnx + x]); };
    double fine = lerp(lerp(at(ix, iy), at(ix + 1, iy), tx), lerp(at(ix, iy + 1), at(ix + 1, iy + 1), tx), ty);
    // Fade to the coarse grid over the patch's outer 3 m, so there is never a step at its edge.
    double edge = std::min(std::min(fx, fnx - 1 - fx), std::min(fy, fny - 1 - fy)) * fineCell;
    if (edge >= 3.0) return fine;
    return lerp(coarseHeight(p), fine, smoothstep(0.0, 3.0, edge));
}

Surface HoleLayout::surface(Vec2 p) const {
    double lat;
    double s = project(p, &lat);
    double d = std::fabs(lat);
    if (d > corridorHalfWidth) return Surface::OutOfBounds;
    if (p.x < gridMin.x || p.y < gridMin.y || p.x > gridMin.x + (nx - 1) * cell || p.y > gridMin.y + (ny - 1) * cell)
        return Surface::OutOfBounds;
    for (const Ellipse& w : water) if (w.contains(p)) return Surface::Water;
    if (green.shape.contains(p)) return Surface::Green;
    for (const Bunker& b : bunkers) if (b.shape.contains(p)) return Surface::Sand;
    if (green.shape.contains(p, 3.5)) return Surface::Fringe;
    for (const TeeBox& t : tees) if ((p - t.pos).length() < 4.0) return Surface::Tee;
    if (s >= fairwayStart && s <= fairwayEnd) {
        double hw = halfWidthAt(s);
        if (d <= hw) return Surface::Fairway;
        if (d <= hw + 4.0) return Surface::FirstCut;
    }
    return d <= fairwayHalfWidth + 24.0 ? Surface::Rough : Surface::DeepRough;
}

bool HoleLayout::treeAt(Vec2 p, Vec2* centre, double* radius, double* height) const {
    for (const Tree& t : trees) {
        Vec2 d = p - t.pos;
        if (d.x * d.x + d.y * d.y < t.radius * t.radius) { *centre = t.pos; *radius = t.radius; *height = t.height; return true; }
    }
    return false;
}

double HoleLayout::obstacleHeight(Vec2 p) const {
    for (const Tree& t : trees) {
        Vec2 d = p - t.pos;
        if (d.x * d.x + d.y * d.y < t.radius * t.radius) return t.height;
    }
    return 0;
}

// ---- terrain --------------------------------------------------------------------------------------

namespace {

double lattice(int x, int y, uint64_t seed) {
    uint64_t h = seed ^ (uint64_t(uint32_t(x)) * 0x9E3779B97F4A7C15ULL) ^ (uint64_t(uint32_t(y)) * 0xC2B2AE3D27D4EB4FULL);
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ULL;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBULL;
    h ^= h >> 31;
    return (h >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0;
}
double noise(Vec2 p, double wavelength, uint64_t seed) {
    double x = p.x / wavelength, y = p.y / wavelength;
    int ix = int(std::floor(x)), iy = int(std::floor(y));
    double tx = x - ix, ty = y - iy;
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
    return lerp(lerp(lattice(ix, iy, seed), lattice(ix + 1, iy, seed), tx),
                lerp(lattice(ix, iy + 1, seed), lattice(ix + 1, iy + 1, seed), tx), ty);
}

struct GreenFeature { Vec2 c; double amp, r; };

// Value noise sampled in a rotated frame per octave so the lattice never lines up with the hole.
double rnoise(Vec2 p, double wavelength, uint64_t seed, double angle) { return noise(p.rotated(angle), wavelength, seed); }

struct TerrainDesign {
    const HoleLayout* h;
    uint64_t seed;
    double hillAmp, detailAmp, teeRaise;
    double rollAmp = 0, rollWave = 45;
    // Shaped slopes along the hole: a cross-slope (sidehill) through each landing area, and a crest or
    // hollow somewhere down the fairway so uphill and downhill lies are common.
    struct Cross { double s, pct; };
    std::vector<Cross> cross;
    double crestS = 0, crestAmp = 0, crestWidth = 40;
    // Green contours.
    std::vector<GreenFeature> bumps;
    bool tier = false; Vec2 tierDir; double tierOffset = 0, tierHeight = 0, tierWidth = 7;
    double crown = 0;

    double greenContour(Vec2 p) const {
        const Vec2 c = h->green.shape.c;
        double z = 0;
        for (const GreenFeature& f : bumps) {
            Vec2 d = p - f.c;
            z += f.amp * std::exp(-(d.x * d.x + d.y * d.y) / (f.r * f.r));
        }
        if (tier) z += tierHeight * smoothstep(-0.5 * tierWidth, 0.5 * tierWidth, (p - c).dot(tierDir) - tierOffset);
        if (crown != 0) {
            Vec2 d = p - c;
            double k = (d.x * d.x + d.y * d.y) / std::pow(std::max(h->green.shape.rx, h->green.shape.ry), 2);
            z += crown * (1.0 - std::min(1.5, k));
        }
        z += 0.035 * rnoise(p, 5.0, seed + 9, 0.7);       // the little breaks you only see when putting
        return z;
    }

    std::vector<double> teeHeights, waterLevels;
    double greenPad = 0;

    double natural(Vec2 p) const {
        double lat;
        double s = h->project(p, &lat);
        double frac = clampd(s / h->lengthM, 0.0, 1.0);
        double base = h->elevationChange * smoothstep(0.0, 1.0, frac);
        // Distance outside the mown fairway: the playing corridor is graded, the surrounds are wild.
        double out = std::max(0.0, std::fabs(lat) - h->halfWidthAt(s));
        if (s < h->fairwayStart) out += h->fairwayStart - s;
        if (s > h->fairwayEnd) out += s - h->fairwayEnd;
        double low = hillAmp * (0.65 * noise(p, 180, seed) + 0.35 * noise(p, 90, seed + 1));
        double high = detailAmp * (0.6 * noise(p, 40, seed + 2) + 0.4 * noise(p, 18, seed + 3));
        // Rolling ground through the playing corridor: mounds and swales that kick and hold a rolling ball.
        double roll = rollAmp * (0.6 * rnoise(p, rollWave, seed + 5, 0.53) + 0.4 * rnoise(p, rollWave * 0.55, seed + 6, 1.21));
        double side = 0;
        for (const Cross& c : cross) side += c.pct * 0.01 * std::exp(-((s - c.s) * (s - c.s)) / (55.0 * 55.0));
        double latSoft = 38.0 * std::tanh(lat / 38.0);                     // the hillside levels off away from play
        double crest = crestAmp * std::exp(-((s - crestS) * (s - crestS)) / (crestWidth * crestWidth));
        return base + roll + side * latSoft + crest + low * (0.35 + 0.65 * smoothstep(0, 45, out)) + high * (0.2 + 0.8 * smoothstep(0, 15, out));
    }

    void prepare() {
        for (const TeeBox& t : h->tees) teeHeights.push_back(natural(t.pos) + teeRaise);
        for (const Ellipse& w : h->water) {
            double lo = natural(w.c);
            for (int i = 0; i < 12; ++i) lo = std::min(lo, natural(w.c + headingDir(i * 30.0) * std::max(w.rx, w.ry)));
            waterLevels.push_back(lo - 0.8);
        }
        greenPad = natural(h->green.shape.c) + 0.4;
    }

    double design(Vec2 p) const {
        double z = natural(p);
        for (size_t i = 0; i < h->water.size(); ++i) {
            const Ellipse& w = h->water[i];
            double k = w.norm(p);
            double dOut = (k - 1.0) * std::min(w.rx, w.ry);
            z = lerp(z, waterLevels[i], 1.0 - smoothstep(0.0, 8.0, dOut));
        }
        for (size_t i = 0; i < h->tees.size(); ++i) {
            double d = (p - h->tees[i].pos).length();
            z = lerp(z, teeHeights[i], 1.0 - smoothstep(4.5, 14.0, d)); // tees are at least 14 m apart
        }
        const Ellipse& g = h->green.shape;
        double gk = g.norm(p);
        double gOut = (gk - 1.0) * std::min(g.rx, g.ry);
        if (gOut < 10.0) {
            Vec2 up = headingDir(h->green.tiltHeadingDeg);
            double plane = greenPad + (p - g.c).dot(up) * h->green.tiltPct * 0.01 + greenContour(p);
            z = lerp(z, plane, 1.0 - smoothstep(0.0, 10.0, gOut));
        }
        for (const Bunker& b : h->bunkers) {
            double k = b.shape.norm(p);
            if (k < 1.0) z -= (b.kind == BunkerKind::Greenside ? 0.7 : 0.45) * (1.0 - k * k);
        }
        return z;
    }
};

void buildHeightfield(HoleLayout& h, const TerrainDesign& td) {
    Vec2 lo{1e9, 1e9}, hi{-1e9, -1e9};
    for (Vec2 p : h.centerline) {
        lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y);
        hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y);
    }
    double pad = h.corridorHalfWidth + 12.0;
    h.gridMin = {lo.x - pad, lo.y - pad};
    h.nx = int(std::ceil((hi.x - lo.x + 2 * pad) / h.cell)) + 1;
    h.ny = int(std::ceil((hi.y - lo.y + 2 * pad) / h.cell)) + 1;
    h.heights.resize(size_t(h.nx) * h.ny);
    for (int y = 0; y < h.ny; ++y)
        for (int x = 0; x < h.nx; ++x)
            h.heights[size_t(y) * h.nx + x] = float(td.design({h.gridMin.x + x * h.cell, h.gridMin.y + y * h.cell}));
}

void buildGreenPatch(HoleLayout& h, const TerrainDesign& td) {
    const Ellipse& g = h.green.shape;
    double r = std::max(g.rx, g.ry) + 14.0;
    h.fineMin = {g.c.x - r, g.c.y - r};
    h.fnx = h.fny = int(std::ceil(2.0 * r / h.fineCell)) + 1;
    h.fineHeights.resize(size_t(h.fnx) * h.fny);
    for (int y = 0; y < h.fny; ++y)
        for (int x = 0; x < h.fnx; ++x)
            h.fineHeights[size_t(y) * h.fnx + x] = float(td.design({h.fineMin.x + x * h.fineCell, h.fineMin.y + y * h.fineCell}));
}

// Slope a ball actually feels: the worst gradient within ~1 m.
double localSlopePct(const ITerrain& t, Vec2 p) {
    double worst = 0;
    for (int i = 0; i < 5; ++i) {
        Vec2 q = i == 0 ? p : p + headingDir(i * 90.0) * 0.9;
        Vec3 n = t.normal(q);
        worst = std::max(worst, 100.0 * std::sqrt(std::max(0.0, 1.0 - n.z * n.z)) / std::max(1e-6, n.z));
    }
    return worst;
}

// Pins go where a greenkeeper would cut them: on the flatter shelves, spread around the green.
// pins[0] is the friendliest (nearest the middle); the rest run from safest to most tucked.
std::vector<Vec2> choosePins(const HoleLayout& h, double maxSlopePct) {
    const Ellipse& g = h.green.shape;
    double minR = std::min(g.rx, g.ry);
    std::vector<Vec2> cand;
    for (double x = -g.rx; x <= g.rx; x += 1.25)
        for (double y = -g.ry; y <= g.ry; y += 1.25) {
            Vec2 p = g.c + Vec2{x, y};
            if (g.norm(p) > 1.0 - 4.0 / minR) continue;
            if (localSlopePct(h, p) <= maxSlopePct) cand.push_back(p);
        }
    std::vector<Vec2> pins;
    if (cand.empty()) return pins;
    auto hazardDist = [&](Vec2 p) {
        double best = 1e9;
        for (const Bunker& b : h.bunkers) best = std::min(best, (p - b.shape.c).length() - std::max(b.shape.rx, b.shape.ry));
        for (const Ellipse& w : h.water) best = std::min(best, (w.norm(p) - 1.0) * std::min(w.rx, w.ry));
        return best;
    };
    Vec2 easy = cand[0];
    for (Vec2 p : cand) if ((p - g.c).length() < (easy - g.c).length()) easy = p;
    pins.push_back(easy);
    std::vector<Vec2> spread;
    for (int k = 0; k < 4; ++k) {   // farthest-point sampling
        Vec2 best = easy; double bestD = -1;
        for (Vec2 p : cand) {
            double d = (p - easy).length();
            for (Vec2 q : spread) d = std::min(d, (p - q).length());
            if (d > bestD) { bestD = d; best = p; }
        }
        spread.push_back(best);
    }
    std::sort(spread.begin(), spread.end(), [&](Vec2 a, Vec2 b) { return hazardDist(a) > hazardDist(b); });
    pins.insert(pins.end(), spread.begin(), spread.end());
    return pins;
}


bool nearAnything(const HoleLayout& h, Vec2 p, double margin) {
    if (h.green.shape.contains(p, margin)) return true;
    for (const Bunker& b : h.bunkers) if (b.shape.contains(p, margin)) return true;
    for (const Ellipse& w : h.water) if (w.contains(p, margin)) return true;
    return false;
}

} // namespace

// ---- hole design ----------------------------------------------------------------------------------

HoleLayout generateHole(Rng& rng, int par, int number, const CourseConfig& cfg) {
    const DifficultyParams& dp = difficultyParams(cfg.difficulty);
    const EnvironmentParams& ep = environmentParams(cfg.environment);
    static const double hazardScale[] = {0.5, 1.0, 1.5, 2.0};
    const double haz = dp.hazardMult * hazardScale[int(clampd(cfg.hazardDensity, 0, 3))];
    const bool wantWater = rng.chance(std::min(0.9, ep.waterChance * (0.6 + 0.4 * haz)));

    HoleLayout h;
    h.number = number;
    h.par = par;
    h.fairwayHalfWidth = dp.fairwayHalfWidth * ep.widthMult;
    h.corridorHalfWidth = h.fairwayHalfWidth + 46.0;

    // 1. Pick a template: each one is a proven strategic idea, not a random arrangement.
    if (par == 3) {
        double r = rng.uniform();
        h.tmpl = wantWater && r < 0.6 ? HoleTemplate::Par3Carry : (r < 0.5 ? HoleTemplate::Par3Open : HoleTemplate::Par3Guarded);
    } else if (par == 4) {
        double r = rng.uniform();
        h.tmpl = r < 0.22 ? HoleTemplate::Par4Straight : r < 0.52 ? HoleTemplate::Par4DoglegLeft
               : r < 0.82 ? HoleTemplate::Par4DoglegRight : HoleTemplate::Par4Short;
    } else {
        double r = rng.uniform();
        h.tmpl = wantWater && r < 0.45 ? HoleTemplate::Par5Cape : (r < 0.6 ? HoleTemplate::Par5DoubleDogleg : HoleTemplate::Par5Straight);
    }

    // 2. Routing of the hole itself.
    double L, bend1 = 0, bend2 = 0;
    std::vector<double> legs;
    if (par == 3) {
        L = rng.range(125, 205) * dp.lengthScale;
        if (h.tmpl == HoleTemplate::Par3Guarded) L = std::min(L, 165.0);
        legs = {L};
    } else if (par == 4) {
        bool shortHole = h.tmpl == HoleTemplate::Par4Short;
        L = (shortHole ? rng.range(275, 312) : rng.range(335, 425)) * dp.lengthScale;
        double d1 = clampd(dp.driveDistance + rng.range(-8, 10), 150.0, L - (shortHole ? 55.0 : 95.0));
        legs = {d1, L - d1};
        double mag = h.tmpl == HoleTemplate::Par4Straight || shortHole ? rng.range(0, 9) : rng.range(16, 36);
        bend1 = (h.tmpl == HoleTemplate::Par4DoglegLeft ? -1 : h.tmpl == HoleTemplate::Par4DoglegRight ? 1 : (rng.chance(0.5) ? 1 : -1)) * mag;
    } else {
        L = rng.range(458, 528) * dp.lengthScale;
        double d1 = dp.driveDistance + rng.range(-8, 10);
        double d3 = rng.range(88, 118);
        legs = {d1, L - d1 - d3, d3};
        double sign = rng.chance(0.5) ? 1 : -1;
        if (h.tmpl == HoleTemplate::Par5Straight) { bend1 = sign * rng.range(0, 10); bend2 = -sign * rng.range(0, 10); }
        else if (h.tmpl == HoleTemplate::Par5DoubleDogleg) { bend1 = sign * rng.range(14, 28); bend2 = -sign * rng.range(14, 28); }
        else { bend1 = sign * rng.range(4, 14); bend2 = sign * rng.range(18, 32); }
    }
    h.lengthM = L;
    h.centerline.push_back({0, 0});
    double heading = 0;
    for (size_t i = 0; i < legs.size(); ++i) {
        if (i == 1) heading += bend1;
        if (i == 2) heading += bend2;
        h.centerline.push_back(h.centerline.back() + headingDir(heading) * legs[i]);
    }
    const double approachHeading = heading;
    const Vec2 approachDir = headingDir(approachHeading);
    for (size_t i = 1; i + 1 < h.centerline.size(); ++i) h.landingZones.push_back({h.centerline[i], 18.0});

    // 3. Green: sized to the shot coming in (long approaches get bigger targets).
    double approachLen = legs.back();
    double sizeForShot = par == 3 ? lerp(0.9, 1.15, smoothstep(130, 200, L)) : lerp(0.92, 1.1, smoothstep(90, 190, approachLen));
    if (h.tmpl == HoleTemplate::Par4Short || h.tmpl == HoleTemplate::Par3Guarded) sizeForShot *= 0.9;
    h.green.shape.c = h.centerline.back();
    h.green.shape.rx = rng.range(11.5, 15.0) * dp.greenScale * sizeForShot;
    h.green.shape.ry = rng.range(13.5, 18.0) * dp.greenScale * sizeForShot;
    h.green.shape.rotDeg = approachHeading + rng.range(-28, 28);
    // Most greens tilt back-to-front so they accept the approach; some fall away to the side.
    h.green.tiltHeadingDeg = approachHeading + rng.range(-65, 65);
    h.green.tiltPct = rng.range(0.7, dp.maxGreenTiltPct);

    // 4. Fairway and tees.
    if (par == 3) {
        h.fairwayStart = L - 48.0;                    // an apron for the run-up shot
    } else {
        h.fairwayStart = std::max(95.0, 0.2 * L + 38.0);
    }
    h.fairwayEnd = L - h.green.shape.ry - 5.0;
    static const char* teeNames[] = {"Championship", "Back", "Middle", "Forward"};
    static const double teeFrac4[] = {0.0, 0.05, 0.11, 0.20};
    static const double teeFrac3[] = {0.0, 0.07, 0.16, 0.28};
    for (int i = 0; i < 4; ++i) {
        double off = std::max((par == 3 ? teeFrac3[i] : teeFrac4[i]) * L, i * 14.0);
        h.tees.push_back({teeNames[i], Vec2{i == 0 ? 0.0 : rng.range(-5, 5), off}, L - off});
    }

    // 5. Water first (it dictates where everything else may go).
    int waterSide = rng.chance(0.5) ? 1 : -1; // +1 = right of play
    if (h.tmpl == HoleTemplate::Par3Carry) {
        // The pond sits between the forward tee and the green; its far bank is never further from the
        // tee in play than this difficulty's longest fair carry.
        double playTee = h.tees[size_t(clampd(cfg.teeIndex, 0, 3))].pos.y;
        double front = h.tees.back().pos.y + 22.0;
        double back = std::min(L - h.green.shape.ry - 20.0, playTee + dp.maxForcedCarry - 6.0);
        if (back - front > 28.0) {
            Ellipse w; w.c = {rng.range(-6, 6), 0.5 * (front + back)}; w.rx = rng.range(30, 44); w.ry = 0.5 * (back - front); w.rotDeg = 0;
            h.water.push_back(w);
            h.fairwayStart = std::max(h.fairwayStart, back + 5.0);
        } else {
            h.tmpl = HoleTemplate::Par3Open;
        }
    } else if (h.tmpl == HoleTemplate::Par5Cape) {
        int inside = bend2 > 0 ? 1 : -1;
        waterSide = inside;
        Ellipse w; w.rx = rng.range(17, 26); w.ry = rng.range(38, 55); w.rotDeg = approachHeading;
        w.c = h.green.shape.c - approachDir * rng.range(18, 32) + approachDir.perp() * (inside * (h.green.shape.rx + 9.0 + w.rx));
        h.water.push_back(w);
    } else if (wantWater && par != 3) {
        // A lateral pond: beside the landing zone, or guarding one side of the green.
        bool atGreen = rng.chance(0.45);
        Ellipse w; w.rx = rng.range(14, 22); w.ry = rng.range(26, 44);
        if (atGreen) {
            w.rotDeg = approachHeading;
            w.c = h.green.shape.c - approachDir * rng.range(5, 22) + approachDir.perp() * (waterSide * (h.green.shape.rx + 9.0 + w.rx));
        } else {
            Vec2 lz = h.landingZones[0].pos;
            w.rotDeg = 0;
            w.c = lz - Vec2{0, rng.range(0, 30)} + Vec2{double(waterSide), 0} * (h.fairwayHalfWidth * 1.1 + 8.0 + w.rx);
        }
        h.water.push_back(w);
    }

    // 6. Bunkers, placed where they ask a question.
    auto addBunker = [&](Vec2 c, double rx, double ry, double rot, BunkerKind kind) {
        Bunker b{{c, rx, ry, rot}, kind};
        if (h.green.shape.contains(c, std::max(rx, ry) + 1.5)) return false;
        for (const Ellipse& w : h.water) if (w.contains(c, std::max(rx, ry) + 2.0)) return false;
        for (const Bunker& o : h.bunkers) if ((o.shape.c - c).length() < std::max(o.shape.rx, o.shape.ry) + std::max(rx, ry) + 2.0) return false;
        for (const TeeBox& t : h.tees) if ((t.pos - c).length() < 25.0) return false;
        h.bunkers.push_back(b);
        return true;
    };
    if (par >= 4) {
        Vec2 lz = h.landingZones[0].pos;
        double hw = h.fairwayHalfWidth * 1.1;
        int inside = std::fabs(bend1) > 12 ? (bend1 > 0 ? 1 : -1) : -waterSide;
        // Inside-corner bunker: the bold line shortens the hole but must carry it.
        if (rng.chance(std::min(1.0, 0.75 * haz)))
        { double back = rng.range(4, 18), rx = rng.range(6, 9), ry = rng.range(9, 14), rot = rng.range(-15, 15);
            addBunker(lz + Vec2{inside * (hw + 5.0), -back}, rx, ry, rot, BunkerKind::Fairway); }
        // Through-the-fairway bunker: catches the drive that is too long or bails out wide.
        if (rng.chance(std::min(1.0, 0.55 * haz)))
        { double fwd = rng.range(14, 30), rx = rng.range(6, 9), ry = rng.range(8, 12), rot = rng.range(-15, 15);
            addBunker(lz + Vec2{-inside * (hw + 4.0), fwd}, rx, ry, rot, BunkerKind::Fairway); }
        if (par == 5) {
            Vec2 lz2 = h.landingZones[1].pos;
            Vec2 d2 = (h.centerline[2] - h.centerline[1]).normalized();
            int side = rng.chance(0.5) ? 1 : -1;
            if (rng.chance(std::min(1.0, 0.7 * haz)))   // squeezes the lay-up
            { double back = rng.range(0, 20), rx = rng.range(6, 9), ry = rng.range(8, 12);
                addBunker(lz2 - d2 * back + d2.perp() * (side * (hw + 4.0)), rx, ry, rad2deg(std::atan2(d2.x, d2.y)), BunkerKind::Fairway); }
        }
        if (h.tmpl == HoleTemplate::Par4Short) {
            // The drivable par 4 defends itself 30-50 m short of the green.
            for (int side : {-1, 1}) {
                double back = rng.range(30, 48), wide = rng.range(12, 20), rx = rng.range(5, 8), ry = rng.range(6, 9);
                addBunker(h.green.shape.c - approachDir * back + approachDir.perp() * (side * wide), rx, ry, approachHeading, BunkerKind::Fairway);
            }
        }
    }
    {
        // Greenside: flank and back positions only, so a run-up through the front is always available.
        std::vector<double> angles = {130, 230, 90, 270, 0, 50, 310};
        for (size_t i = angles.size() - 1; i > 0; --i) std::swap(angles[i], angles[size_t(rng.rangeInt(0, int(i)))]);
        int want = int(std::round(clampd(rng.range(0.8, 2.6) * haz * ep.bunkerMult, 0.0, 4.0)));
        if (h.tmpl == HoleTemplate::Par3Guarded) want = std::max(want, 3);
        if (par == 3 && want == 0) want = 1;
        for (size_t i = 0; i < angles.size() && want > 0; ++i) {
            Vec2 dir = headingDir(approachHeading + angles[i]);
            double br = rng.range(4.5, 7.5);
            Vec2 c = h.green.shape.c + dir * (ellipseRadiusAlong(h.green.shape, dir) + br + 2.5);
            if (addBunker(c, br * rng.range(1.0, 1.5), br, approachHeading + angles[i] + 90, BunkerKind::Greenside)) --want;
        }
    }

    // 8. Terrain.
    h.elevationChange = rng.range(-ep.elevRange, ep.elevRange) * (par == 3 ? 0.6 : 1.0);
    TerrainDesign td;
    td.h = &h;
    td.seed = rng.next();
    td.hillAmp = ep.hillAmp;
    td.detailAmp = ep.detailAmp;
    td.teeRaise = ep.teeRaise * rng.range(0.6, 1.2);
    td.rollAmp = ep.rollAmp;
    td.rollWave = ep.rollWave;
    for (const LandingZone& z : h.landingZones) {           // sidehill lies in the landing areas
        double pct = rng.range(1.8, 4.2) * (rng.chance(0.5) ? 1.0 : -1.0);
        td.cross.push_back({h.project(z.pos), pct});
    }
    if (par >= 4) {                                         // a crest or hollow between tee and green
        td.crestS = rng.range(0.35, 0.8) * L;
        td.crestAmp = rng.range(1.2, 2.6) * (rng.chance(0.6) ? 1.0 : -1.0);
        td.crestWidth = rng.range(32, 50);
    }
    {   // Green contours, scaled by difficulty: a tier on some greens, ridges and hollows on all.
        const Ellipse& g = h.green.shape;
        double cs = dp.greenContour;
        int nb = rng.rangeInt(2, 4);
        for (int k = 0; k < nb; ++k) {
            double ang = rng.range(0, 360), rad = std::sqrt(rng.uniform()) * 0.75;
            Vec2 off = headingDir(ang);
            double amp = rng.range(0.09, 0.24) * cs * (rng.chance(0.55) ? 1.0 : -1.0);
            if (k == 0) amp = rng.range(0.17, 0.26) * std::max(cs, 0.8);   // every green has at least one real feature
            double r = rng.range(4.8, 8.0);
            td.bumps.push_back({g.c + Vec2{off.x * g.rx * rad, off.y * g.ry * rad}, amp, r});
        }
        td.tier = rng.chance(0.25 + 0.15 * int(cfg.difficulty));
        double tierAng = approachHeading + (rng.chance(0.7) ? 0.0 : (rng.chance(0.5) ? 90.0 : -90.0));
        td.tierDir = headingDir(tierAng);
        td.tierOffset = rng.range(-0.25, 0.25) * std::min(g.rx, g.ry);
        td.tierHeight = rng.range(0.20, 0.36) * cs;
        td.tierWidth = rng.range(8.5, 11.5);
        if (cfg.environment == Environment::Links || cfg.environment == Environment::Coastal) td.crown = rng.range(0.05, 0.18) * cs;
    }
    td.prepare();
    buildHeightfield(h, td);
    buildGreenPatch(h, td);

    // 7. Pin positions, cut on the real contoured surface.
    h.green.pins = choosePins(h, dp.maxPinSlopePct);
    if (h.green.pins.empty()) h.green.pins.push_back(h.green.shape.c);   // validation will reject this hole
    while (h.green.pins.size() < 5) h.green.pins.push_back(h.green.pins[0]);
    {
        int pd = int(clampd(cfg.pinDifficulty, 0, 3));
        h.pinIndex = pd == 0 ? 0 : pd == 1 ? rng.rangeInt(1, 2) : pd == 2 ? rng.rangeInt(2, 3) : rng.rangeInt(3, 4);
    }

    // 9. Trees frame the hole from outside the playing corridor.
    for (double s = 20.0; s < L + 30.0; s += 16.0) {
        for (int side : {-1, 1}) {
            if (!rng.chance(std::min(0.95, 0.55 * ep.treeDensity))) continue;
            Vec2 base = h.pointAt(std::min(s, L));
            size_t seg = 1;
            double acc = 0;
            for (; seg + 1 < h.centerline.size(); ++seg) {
                acc += (h.centerline[seg] - h.centerline[seg - 1]).length();
                if (s <= acc) break;
            }
            Vec2 dir = (h.centerline[seg] - h.centerline[seg - 1]).normalized();
            if (s > L) base = base + dir * (s - L);
            double wide = rng.range(13, 34), drift = rng.range(-6, 6);
            Vec2 p = base + dir.perp() * (side * (h.fairwayHalfWidth * 1.1 + wide)) + dir * drift;
            Tree t{p, rng.range(2.5, 4.5), rng.range(9, 16)};
            Surface sf = h.surface(p);
            if (sf != Surface::Rough && sf != Surface::DeepRough) continue;
            if (nearAnything(h, p, t.radius + 6.0)) continue;
            bool clash = false;
            for (const TeeBox& tb : h.tees) if ((tb.pos - p).length() < 14.0) clash = true;
            for (const Tree& o : h.trees) if ((o.pos - p).length() < o.radius + t.radius) clash = true;
            if (!clash) h.trees.push_back(t);
        }
    }
    return h;
}

// ---- share codes and the daily seed ---------------------------------------------------------------

namespace {
const char* kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"; // Crockford base32: no I, L, O, U
int alphaIndex(char c) {
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    if (c == 'O') c = '0';
    if (c == 'I' || c == 'L') c = '1';
    for (int i = 0; i < 32; ++i) if (kAlphabet[i] == c) return i;
    return -1;
}
uint8_t checksum4(const uint8_t* b) {
    unsigned s = 7;
    for (int i = 0; i < 9; ++i) s = s * 31 + b[i];
    s += b[9] & 0x0F;
    return uint8_t((s ^ (s >> 4) ^ (s >> 8) ^ (s >> 12)) & 0x0F);
}
}

std::string encodeShareCode(const CourseConfig& c) {
    uint8_t b[10];
    for (int i = 0; i < 8; ++i) b[i] = uint8_t(c.seed >> (8 * i));
    int pin = int(clampd(c.pinDifficulty, 0, 3));
    b[8] = uint8_t(int(c.difficulty) | (int(c.environment) << 2) | (int(clampd(c.hazardDensity, 0, 3)) << 5) | ((pin & 1) << 7));
    b[9] = uint8_t(((pin >> 1) & 1) | (int(clampd(c.teeIndex, 0, 3)) << 1) | ((c.holes == 18 ? 1 : 0) << 3));
    b[9] |= uint8_t(checksum4(b) << 4);
    std::string out = c.holes == 18 ? "R18" : "R9";
    for (int i = 0; i < 16; ++i) {
        int v = 0;
        for (int k = 0; k < 5; ++k) {
            int bit = i * 5 + k;
            v |= ((b[bit / 8] >> (bit % 8)) & 1) << k;
        }
        if (i % 4 == 0) out += '-';
        out += kAlphabet[v];
    }
    return out;
}

bool decodeShareCode(const std::string& code, CourseConfig* out) {
    size_t dash = code.find('-');
    if (dash == std::string::npos) return false;
    uint8_t b[10] = {0};
    int n = 0;
    for (size_t i = dash; i < code.size(); ++i) {
        if (code[i] == '-' || code[i] == ' ') continue;
        int v = alphaIndex(code[i]);
        if (v < 0 || n >= 16) return false;
        for (int k = 0; k < 5; ++k) {
            int bit = n * 5 + k;
            b[bit / 8] |= uint8_t(((v >> k) & 1) << (bit % 8));
        }
        ++n;
    }
    if (n != 16) return false;
    if ((b[9] >> 4) != checksum4(b)) return false;
    CourseConfig c;
    c.seed = 0;
    for (int i = 0; i < 8; ++i) c.seed |= uint64_t(b[i]) << (8 * i);
    c.difficulty = Difficulty(b[8] & 3);
    int env = (b[8] >> 2) & 7;
    if (env >= int(Environment::Count)) return false;
    c.environment = Environment(env);
    c.hazardDensity = (b[8] >> 5) & 3;
    c.pinDifficulty = ((b[8] >> 7) & 1) | ((b[9] & 1) << 1);
    c.teeIndex = (b[9] >> 1) & 3;
    c.holes = (b[9] >> 3) & 1 ? 18 : 9;
    if (out) *out = c;
    return true;
}

Date dateFromUnix(int64_t t) {
    int64_t z = (t >= 0 ? t : t - 86399) / 86400 + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = unsigned(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = int64_t(yoe) + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    return {int(y + (m <= 2)), int(m), int(d)};
}
int64_t secondsUntilNextDaily(int64_t t) {
    int64_t into = ((t % 86400) + 86400) % 86400;
    return 86400 - into;
}
std::string dailyId(Date d) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", d.year, d.month, d.day);
    return buf;
}
CourseConfig dailyConfig(Date d) {
    CourseConfig c;
    c.seed = hashString("NGG-DAILY-1:" + dailyId(d));
    Rng r(c.seed);
    c.environment = Environment(r.rangeInt(0, int(Environment::Count) - 1));
    c.difficulty = Difficulty::Advanced;   // one fair, fixed standard for everyone
    c.hazardDensity = 1;
    c.pinDifficulty = 2;
    c.teeIndex = 1;
    c.holes = 9;
    return c;
}

} // namespace golf

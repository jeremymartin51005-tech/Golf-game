#include "golf/BallFlight.h"

namespace golf {

namespace {

constexpr double kArea = kPi * kBallRadius * kBallRadius;
constexpr double kSpinDecayTau = 22.0;   // seconds; spin falls ~4-5% per second in flight
constexpr double kCupCentreSpeed = 1.63; // m/s: fastest dead-centre putt that still drops

struct Aero { double cd, cl; };
Aero aero(double speed, double spin) {
    double s = speed > 0.1 ? kBallRadius * spin / speed : 0.0; // spin ratio
    s = std::min(s, 0.45);
    Aero a;
    // Fitted so stock tour launch conditions reproduce published carry distances and apex heights
    // (see tests/test_physics.cpp). The lift offset fades out for a ball with almost no spin.
    a.cl = std::min(0.40, (0.12 + 0.80 * s - 1.00 * s * s) * std::min(1.0, s / 0.02));
    a.cd = 0.22 + 0.30 * s;
    return a;
}

struct State {
    Vec3 pos, vel, omega;
    bool airborne = true;
};

Vec3 accelAir(const Vec3& vel, const Vec3& omega, const Vec3& wind, double rho) {
    Vec3 vr = vel - wind;
    double sp = vr.length();
    Vec3 a{0, 0, -kGravity};
    if (sp < 1e-6) return a;
    double w = omega.length();
    Aero c = aero(sp, w);
    double k = 0.5 * rho * kArea / kBallMass;
    a += vr * (-k * c.cd * sp);
    if (w > 1e-6) {
        Vec3 dir = omega.cross(vr).normalized();
        a += dir * (k * c.cl * sp * sp);
    }
    return a;
}

ShotOutcome run(State st, double aimDeg, double startDirDeg, const ITerrain& terrain, const Conditions& cond,
                Cup cup, const FlightParams& fp) {
    ShotOutcome out;
    out.start = st.pos;
    const double rho = cond.airDensity();
    const double dt = fp.dt;
    const Vec2 aimDir{std::sin(deg2rad(aimDeg)), std::cos(deg2rad(aimDeg))};
    const Vec2 startDir{std::sin(deg2rad(startDirDeg)), std::cos(deg2rad(startDirDeg))};
    Vec3 lastDry = st.pos;
    bool landed = !st.airborne;
    bool lipLatch = false;
    double lastObstacleT = -10.0;
    double t = 0;
    double maxZ = st.pos.z;
    int step = 0;
    const int decim = std::max(1, int(std::round((1.0 / 60.0) / dt)));
    if (fp.recordPath) out.path.push_back({0, st.pos, st.airborne});

    auto finishAt = [&](RestReason why) {
        out.reason = why;
        out.rest = st.pos;
    };

    for (; t < fp.maxTime; t += dt, ++step) {
        Vec3 prevPos = st.pos;
        if (st.airborne) {
            double ground = terrain.height(st.pos.xy());
            Vec3 wind = cond.wind.at(st.pos.z - ground);
            // RK4 on position/velocity; spin changes slowly enough to hold over a step.
            Vec3 k1v = accelAir(st.vel, st.omega, wind, rho);
            Vec3 k2v = accelAir(st.vel + k1v * (dt * 0.5), st.omega, wind, rho);
            Vec3 k3v = accelAir(st.vel + k2v * (dt * 0.5), st.omega, wind, rho);
            Vec3 k4v = accelAir(st.vel + k3v * dt, st.omega, wind, rho);
            Vec3 k1x = st.vel, k2x = st.vel + k1v * (dt * 0.5), k3x = st.vel + k2v * (dt * 0.5), k4x = st.vel + k3v * dt;
            st.pos += (k1x + k2x * 2 + k3x * 2 + k4x) * (dt / 6.0);
            st.vel += (k1v + k2v * 2 + k3v * 2 + k4v) * (dt / 6.0);
            st.omega = st.omega * std::exp(-dt / kSpinDecayTau);
            maxZ = std::max(maxZ, st.pos.z);

            Vec2 xy = st.pos.xy();
            double h = terrain.height(xy);
            Surface surf = terrain.surface(xy);
            if (surf != Surface::Water && surf != Surface::OutOfBounds) lastDry = {xy.x, xy.y, h};

            // Trees: a solid trunk the ball ricochets off, and a crown of branches and leaves that
            // slows and deflects it the longer it stays inside (a clipped edge barely matters,
            // a ball through the middle drops out of the sky).
            Vec2 tc; double tr = 0, th = 0;
            if (terrain.treeAt(xy, &tc, &tr, &th)) {
                double above = st.pos.z - h;
                Vec2 off = xy - tc;
                double d = off.length();
                const double trunkR = 0.28;
                if (above > 0 && above < th * 0.55 && d < trunkR) {
                    if (t - lastObstacleT > 0.05) {
                        lastObstacleT = t;
                        if (!out.hitObstacle) out.obstaclePoint = st.pos;
                        out.hitObstacle = out.hitTrunk = true;
                        Vec2 nrm = d > 1e-6 ? off * (1.0 / d) : Vec2{-st.vel.x, -st.vel.y}.normalized();
                        Vec2 vh{st.vel.x, st.vel.y};
                        double vn = vh.dot(nrm);
                        if (vn < 0) {
                            Vec2 refl = vh - nrm * (vn * 1.45);            // restitution ~0.45 off bark
                            st.vel = {refl.x * 0.8, refl.y * 0.8, st.vel.z * 0.7};
                            st.omega = st.omega * 0.4;
                            st.pos = {tc.x + nrm.x * (trunkR + 0.01), tc.y + nrm.y * (trunkR + 0.01), st.pos.z};
                        }
                    }
                } else if (above > th * 0.3 && above < th) {
                    // Inside the crown: thicker toward the centre.
                    double density = 1.0 - clampd(d / tr, 0.0, 1.0);
                    double k = (3.0 + 9.0 * density) * dt;
                    if (!out.hitObstacle) { out.obstaclePoint = st.pos; out.hitObstacle = true; }
                    // Deterministic twig deflections, different for every tree and every point in it.
                    double hsh = std::sin(tc.x * 12.9898 + tc.y * 78.233 + std::floor(above * 3.0) * 37.719) * 43758.5453;
                    double jitter = (hsh - std::floor(hsh)) - 0.5;
                    Vec2 vh{st.vel.x, st.vel.y};
                    vh = vh.rotated(jitter * 1.2 * k);
                    double keep = std::exp(-k);
                    st.vel = {vh.x * keep, vh.y * keep, st.vel.z * (st.vel.z > 0 ? keep : 1.0)};
                    st.omega = st.omega * std::exp(-2.0 * k);
                }
            }

            if (st.pos.z <= h) {
                st.pos.z = h;
                if (!landed) {
                    landed = true;
                    out.landing = st.pos;
                    out.carry = (st.pos.xy() - out.start.xy()).length();
                    out.flightTime = t + dt;
                    double hs = st.vel.xy().length();
                    out.landingAngleDeg = rad2deg(std::atan2(-st.vel.z, hs));
                    out.curveM = (st.pos.xy() - out.start.xy()).dot(startDir.perp());
                }
                if (surf == Surface::Water) {
                    // A fast, shallow ball skips off water like a flat stone; anything else goes in.
                    double hs = Vec2{st.vel.x, st.vel.y}.length();
                    double angle = rad2deg(std::atan2(-st.vel.z, std::max(hs, 1e-6)));
                    out.splashes.push_back(st.pos);
                    if (hs > 18.0 && angle < 12.0 && out.waterSkips < 4) {
                        ++out.waterSkips;
                        st.vel = {st.vel.x * 0.62, st.vel.y * 0.62, -st.vel.z * 0.45 + 0.6};
                        st.omega = st.omega * 0.5;
                        st.pos.z = h + 0.02;
                        continue;
                    }
                    finishAt(RestReason::Water);
                    break;
                }
                if (surf == Surface::OutOfBounds) { finishAt(RestReason::OutOfBounds); break; }
                if (cup.present && (xy - cup.pos).length() < 0.045) { st.pos = {cup.pos.x, cup.pos.y, h}; finishAt(RestReason::Holed); break; }

                SurfaceProps sp = surfaceProps(surf, cond.stimpFeet, cond.wetness);
                Vec3 n = terrain.normal(xy);
                double vn = st.vel.dot(n);
                if (vn < 0) {
                    ++out.bounces;
                    // Turf is not rigid: the ball ploughs a pitch mark, so it rebounds as if from a surface
                    // tilted back toward its line of flight (Penner, "The run of a golf ball"). Softer
                    // surfaces tilt more. This is what stops a steep wedge and lets a shallow drive skip.
                    double vin = st.vel.length();
                    Vec3 vtIn = st.vel - n * vn;
                    double thetaI = std::atan2(vtIn.length(), -vn); // from the normal
                    double soft = 0.42 / std::max(0.12, sp.restitution);
                    double thetaC = std::min(deg2rad(32.0), deg2rad(15.4) * (vin / 18.6) * (thetaI / deg2rad(44.4)) * soft);
                    if (vtIn.length() > 1e-6) n = (n * std::cos(thetaC) - vtIn.normalized() * std::sin(thetaC)).normalized();
                    vn = st.vel.dot(n);
                    Vec3 vt = st.vel - n * vn;
                    double sn = -vn;
                    double e = sn <= 20.0 ? 0.510 - 0.0375 * sn + 0.000903 * sn * sn : 0.120;
                    e *= sp.restitution / 0.42;
                    // Friction acts on the contact point, whose velocity includes spin: this is what makes
                    // a high-spin wedge check and a low-spin drive release.
                    Vec3 slip = vt + st.omega.cross(n * -kBallRadius);
                    double slipSpeed = slip.length();
                    if (slipSpeed > 1e-6) {
                        Vec3 sdir = slip * (1.0 / slipSpeed);
                        double j = std::min(sp.friction * (1.0 + e) * -vn, (2.0 / 7.0) * slipSpeed);
                        vt = vt - sdir * j;
                        st.omega += n.cross(sdir) * (j / (0.4 * kBallRadius));
                    }
                    st.vel = vt + n * (-e * vn);
                    if (surf == Surface::Sand) st.vel = st.vel * 0.3; // sand swallows the impact
                    Vec3 rn = terrain.normal(xy);
                    if (st.vel.dot(rn) < 0.6) { // too little energy to leave the ground again
                        st.airborne = false;
                        st.vel = st.vel - rn * st.vel.dot(rn);
                    } else {
                        st.pos.z = h + 1e-4;
                    }
                }
            }
        } else {
            Vec2 xy = st.pos.xy();
            Surface surf = terrain.surface(xy);
            SurfaceProps sp = surfaceProps(surf, cond.stimpFeet, cond.wetness);
            Vec3 n = terrain.normal(xy);
            Vec3 g{0, 0, -kGravity};
            Vec3 gt = g - n * g.dot(n);
            double speed = st.vel.length();
            double resist = sp.rollResist * kGravity * n.z;

            if (speed < 0.03 && gt.length() <= resist) { finishAt(RestReason::Stopped); break; }

            Vec3 slip = st.vel + st.omega.cross(n * -kBallRadius);
            slip = slip - n * slip.dot(n);
            double slipSpeed = slip.length();
            Vec3 a = gt;
            if (slipSpeed > 0.05) {
                // Still skidding (or spinning back): sliding friction until the ball rolls true.
                Vec3 sdir = slip * (1.0 / slipSpeed);
                double j = std::min(sp.friction * kGravity * dt, (2.0 / 7.0) * slipSpeed);
                st.vel = st.vel - sdir * j;
                st.omega += n.cross(sdir) * (j / (0.4 * kBallRadius));
            } else {
                if (speed > 1e-6) {
                    double dv = std::min(speed, resist * dt);
                    st.vel = st.vel - st.vel * (dv / speed);
                }
                st.omega = n.cross(st.vel) * (1.0 / kBallRadius); // pure rolling
            }
            st.vel += a * dt;
            st.vel = st.vel - n * st.vel.dot(n);
            st.pos += st.vel * dt;
            st.pos.z = terrain.height(st.pos.xy());

            {   // A rolling ball that runs into a trunk bounces off it.
                Vec2 tc; double tr = 0, th = 0;
                if (terrain.treeAt(st.pos.xy(), &tc, &tr, &th)) {
                    Vec2 off = st.pos.xy() - tc;
                    double d = off.length();
                    if (d < 0.28 && d > 1e-6) {
                        Vec2 nrm = off * (1.0 / d), vh{st.vel.x, st.vel.y};
                        double vn = vh.dot(nrm);
                        if (vn < 0) { Vec2 r = vh - nrm * (vn * 1.4); st.vel = {r.x * 0.7, r.y * 0.7, 0}; out.hitObstacle = out.hitTrunk = true; }
                    }
                }
            }
            Surface ns = terrain.surface(st.pos.xy());
            if (ns == Surface::Water) { out.splashes.push_back(st.pos); finishAt(RestReason::Water); break; }
            if (ns == Surface::OutOfBounds) { finishAt(RestReason::OutOfBounds); break; }
            lastDry = st.pos;

            if (cup.present) {
                // Judge the putt by how close its line passes to the centre of the hole, not by where
                // this particular timestep happened to end.
                double u;
                double seg = distToSegment(cup.pos, prevPos.xy(), st.pos.xy(), &u);
                Vec2 vdir = st.vel.xy().normalized();
                Vec2 rel0 = cup.pos - prevPos.xy();
                double d = std::fabs(vdir.x * rel0.y - vdir.y * rel0.x);
                if (seg < kCupRadius && d < kCupRadius) {
                    if (!lipLatch) {
                        double rel = d / kCupRadius;
                        double capture = kCupCentreSpeed * std::sqrt(std::max(0.0, 1.0 - rel * rel));
                        speed = st.vel.length();
                        if (speed <= capture) {
                            st.pos = {cup.pos.x, cup.pos.y, terrain.height(cup.pos)};
                            finishAt(RestReason::Holed);
                            break;
                        }
                        // Lip-out: the ball rides the rim, turning toward the hole and shedding speed.
                        lipLatch = true;
                        Vec2 v2 = st.vel.xy();
                        Vec2 toCup = cup.pos - prevPos.xy();
                        double side = v2.x * toCup.y - v2.y * toCup.x; // >0: cup is to the left
                        double excess = clampd(capture / speed, 0.0, 1.0);
                        double turn = deg2rad(55.0) * rel * excess * (side > 0 ? 1.0 : -1.0);
                        Vec2 nv = v2.rotated(turn) * (0.85 - 0.25 * rel * excess);
                        st.vel = {nv.x, nv.y, 0};
                        st.vel = st.vel - n * st.vel.dot(n);
                    }
                } else if ((st.pos.xy() - cup.pos).length() > 0.2) {
                    lipLatch = false;
                }
            }
        }
        if (fp.recordPath && step % decim == 0) out.path.push_back({t + dt, st.pos, st.airborne});
    }
    if (t >= fp.maxTime) finishAt(RestReason::TimedOut);

    if (!landed) { out.landing = st.pos; out.carry = 0; }
    out.totalTime = t;
    out.total = (out.rest.xy() - out.start.xy()).length();
    out.apex = maxZ - out.start.z;
    out.lateralAtRest = (out.rest.xy() - out.start.xy()).dot(aimDir.perp());
    out.finalSpinRpm = rad2rpm(st.omega.length());
    out.restSurface = terrain.surface(out.rest.xy());
    out.hazardEntry = lastDry;
    if (fp.recordPath) out.path.push_back({t, st.pos, st.airborne});
    return out;
}

} // namespace

ShotOutcome simulateShot(const Launch& L, Vec3 startPos, double aimDeg, const ITerrain& terrain,
                         const Conditions& cond, Cup cup, const FlightParams& fp) {
    State st;
    st.pos = startPos;
    double heading = aimDeg + L.azimuthDeg;
    Vec3 h{std::sin(deg2rad(heading)), std::cos(deg2rad(heading)), 0};
    double la = deg2rad(L.launchDeg);
    st.vel = h * (L.ballSpeed * std::cos(la)) + Vec3{0, 0, L.ballSpeed * std::sin(la)};
    Vec3 up{0, 0, 1};
    Vec3 side = h.cross(up);
    double tilt = deg2rad(L.spinAxisDeg);
    st.omega = (side * std::cos(tilt) - up * std::sin(tilt)) * rpm2rad(L.spinRpm);
    st.airborne = true;
    st.pos.z += 1e-3;
    return run(st, aimDeg, heading, terrain, cond, cup, fp);
}

ShotOutcome simulatePutt(double speed, Vec3 startPos, double aimDeg, const ITerrain& terrain,
                         const Conditions& cond, Cup cup, const FlightParams& fp) {
    State st;
    st.pos = startPos;
    st.pos.z = terrain.height(startPos.xy());
    Vec3 n = terrain.normal(startPos.xy());
    Vec3 h{std::sin(deg2rad(aimDeg)), std::cos(deg2rad(aimDeg)), 0};
    Vec3 tangent = (h - n * h.dot(n)).normalized();
    st.vel = tangent * speed;
    st.omega = n.cross(st.vel) * (1.0 / kBallRadius);
    st.airborne = false;
    return run(st, aimDeg, aimDeg, terrain, cond, cup, fp);
}

PuttLaunch computePutt(const SwingResult& sw, double maxPuttSpeed, const PlayerAttributes& attr) {
    PuttLaunch p{0, 0};
    if (!sw.valid) return p;
    double skill = 1.20 - 0.40 * attr.putting;
    // Backswing length sets pace; a rushed stroke hits it harder, a decelerating one leaves it short.
    // Progressive stroke: most of the backswing range is devoted to short and medium putts, so a tap-in
    // is a small but deliberate stroke rather than a twitch.
    double pace = std::pow(sw.power, 1.6) * (1.0 - clampd(sw.timingError, -0.5, 0.5) * 0.25 * skill);
    if (sw.contact == Contact::Fat) pace *= 0.6 + 0.4 * sw.contactQuality;
    p.speed = maxPuttSpeed * pace;
    p.azimuthDeg = (0.35 * sw.faceDeg + 0.10 * sw.pathDeg) * skill;
    return p;
}

PuttRead readPutt(Vec3 ballPos, Cup cup, const ITerrain& terrain, const Conditions& cond) {
    PuttRead r{false, 0, 0, 0, 0};
    Vec2 to = cup.pos - ballPos.xy();
    double dist = to.length();
    if (dist < 1e-3) { r.found = true; return r; }
    double direct = rad2deg(std::atan2(to.x, to.y));
    Vec2 dir = to.normalized();
    r.elevationM = terrain.height(cup.pos) - terrain.height(ballPos.xy());
    double a = stimpToRollResist(cond.stimpFeet) * (1.0 + 0.5 * cond.wetness) * kGravity;
    const double past = 0.35; // ideal pace finishes just past the hole
    double speed = std::sqrt(std::max(0.05, 2.0 * a * (dist + past) + 2.0 * kGravity * r.elevationM));
    double aim = direct;
    FlightParams fp;
    fp.recordPath = true;
    Cup none;
    for (int i = 0; i < 16; ++i) {
        ShotOutcome o = simulatePutt(speed, ballPos, aim, terrain, cond, none, fp);
        // Signed miss where the path passes the hole.
        double best = 1e9, miss = 0;
        for (const TrajectoryPoint& tp : o.path) {
            Vec2 rel = tp.pos.xy() - cup.pos;
            double d = rel.length();
            if (d < best) { best = d; miss = rel.dot(dir.perp()); }
        }
        double along = (o.rest.xy() - ballPos.xy()).dot(dir);
        if (std::fabs(miss) < 0.01 && std::fabs(along - (dist + past)) < 0.08) break;
        aim -= rad2deg(std::atan2(miss, dist)) * 0.9;
        if (along > 0.05) speed *= std::pow((dist + past) / along, 0.5 * 0.8);
        else speed *= 1.3;
        speed = clampd(speed, 0.1, 12.0);
    }
    ShotOutcome fin = simulatePutt(speed, ballPos, aim, terrain, cond, cup, fp);
    r.found = fin.reason == RestReason::Holed;
    r.aimDeg = aim;
    r.speed = speed;
    r.breakM = dist * std::tan(deg2rad(aim - direct));
    return r;
}

} // namespace golf

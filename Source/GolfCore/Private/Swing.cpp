#include "golf/Swing.h"

namespace golf {

SwingSettings SwingSettings::forDifficulty(SwingDifficulty d) {
    SwingSettings s;
    switch (d) {
        case SwingDifficulty::Beginner:     s.assist = 0.65; s.timingWindow = 0.20; s.pathDeadzoneDeg = 6.0; break;
        case SwingDifficulty::Intermediate: s.assist = 0.35; s.timingWindow = 0.12; s.pathDeadzoneDeg = 3.5; break;
        case SwingDifficulty::Advanced:     s.assist = 0.12; s.timingWindow = 0.07; s.pathDeadzoneDeg = 2.0; break;
        case SwingDifficulty::Simulation:   s.assist = 0.00; s.timingWindow = 0.04; s.pathDeadzoneDeg = 1.0; break;
    }
    return s;
}

namespace {

double idealDownTime(const SwingSettings& s, double power) {
    // Shorter swings are a little quicker, but tempo stays recognisably the same.
    return s.idealDownswingTime * (0.6 + 0.4 * clampd(power, 0.0, 1.0));
}

double deadzone(double v, double dz) {
    if (std::fabs(v) <= dz) return 0.0;
    return v > 0 ? v - dz : v + dz;
}

struct RawSwing {
    double backFrac = 0;        // backswing / calibrated full backswing
    double timingError = 0;     // raw (actual-ideal)/ideal
    double mousePathDeg = 0;    // direction of the downswing in mouse space
    double releaseDeg = 0;      // direction of the last 20% minus overall direction
    double shortfall = 0;       // fraction of the backswing NOT returned (0 = reached the ball)
    double speedRatio = 1;      // speed through impact / peak speed
    double backTime = 0, downTime = 0;
};

// The single place where execution becomes a SwingResult. Every control scheme funnels through here.
SwingResult resolve(const RawSwing& r, const SwingSettings& s, const PlayerAttributes& a) {
    SwingResult out;
    out.valid = true;
    out.power = clampd(r.backFrac, 0.0, 1.0);
    out.overswing = std::max(0.0, r.backFrac - 1.0);
    out.backswingTime = r.backTime;
    out.downswingTime = r.downTime;
    out.tempoRatio = r.downTime > 1e-6 ? r.backTime / r.downTime : 0.0;

    // Skill of the created golfer and the assist setting shrink mistakes; they never create good shots.
    double errScale = (1.0 - clampd(s.assist, 0.0, 1.0)) * (1.15 - 0.30 * a.consistency) * (1.0 + 4.0 * out.overswing);

    double te = deadzone(r.timingError, s.timingWindow);
    out.timingError = te;

    double speed = std::pow(out.power, 0.8);
    speed *= te < 0 ? 1.0 + std::min(0.03, -te * 0.12)       // rushing buys a little speed...
                    : 1.0 - std::min(0.40, te * 0.50);       // ...a lazy downswing loses a lot
    speed *= 1.0 + std::min(0.03, out.overswing * 0.2);
    speed *= 0.90 + 0.10 * a.power; // 1.0 = tour-level speed
    out.speedFactor = speed;

    double mousePath = deadzone(r.mousePathDeg, s.pathDeadzoneDeg);
    out.pathDeg = clampd(mousePath * s.pathGain * errScale, -10.0, 10.0);

    double timingFace = te * s.timingFaceGainDeg * errScale;           // early = closed, late = open
    double releaseFace = deadzone(r.releaseDeg, s.pathDeadzoneDeg) * s.releaseFaceGainDeg * errScale;
    out.faceDeg = clampd(0.5 * out.pathDeg + timingFace + releaseFace, -12.0, 12.0);

    if (r.shortfall > 0.02) {
        out.contact = Contact::Thin;
        out.contactQuality = 1.0 - std::min(0.7, r.shortfall * 1.5) * std::min(1.0, errScale + 0.3);
    } else if (r.speedRatio < 0.70) {
        out.contact = Contact::Fat;
        double q = clampd(r.speedRatio / 0.70, 0.3, 1.0);
        out.contactQuality = 1.0 - (1.0 - q) * std::min(1.0, errScale + 0.3);
    }

    auto& fb = out.feedback;
    if (out.contact == Contact::Thin) fb.push_back("Thin: the downswing stopped before it reached the ball.");
    if (out.contact == Contact::Fat) fb.push_back("Fat: you slowed down through impact instead of accelerating.");
    if (te < -0.01) fb.push_back("Early: the downswing was rushed, which closed the clubface.");
    if (te > 0.01) fb.push_back("Late: the downswing was slow, which left the face open and cost distance.");
    if (out.overswing > 0.03) fb.push_back("Overswing: the backswing went past 100%, magnifying every error.");
    if (out.pathDeg > 0.5) fb.push_back("Path in-to-out: the mouse travelled to the right on the way through.");
    if (out.pathDeg < -0.5) fb.push_back("Path out-to-in: the mouse travelled to the left on the way through.");
    if (releaseFace > 0.3) fb.push_back("Face held open: the mouse drifted right just before impact.");
    if (releaseFace < -0.3) fb.push_back("Face flipped closed: the mouse drifted left just before impact.");
    if (fb.empty()) fb.push_back("Pure: on plane, on tempo, centre strike.");
    return out;
}

} // namespace

MouseSwing::MouseSwing(SwingSettings s, PlayerAttributes a) : set_(s), attr_(a) {}

void MouseSwing::reset() {
    phase_ = SwingPhase::Address;
    pos_ = {};
    topY_ = topT_ = startT_ = lastT_ = lastMoveT_ = 0;
    topPos_ = {};
    down_.clear();
    result_ = {};
}

double MouseSwing::livePower() const {
    double y = phase_ == SwingPhase::Backswing ? pos_.y : topY_;
    return y / set_.backswingCounts;
}

void MouseSwing::addSample(const MouseSample& m) {
    if (finished()) return;
    double dx = m.dx * set_.sensitivity * (set_.leftHanded ? -1.0 : 1.0);
    double dy = m.dy * set_.sensitivity;
    bool moved = dx != 0.0 || dy != 0.0;
    double prevT = lastT_;
    lastT_ = m.t;
    if (moved) lastMoveT_ = m.t;
    const double bc = set_.backswingCounts;

    if (phase_ == SwingPhase::Address) {
        bool wasAtRest = pos_.y <= 0.0;
        pos_ = pos_ + Vec2{dx, dy};
        if (pos_.y <= 0.0) { pos_ = {}; return; }       // forward waggles at address do nothing
        if (wasAtRest) startT_ = prevT > 0 ? prevT : m.t;
        if (pos_.y > 0.04 * bc) { phase_ = SwingPhase::Backswing; topY_ = pos_.y; topT_ = m.t; topPos_ = pos_; }
        return;
    }

    pos_ = pos_ + Vec2{dx, dy};

    if (phase_ == SwingPhase::Backswing) {
        if (pos_.y >= topY_) {
            topY_ = pos_.y; topT_ = m.t; topPos_ = pos_;
            down_.clear();
        } else {
            down_.push_back({m.t, pos_});
            if (pos_.y < topY_ - std::max(0.03 * bc, 6.0)) phase_ = SwingPhase::Downswing;
        }
        if (phase_ != SwingPhase::Downswing) return;
    } else {
        down_.push_back({m.t, pos_});
    }

    // Downswing
    if (pos_.y <= 0.0) { finish(true); return; }
    double minY = topY_;
    for (const Pt& p : down_) minY = std::min(minY, p.p.y);
    if (pos_.y > minY + 0.05 * bc) finish(false); // reversed direction mid-downswing
}

void MouseSwing::tick(double now) {
    if (phase_ == SwingPhase::Downswing && now - lastMoveT_ > 0.12) finish(false);
}

void MouseSwing::finish(bool reachedBall) {
    const double bc = set_.backswingCounts;
    RawSwing r;
    r.backFrac = topY_ / bc;
    if (down_.empty() || r.backFrac < 0.08) { phase_ = SwingPhase::Cancelled; return; }

    double downStart = std::max(topT_, down_.front().t - 0.02);
    Vec2 impact = down_.back().p;
    double impactT = down_.back().t;
    if (reachedBall && down_.size() >= 2) {
        const Pt& a = down_[down_.size() - 2];
        const Pt& b = down_.back();
        double span = a.p.y - b.p.y;
        double u = span > 1e-9 ? clampd(a.p.y / span, 0.0, 1.0) : 1.0;
        impact = a.p + (b.p - a.p) * u;
        impactT = a.t + (b.t - a.t) * u;
    } else if (!reachedBall) {
        // Use the lowest point reached, and when it was reached.
        for (const Pt& p : down_) if (p.p.y < impact.y) { impact = p.p; impactT = p.t; }
    }

    double travelled = topY_ - impact.y;
    r.downTime = impactT - downStart;
    r.backTime = std::max(0.0, topT_ - startT_);
    double ideal = idealDownTime(set_, r.backFrac);
    // A short or very slow return is the player backing off, not a swing.
    if ((!reachedBall && travelled < 0.25 * topY_) || r.downTime > 4.0 * ideal || r.downTime <= 0.0) {
        phase_ = SwingPhase::Cancelled;
        return;
    }
    r.shortfall = reachedBall ? 0.0 : impact.y / topY_;
    // Judge timing on the distance actually covered so a thin shot isn't also called "early".
    r.timingError = (r.downTime / std::max(0.25, travelled / topY_) - ideal) / ideal;

    Vec2 d = impact - topPos_;
    r.mousePathDeg = rad2deg(std::atan2(d.x, -d.y));

    // Last 20% of the travel: direction relative to the whole downswing.
    double y80 = impact.y + 0.2 * travelled;
    Vec2 p80 = topPos_;
    double t85 = downStart;
    double y85 = impact.y + 0.15 * travelled;
    Vec2 prev = topPos_;
    double prevT = downStart;
    bool got80 = false, got85 = false;
    double peak = 0;
    size_t w0 = 0;
    for (size_t i = 0; i < down_.size(); ++i) {
        const Pt& p = down_[i];
        if (p.t > impactT + 1e-9) break;
        if (!got80 && p.p.y <= y80) {
            double span = prev.y - p.p.y;
            double u = span > 1e-9 ? (prev.y - y80) / span : 0.0;
            p80 = prev + (p.p - prev) * u;
            got80 = true;
        }
        if (!got85 && p.p.y <= y85) {
            double span = prev.y - p.p.y;
            double u = span > 1e-9 ? (prev.y - y85) / span : 0.0;
            t85 = prevT + (p.t - prevT) * u;
            got85 = true;
        }
        // Peak speed over a ~20 ms sliding window (robust to 125 Hz mice and jitter).
        while (w0 < i && p.t - down_[w0].t > 0.02) ++w0;
        if (w0 < i && p.t - down_[w0].t >= 0.008)
            peak = std::max(peak, (p.p - down_[w0].p).length() / (p.t - down_[w0].t));
        prev = p.p; prevT = p.t;
    }
    if (got80) {
        Vec2 d2 = impact - p80;
        if (d2.length() > 1e-6) r.releaseDeg = rad2deg(std::atan2(d2.x, -d2.y)) - r.mousePathDeg;
    }
    if (got85 && peak > 0 && impactT - t85 > 1e-6) {
        double endSpeed = 0.15 * travelled / (impactT - t85);
        r.speedRatio = clampd(endSpeed / peak, 0.0, 1.0);
    }

    result_ = resolve(r, set_, attr_);
    phase_ = SwingPhase::Done;
}

SwingResult classicMeterSwing(double power, double accuracy, const SwingSettings& s, const PlayerAttributes& a) {
    RawSwing r;
    r.backFrac = clampd(power, 0.0, 1.15);
    r.timingError = clampd(accuracy, -1.0, 1.0) * 0.35;
    double ideal = idealDownTime(s, r.backFrac);
    r.downTime = ideal * (1.0 + r.timingError);
    r.backTime = ideal * s.idealTempo;
    return resolve(r, s, a);
}

SwingResult assistedSwing(double power, const PlayerAttributes& a) {
    SwingSettings s = SwingSettings::forDifficulty(SwingDifficulty::Beginner);
    RawSwing r;
    r.backFrac = clampd(power, 0.0, 1.0);
    r.downTime = idealDownTime(s, r.backFrac);
    r.backTime = r.downTime * s.idealTempo;
    SwingResult out = resolve(r, s, a);
    out.speedFactor *= 0.95; // the price of a guaranteed-straight swing
    out.feedback = {"Assisted swing: straight, at 95% of your full distance."};
    return out;
}

std::vector<MouseSample> synthesizeSwing(const SyntheticSwing& p, const SwingSettings& s) {
    std::vector<MouseSample> out;
    const double dt = 1.0 / p.rateHz;
    const double top = p.power * s.backswingCounts / s.sensitivity;
    const double td = idealDownTime(s, p.power) * p.downTimeScale;
    const double tb = idealDownTime(s, p.power) * s.idealTempo;
    const double tanPath = std::tan(deg2rad(p.pathAngleDeg));
    const double tanRel = std::tan(deg2rad(p.pathAngleDeg + p.lateReleaseDeg)) - tanPath;
    const double endY = p.stopShortFrac * top;
    const double follow = p.stopShortFrac > 0 ? 0.0 : 0.03;

    auto posAt = [&](double t) -> Vec2 {
        if (t <= tb) {
            double u = t / tb;
            return {0.0, top * 0.5 * (1.0 - std::cos(kPi * u))};
        }
        double u = (t - tb) / td;
        double y;
        if (u <= 1.0) {
            double accel = u * u, ease = 1.0 - (1.0 - u) * (1.0 - u);
            y = top - (top - endY) * lerp(accel, ease, p.decelerate);
        } else {
            double vEnd = (top - endY) / td * lerp(2.0, 0.0, p.decelerate);
            y = endY - vEnd * (t - tb - td);
        }
        double travel = top - y;
        double x = tanPath * travel + tanRel * std::max(0.0, travel - 0.8 * (top - endY));
        return {x, y};
    };

    Vec2 prev{0, 0};
    double t0 = 1.0; // arbitrary non-zero start time
    out.push_back({t0, 0, 0});
    for (double t = dt; t <= tb + td + follow + 1e-9; t += dt) {
        Vec2 p2 = posAt(t);
        out.push_back({t0 + t, p2.x - prev.x, p2.y - prev.y});
        prev = p2;
    }
    return out;
}

SwingResult runSwing(const std::vector<MouseSample>& samples, const SwingSettings& s, const PlayerAttributes& a) {
    MouseSwing sw(s, a);
    for (const MouseSample& m : samples) sw.addSample(m);
    if (!samples.empty()) sw.tick(samples.back().t + 0.5);
    return sw.result();
}

} // namespace golf

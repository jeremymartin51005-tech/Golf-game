#include "golf/Surface.h"

namespace golf {

const char* surfaceName(Surface s) {
    switch (s) {
        case Surface::Tee: return "Tee";
        case Surface::Fairway: return "Fairway";
        case Surface::FirstCut: return "First Cut";
        case Surface::Rough: return "Rough";
        case Surface::DeepRough: return "Deep Rough";
        case Surface::Sand: return "Sand";
        case Surface::Green: return "Green";
        case Surface::Fringe: return "Fringe";
        case Surface::HardGround: return "Hard Ground";
        case Surface::Water: return "Water";
        case Surface::OutOfBounds: return "Out of Bounds";
    }
    return "?";
}

double stimpToRollResist(double stimpFeet) {
    // A stimpmeter releases the ball at 1.83 m/s; it rolls `stimp` feet on a flat green.
    const double v0 = 1.83;
    double d = std::max(4.0, stimpFeet) * 0.3048;
    return v0 * v0 / (2.0 * d) / kGravity;
}

SurfaceProps surfaceProps(Surface s, double stimpFeet, double wetness) {
    SurfaceProps p{};
    switch (s) {
        case Surface::Tee:
        case Surface::Fairway:    p = {0.42, 0.45, 0.160, 1.00, 1.00, 1.0}; break;
        case Surface::FirstCut:   p = {0.36, 0.50, 0.280, 0.97, 0.90, 1.1}; break;
        case Surface::Rough:      p = {0.28, 0.55, 0.500, 0.90, 0.70, 1.3}; break;
        case Surface::DeepRough:  p = {0.18, 0.65, 0.900, 0.75, 0.55, 1.7}; break;
        case Surface::Sand:       p = {0.10, 0.80, 1.600, 0.82, 0.80, 1.4}; break;
        case Surface::Green:      p = {0.38, 0.50, stimpToRollResist(stimpFeet), 1.00, 1.00, 1.0}; break;
        case Surface::Fringe:     p = {0.40, 0.48, 0.090, 1.00, 0.97, 1.0}; break;
        case Surface::HardGround: p = {0.60, 0.30, 0.070, 1.00, 1.00, 1.1}; break;
        case Surface::Water:
        case Surface::OutOfBounds: p = {0.0, 1.0, 5.0, 0.0, 0.0, 1.0}; break;
    }
    double w = clampd(wetness, 0.0, 1.0);
    p.restitution *= 1.0 - 0.35 * w;
    p.rollResist *= 1.0 + 0.5 * w;
    return p;
}

Vec3 Wind::at(double h) const {
    // Logarithmic wind profile: calmer near the ground, full strength at 10 m, a little more above.
    const double z0 = 0.03;
    double f = std::log(std::max(h, 0.1) / z0) / std::log(10.0 / z0);
    f = clampd(f, 0.0, 1.35);
    double a = deg2rad(headingDeg);
    return {std::sin(a) * speed * f, std::cos(a) * speed * f, 0.0};
}

double Conditions::airDensity() const {
    double pressure = 101325.0 * std::pow(1.0 - 2.25577e-5 * altitudeM, 5.25588);
    return pressure / (287.05 * (temperatureC + 273.15));
}

Vec3 ITerrain::normal(Vec2 p) const {
    const double e = 0.25;
    double dx = (height({p.x + e, p.y}) - height({p.x - e, p.y})) / (2 * e);
    double dy = (height({p.x, p.y + e}) - height({p.x, p.y - e})) / (2 * e);
    return Vec3{-dx, -dy, 1.0}.normalized();
}

} // namespace golf

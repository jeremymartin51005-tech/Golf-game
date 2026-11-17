// GolfCore - engine-independent golf simulation. Plain C++17, no engine dependencies.
// Units: metres, seconds, kilograms, radians unless a name says Deg/Rpm.
// Axes: +X right of target line, +Y downrange, +Z up.
#pragma once
#include <algorithm>
#include <cmath>

namespace golf {

constexpr double kPi = 3.14159265358979323846;
constexpr double kGravity = 9.80665;
constexpr double kBallMass = 0.04593;
constexpr double kBallRadius = 0.021335;
constexpr double kCupRadius = 0.054;

inline double deg2rad(double d) { return d * kPi / 180.0; }
inline double rad2deg(double r) { return r * 180.0 / kPi; }
inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline double lerp(double a, double b, double t) { return a + (b - a) * t; }
inline double smoothstep(double e0, double e1, double x) {
    double t = clampd((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
inline double rpm2rad(double rpm) { return rpm * 2.0 * kPi / 60.0; }
inline double rad2rpm(double w) { return w * 60.0 / (2.0 * kPi); }

struct Vec2 {
    double x = 0, y = 0;
    Vec2() = default;
    Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    double dot(Vec2 o) const { return x * o.x + y * o.y; }
    double length() const { return std::sqrt(x * x + y * y); }
    Vec2 normalized() const { double l = length(); return l > 1e-12 ? Vec2{x / l, y / l} : Vec2{0, 0}; }
    Vec2 perp() const { return {y, -x}; } // right-hand side of direction
    Vec2 rotated(double a) const { double c = std::cos(a), s = std::sin(a); return {x * c - y * s, x * s + y * c}; }
};

struct Vec3 {
    double x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3& operator+=(Vec3 o) { x += o.x; y += o.y; z += o.z; return *this; }
    double dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(Vec3 o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalized() const { double l = length(); return l > 1e-12 ? Vec3{x / l, y / l, z / l} : Vec3{0, 0, 0}; }
    Vec2 xy() const { return {x, y}; }
};

// Distance from point p to segment ab; t receives the 0..1 parameter of the closest point.
inline double distToSegment(Vec2 p, Vec2 a, Vec2 b, double* t = nullptr) {
    Vec2 ab = b - a;
    double l2 = ab.dot(ab);
    double u = l2 > 1e-12 ? clampd((p - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
    if (t) *t = u;
    return (p - (a + ab * u)).length();
}

} // namespace golf

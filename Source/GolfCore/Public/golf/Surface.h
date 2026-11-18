#pragma once
#include "Math.h"

namespace golf {

enum class Surface { Tee, Fairway, FirstCut, Rough, DeepRough, Sand, Green, Fringe, HardGround, Water, OutOfBounds };

struct SurfaceProps {
    double restitution;   // normal bounce coefficient at moderate impact speed
    double friction;      // sliding friction during bounce
    double rollResist;    // rolling deceleration as a fraction of g
    // Effect on a shot played from this lie:
    double lieSpeed;      // ball speed multiplier
    double lieSpin;       // spin multiplier (rough produces low-spin "flyers")
    double lieDispersion; // multiplier on face/path error
};

const char* surfaceName(Surface s);

// stimpFeet sets green speed; wetness 0..1 softens bounce and slows roll on every surface.
SurfaceProps surfaceProps(Surface s, double stimpFeet = 10.5, double wetness = 0.0);

// Rolling deceleration (fraction of g) for a green of the given stimp reading.
double stimpToRollResist(double stimpFeet);

struct Wind {
    double speed = 0;        // m/s at 10 m height
    double headingDeg = 0;   // direction the wind blows TOWARD, 0 = +Y (tailwind), 90 = +X
    Vec3 at(double heightAboveGround) const;
};

struct Conditions {
    Wind wind;
    double temperatureC = 20;
    double altitudeM = 0;
    double stimpFeet = 10.5;
    double wetness = 0;      // 0 dry .. 1 soaked
    double airDensity() const;
};

// Everything the ball needs to know about the world. Implemented by generated holes,
// flat test ranges, and (in engine) the landscape adapter.
class ITerrain {
public:
    virtual ~ITerrain() = default;
    virtual double height(Vec2 p) const = 0;
    virtual Surface surface(Vec2 p) const = 0;
    virtual Vec3 normal(Vec2 p) const;
    // Height of solid obstacles (tree canopy) above ground at p; 0 = none.
    virtual double obstacleHeight(Vec2) const { return 0; }
    // Tree at p, if any: trunk centre, crown radius, total height (for trunk and canopy collisions).
    virtual bool treeAt(Vec2, Vec2* centre, double* radius, double* height) const { (void)centre; (void)radius; (void)height; return false; }
};

class FlatTerrain : public ITerrain {
public:
    explicit FlatTerrain(Surface s = Surface::Fairway, double slopeX = 0, double slopeY = 0)
        : s_(s), sx_(slopeX), sy_(slopeY) {}
    double height(Vec2 p) const override { return p.x * sx_ + p.y * sy_; }
    Surface surface(Vec2) const override { return s_; }
private:
    Surface s_;
    double sx_, sy_;
};

} // namespace golf

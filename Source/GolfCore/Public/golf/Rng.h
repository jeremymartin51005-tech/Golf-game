#pragma once
#include <cstdint>
#include <string>

namespace golf {

// Deterministic, platform-independent RNG (SplitMix64). Never use std:: distributions
// for gameplay: they differ between standard libraries and would break shared seeds.
class Rng {
public:
    explicit Rng(uint64_t seed = 1) : s_(seed) {}
    uint64_t next() {
        uint64_t z = (s_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    double uniform() { return (next() >> 11) * (1.0 / 9007199254740992.0); } // [0,1)
    double range(double lo, double hi) { return lo + (hi - lo) * uniform(); }
    int rangeInt(int lo, int hi) { return lo + int(next() % uint64_t(hi - lo + 1)); } // inclusive
    bool chance(double p) { return uniform() < p; }
    double signedUnit() { return uniform() * 2.0 - 1.0; }
    Rng fork(uint64_t salt) { return Rng(next() ^ (salt * 0xD6E8FEB86659FD93ULL)); }
private:
    uint64_t s_;
};

inline uint64_t hashString(const std::string& s) { // FNV-1a 64
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

} // namespace golf

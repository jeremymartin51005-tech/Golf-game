#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct TestCase { const char* name; std::function<void()> fn; };
inline std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
inline int& failures() { static int f = 0; return f; }
struct Registrar { Registrar(const char* n, std::function<void()> f) { registry().push_back({n, f}); } };

#define TEST(name) static void name(); static Registrar reg_##name(#name, name); static void name()
#define CHECK(cond) do { if (!(cond)) { ++failures(); std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_RANGE(v, lo, hi) do { double _v = (v); if (!(_v >= (lo) && _v <= (hi))) { ++failures(); \
    std::printf("    FAIL %s:%d  %s = %.3f not in [%.3f, %.3f]\n", __FILE__, __LINE__, #v, _v, double(lo), double(hi)); } } while (0)

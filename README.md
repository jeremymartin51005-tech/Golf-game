# Next-Generation Golf

A golf simulator for Apple Silicon Macs built on Unreal Engine 5, with a mouse-controlled swing,
procedurally generated courses and (later) a separate Chaos Mode.

**Current state: the simulation core is built and tested. There is no playable game yet.**
See [docs/STATUS.md](docs/STATUS.md) for exactly what works and what does not.

| Path | What it is | Verified? |
|---|---|---|
| `Source/GolfCore/` | Engine-independent simulation: swing analysis, impact, ball flight, putting, clubs, course generation, validation, rules, scoring, AI golfer, Daily seed, share codes | Compiled and unit-tested (37 tests) on Linux |
| `CoreHarness/` | CMake build, tests and `golfcli` tool for the core | Yes |
| `Source/NextGenGolf/`, `Config/`, `NextGenGolf.uproject` | Unreal project scaffold: raw mouse sampler, shot component, graphics presets | **No. Never compiled or opened in Unreal** |
| `docs/` | Specification, roadmap, status report | |

## Build and test the core (macOS or Linux, no Unreal needed)

```bash
cd CoreHarness
cmake -S . -B build
cmake --build build
./build/golf_tests
```

## Explore it

```bash
./build/golfcli distances              # stock carry table for every club
./build/golfcli course 42 2 2          # generate + validate a nine: seed, environment 0-5, difficulty 0-3
./build/golfcli course R9-7000-0000-0000-GGAG   # same, from a share code
./build/golfcli daily 1794787200       # the Daily Course for a Unix time
./build/golfcli bench                  # CPU cost of the simulation
```

## Open in Unreal (first time; untested)

1. Install Unreal Engine 5 (latest stable release that supports your macOS) and Xcode.
2. Right-click `NextGenGolf.uproject` > *Switch Unreal Engine version*, then generate project files.
3. Build. Expect compile errors on the first attempt: the Unreal-side code was written blind.

# Next-Generation Golf

A golf simulator with a mouse-controlled swing and procedurally generated courses.

## Play it now (Windows, Mac or Linux)

1. Download **[`Web/dist/NextGenGolf.html`](Web/dist/NextGenGolf.html)** (open it on GitHub and use the *Download raw file* button).
2. Double-click the file. It opens in your browser (Chrome or Edge recommended) and needs no install and no internet.
3. Click once in the game to capture the mouse. Then **hold the left mouse button, pull the mouse toward you, and push it away through the ball.**

| Key | Action |
|---|---|
| Mouse / `A` `D` | Aim |
| `W` `S` or wheel | Change club |
| `Q` `E` | Shot type (punch, pitch, chip, flop, bunker, stinger ...) |
| `Z` `X` | Draw / fade |
| `C` `V` | Lower / higher flight |
| `F` | Swing style (full, three-quarter, half, controlled) |
| `R` | Reset shot shape |
| `M` | Overhead map |
| `G` | Putting grid (slope-tinted) |
| `Tab` / `Esc` | Scorecard and pause menu |
| `Space` | Continue / fast-forward the ball |

Modes in this build: Random 9, Daily Course, shared course codes, Practice Range, four selectable golfers,
local statistics. See [docs/STATUS.md](docs/STATUS.md) for exactly what exists and what does not.

## What is in this repository

| Path | What it is | Verified? |
|---|---|---|
| `Source/GolfCore/` | Engine-independent simulation: swing analysis, impact, ball flight, putting, clubs, course generation and validation, rules, scoring, AI golfer, Daily seed, share codes | 45 automated tests; native and WebAssembly builds generate identical courses |
| `Web/` | Browser game: three.js renderer and UI around the core compiled to WebAssembly | Played end to end by automated browser tests (headless Chromium) |
| `CoreHarness/` | CMake build, tests and `golfcli` tool for the core | Yes |
| `Source/NextGenGolf/`, `Config/`, `NextGenGolf.uproject` | Unreal Engine 5 project for the downloadable version (Windows and Mac) | **No. Never compiled or opened in Unreal** |
| `docs/` | Specification, roadmap, status report | |

## Build from source

Core and tests (any OS with CMake and a C++17 compiler):

```bash
cmake -S CoreHarness -B CoreHarness/build
```

```bash
cmake --build CoreHarness/build
```

```bash
CoreHarness/build/golf_tests
```

Browser game (Ubuntu packages: `clang lld wasi-libc libc++-18-dev-wasm32 libc++abi-18-dev-wasm32 libclang-rt-18-dev-wasm32`):

```bash
Web/build_wasm.sh
```

```bash
python3 Web/build.py
```

## Unreal Engine version (untested)

1. Install Unreal Engine 5 (latest stable) with Visual Studio 2022 on Windows or Xcode on Mac.
2. Right-click `NextGenGolf.uproject`, choose *Generate project files*, then build.
3. Expect compile errors on the first attempt: this code was written without an Unreal toolchain.
4. Create a game mode whose Default Pawn Class is `GolfPlayerPawn`, and give `GolfHoleActor` a material
   that feeds Vertex Color into Base Color.

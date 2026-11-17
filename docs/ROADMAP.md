# Roadmap

Status key: **done** = built and verified by running it · **core** = simulation logic done and
tested, no in-engine implementation · **scaffold** = code written, never compiled or run ·
**todo** = not started.

## Phase 1 - Project foundation
| Item | Status |
|---|---|
| Inspect environment | done (Linux cloud container: no Unreal, no Mac, no GPU) |
| Verify Unreal Engine and macOS compatibility | **blocked** - needs a Mac with UE5 installed |
| Project architecture (engine-free `GolfCore` + `NextGenGolf` game module) | done |
| Create Unreal project (Windows and Mac targets) | scaffold |
| Project compiles / launches in Unreal | **blocked** |
| Mac rendering config, graphics presets | scaffold (values are unmeasured guesses) |
| Raw mouse input path | scaffold |
| Initial environment, main menu | todo |

## Phase 2 - Core golf gameplay
| Item | Status |
|---|---|
| Mouse swing analysis (power, tempo, path, face, contact, feedback text) | core |
| Classic timing swing, assisted swing | core |
| Swing *feel* (the Priority 1 gate) | **not assessed - nobody has swung it yet** |
| Swing indicator HUD, calibration screen, swing practice mode | todo |
| Ball flight, bounce, roll, surfaces, wind, temperature, altitude | core |
| Clubs (17 defined, 14-club bag) | core |
| Shot shaping and 12 shot types | core |
| Putting, cup capture, lip-outs, green read | core |
| Scoring and rules (water, out of bounds, GIR, fairways) | core |
| Cameras | todo |
| **Milestone: one playable hole that feels good** | playable in the browser build; feel not yet judged by a person |

## Phase 3 - Course experience
18-hole hand-designed course, range, short-game area, scorecard UI, weather visuals: todo.
(Weather and course conditions already affect the physics.)

## Phase 4 - Procedural golf
| Item | Status |
|---|---|
| Ten hole templates, terrain, bunkers, water, greens, tees, pins, trees | core |
| Nine-hole routing without overlaps | core |
| Validation, including an AI golfer that must finish every hole | core |
| Share codes, Daily seed and countdown | core |
| Rendering a generated hole in Unreal | todo |
| Daily leaderboard, history, streaks; saving courses | todo |
| **Gate: a human finishes a generated round** | a scripted browser session has; a human has not yet |

## Phase 5 - Chaos Mode
todo. Only the separation hook exists (`GameRules`). Order: chaos director and event budget, meteors,
lava, one dinosaur (T-Rex), power shots, the rest.

## Phase 6 - Career and progression · Phase 7 - Multiplayer and social · Phase 8 - Polish
todo.

## Browser build (added in session 2)
A complete, playable client on the same core: see STATUS.md. It is the fastest way to play and to tune
the swing while the Unreal version is brought up. Core changes are shared by both.

## Next priorities, in order
1. Human play-test of the browser build; tune swing sensitivity, tempo and difficulty from feedback.
2. On a Windows PC or Mac with UE5: open the project, fix compile errors, confirm it builds and launches.
3. Unreal: terrain material, flight and landing cameras, HUD, then parity with the browser build.
4. Chaos Mode in the core, with tests proving traditional golf is unaffected.
5. First frame-time measurements on real hardware; revise graphics presets from data.

## Known gaps and risks
See "Known bugs and weaknesses" in STATUS.md.

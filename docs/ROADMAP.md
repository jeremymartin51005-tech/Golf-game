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
| Create Unreal project | scaffold |
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
| **Milestone: one playable hole that feels good** | todo |

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
| **Gate: a human finishes a generated round** | todo |

## Phase 5 - Chaos Mode
todo. Only the separation hook exists (`GameRules`). Order: chaos director and event budget, meteors,
lava, one dinosaur (T-Rex), power shots, the rest.

## Phase 6 - Career and progression · Phase 7 - Multiplayer and social · Phase 8 - Polish
todo.

## Next priorities, in order
1. On a Mac with UE5: open the project, fix compile errors, confirm it builds and launches.
2. Render one generated hole (heightfield mesh coloured by surface) with an address camera.
3. Wire the mouse sampler to the swing, add the swing indicator, hit balls. Iterate on feel.
4. Putting, hole-out, scorecard: the Phase 2 milestone.
5. First frame-time measurements on real hardware; revise graphics presets from data.

## Known gaps and risks
- The AI golfer scores far too low (about 8 under for nine holes). Its planner removes most of the
  difficulty a human faces, so its scores say holes are *finishable*, not that they are well balanced.
- Each generated hole has its own terrain tile; neighbouring holes do not share a continuous landscape.
- Trees are simple cylinders to the physics. Wind is steady (no gusts).
- Driver carry is about 9 yards short of the tour reference; other clubs are within 5.
- Share codes carry seed, difficulty, environment, hazards, pins and tees - not custom wind, weather,
  green speed or hole composition.
- Left-handed play: only the input mirroring is tested, not shape naming or a full round.
- Unreal Engine version is not chosen; it depends on the Mac and macOS version available.

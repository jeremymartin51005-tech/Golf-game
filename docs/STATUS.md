# Status report (2026-11-17, second session)

## Summary

**The game is playable today in a web browser** on Windows, Mac or Linux: download
`Web/dist/NextGenGolf.html` and double-click it. It is a real game built on the tested simulation
core, with the mouse swing, generated courses, putting, scoring and saved statistics.

**The Unreal Engine 5 version is still unbuilt.** No machine with Unreal has been connected, so the
Unreal project has never been compiled. It now contains the glue for a first playable hole, but
expect compile errors.

Nobody human has played either version yet. Everything below marked "tested" was exercised by
automated tests, including a scripted browser session, not by a person judging how it feels.

## What is playable (browser build)

- Natural mouse swing (hold, pull back, push through), plus classic 3-click and assisted swings.
- Four swing difficulties; adjustable sensitivity, backswing length and tempo; left-handed play.
- 14-club bag, caddie club suggestion, 12 shot types, draw/fade and trajectory control, four swing styles.
- Putting with a green-reading line on the easier difficulties, lip-outs, progressive stroke power.
- Random 9 (six environments, four course difficulties, four tees), Daily Course with one official
  score per day and a countdown, shared course codes, practice range.
- Four golfers to choose from and rename, with different attributes. The Daily Course ignores
  attributes so scores are comparable.
- Water and out-of-bounds penalties, 10-stroke limit per hole, scorecard, round summary, local
  statistics (average, best, fairways, greens, putts, daily streak).
- Address, flight, landing, putting and overhead cameras; shot tracer; synthesised sound effects;
  Performance / Balanced / Quality graphics settings; frame-time display.

## What was tested, and how

| What | Result |
|---|---|
| Core unit tests | 42 of 42 pass |
| Native (gcc, x86) vs WebAssembly (clang) | 12 generated courses identical |
| Browser: real mouse events through the swing | Works (pull back, push through, strike; release early backs off) |
| Browser: classic and assisted swings, club/shot keys, pause, settings | Work |
| Browser: bad share code rejected, good code loads the same course as native | Works |
| Browser: full nine-hole Daily round to the summary screen | Completed, 38 (+2) with scripted imperfect swings; saved as the official daily score |
| Fuzzing: 3,000 garbage mouse streams, 500+ random shots on generated terrain, weakest golfers on the hardest courses | No crashes, no non-finite numbers, every round finishes |

Bugs found and fixed this session: seeds produced different courses on different compilers; the round
summary crashed; pause state leaked after quitting to the menu; capturing the mouse could be read
as a swing; tap-in putts were cancelled as waggles; chip-ins were wrongly credited as greens in
regulation; raised tees were cliff-like; left-handed path meter was mirrored.

## Measurements

CPU only, from a Linux cloud container (Intel Xeon 2.1 GHz, one thread): a full driver shot simulates in
0.73 ms; a nine-hole course generates and validates in about 0.4 s natively and about 0.5 s in
WebAssembly; the WebAssembly core is 190 KB and the whole game is one 1 MB file.

**No frame-rate figure exists.** The only renderer available here was software WebGL in headless
Chromium, which is not representative of any real computer. Turn on *Frame-time display* in
Settings to see real numbers on your machine.

## What remains unfinished

- Unreal version: never compiled; no materials, UI, audio or character assets.
- Not built at all: the three hand-designed 18-hole courses, Career, Tournament and Challenge modes,
  Chaos Mode, multiplayer, online leaderboards, course designer, replays.
- Browser build is stylised low-poly, not the "modern sports game" visual target.

## Known bugs and weaknesses

- Swing feel is unassessed by a human; default sensitivity and tempo are educated guesses.
- The AI golfer used for course validation is unrealistically good, so it proves holes are
  finishable, not that they are well balanced.
- Driver carry is about 9 yards short of the tour reference.
- Each hole has its own terrain; neighbouring holes are not visible from one another.
- Trees are cylinders to the physics; hitting one is abrupt. The tree-hit sound is not timed to the hit.
- Share codes carry seed, difficulty, environment, hazards, pins and tees only.
- Statistics live in the browser's local storage; clearing site data erases them.

## Next priorities

1. A person plays the browser build and reports how the swing feels; tune from that.
2. Connect a Windows PC or Mac with Unreal Engine 5; compile, fix and launch the Unreal project.
3. Chaos Mode foundations in the core (event director, meteors, lava) with tests, then in the browser build.

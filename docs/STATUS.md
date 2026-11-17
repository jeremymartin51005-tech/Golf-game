# Status report - end of first session (2026-11-17)

## Summary

The engine-independent simulation core is built and passes 37 automated tests. **Nothing is playable
yet**: there is no Unreal build, no rendering, no UI, and nobody has swung a mouse at it. Phase 1 is
blocked on a Mac with Unreal Engine 5; this session ran in a Linux cloud container with neither.

## Environment found

Ubuntu 24.04, x86-64, 4 cores, 15 GB RAM, no GPU, about 31 GB free disk. g++ 13, clang, CMake, Ninja.
No Unreal Engine (and no room or licence path to install it), no macOS, no attached computer.

## What was implemented (all in `Source/GolfCore`, about 2,400 lines plus 800 lines of tests)

- **Mouse swing analyser.** Streams raw mouse events through address, backswing, downswing and
  impact. Produces power, clubhead-speed factor, club path, face angle, timing error, tempo ratio,
  contact (pure/thin/fat) and plain-language feedback. Deterministic; no randomness anywhere.
  Classic three-click and assisted swings feed the same pipeline. Four difficulty presets.
- **Impact model.** Turns a swing into launch conditions per club, lie, shot type, swing style and
  intended shape. Face-to-path sets the spin-axis tilt (D-plane), so draws, fades, hooks, slices,
  pushes and pulls come out of the geometry.
- **Ball flight.** RK4 integration with drag, spin lift, spin decay, wind profile, air density from
  temperature and altitude; turf-deformation bounce model; spin-dependent check and release; rolling
  with slope; eleven surface types; wetness; tree collisions; water and out-of-bounds detection.
- **Putting.** Rolling on sloped greens calibrated to the stimpmeter definition; cup capture that
  depends on pace and how much of the hole the ball catches; lip-outs; a green-reading solver.
- **Clubs.** 17 clubs with distinct stock numbers; 14-club bag validation.
- **Course generator.** Ten hole templates, six environments, four difficulties; fairways that widen
  at landing zones; bunkers placed at dogleg corners, through the fairway and flanking greens with
  the front left open; ponds and forced carries limited by difficulty; four tees; five pin positions;
  heightfield terrain graded along the playing corridor; non-overlapping nine-hole routing.
- **Validation.** Structural checks per hole and per course, then an AI golfer plays every hole with
  the real physics and must hole out. Failing holes are redesigned, never shipped.
- **Rules and scoring.** Stroke count, putts, penalties and drops, fairways, greens in regulation,
  score names, scorecard totals.
- **Seeds.** Checksummed share codes; Daily Course seed per UTC day with countdown.
- **Unreal scaffold.** Project file, build rules, raw mouse sampler, shot component, graphics
  presets, Mac config. Written blind; see below.

## What is actually playable

Nothing by a human. From a terminal you can generate and inspect courses, watch AI golfers play
them, and print club distance tables (`CoreHarness/build/golfcli`).

## Measurements (all from this Linux container: Intel Xeon 2.1 GHz, one thread)

| What | Result |
|---|---|
| Unit tests | 37 of 37 pass, 15 s |
| Courses generated in tests | 24 of 24 valid (every environment x difficulty), plus about 20 more by hand; all on the first attempt |
| Nine-hole generate + validate | about 400 ms |
| Full driver shot on generated terrain | 0.73 ms (0.33 microseconds per 240 Hz step) |
| Swing analysis | 0.04 microseconds per mouse event |
| Same swing at 125 Hz vs 1000 Hz mouse | carry within 4 m, finish within 5 m |
| 120 Hz vs 1000 Hz physics step | driver carry within 1 m |

Stock carry against published tour launch-monitor averages (yards): driver 266 vs 275, 5-wood 232
vs 230, 5-iron 193 vs 194, 7-iron 167 vs 172, 9-iron 147 vs 148, pitching wedge 136 vs 136.

**Not measured, because it cannot be here:** frame rate, frame pacing, GPU load, memory, loading
time, input latency, anything on Apple Silicon or Metal. The CPU figures above show the simulation
is a negligible part of a frame budget; they say nothing about rendering.

## What remains unfinished

Everything outside the core: see [ROADMAP.md](ROADMAP.md). Most importantly the Unreal project has
never been compiled, and the Priority 1 gate (the swing must *feel* good) has not been attempted.

## Known bugs and weaknesses

- AI golfer is unrealistically good (roughly 8 under par for nine holes as an "expert", about level
  par as an "amateur"). It proves holes can be finished; it does not prove they are well balanced.
- Driver carries about 9 yards short of the reference.
- Unreal-side code will very likely need fixes on first compile.
- Further items are listed under "Known gaps and risks" in the roadmap.

## Next priorities

1. Get the project onto a Mac with Unreal Engine 5; compile, fix, launch.
2. Render one generated hole and hit a ball with the mouse.
3. Iterate on swing feel with a human in the loop.

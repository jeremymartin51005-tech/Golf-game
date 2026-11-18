# Status report (2026-11-18, fifth session)

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

## Cinematic look (seventh session)

New default look, "Cinematic golden hour" (Settings > Look; "Bright daylight" keeps the previous
style): low golden sun placed just off the line of play, screen-space light shafts that trees block,
a hazy sun glow, anamorphic streak and lens ghosts that fade when the sun is hidden, warm sun-tinted
haze, glittering sun path on water, and a warm, higher-contrast grade. Light shafts and flare need
Balanced or Quality graphics; Performance keeps the warm light, sky and haze.

## Graphics pass (sixth session)

Shader-drawn clouds that drift, with sunlit tops and shaded undersides (amount set by the weather),
replacing flat cloud sprites; fuller trees built from several overlapping canopy masses (tiered
pines) with baked shading; mowing stripes that swap light and dark with the viewing angle, as real
stripes do; two layered rings of distant hills for depth, with snow on alpine peaks.

## Visual and swing-UI upgrade (fifth session)

Scope was limited to four items. **No Unreal Engine was available** (no Unreal install in this
container and no computer attached), so the Unreal-side code below is written but uncompiled and
untested; everything else was built and tested in the browser game.

- **Bright, colourful course look.** Saturated palette with each surface clearly distinct; crisp
  mowing stripes down each hole and a cross-cut drawn in the terrain shader; a diagonal checkerboard
  cut and darker collar on greens; lighter first cut; brilliant sand; turquoise water; lighter haze
  in every weather; flowering shrubs at the edges of play (kept 32 m clear of the line of play) and
  flower beds framing the tee; stronger colour grade. Unreal: the same palette, fairway stripes and
  green checkerboard in the terrain vertex colours.
- **Swing gauge redesigned.** One vertical capsule replaces the old bar, path track and text: smooth
  power fill, a glowing perfect zone at the power that reaches the pin (or a full swing when it is out
  of reach; on putts, the pace for the read), a live carry/roll readout riding the fill, step-by-step
  prompts, a tempo strip with a dot, and a grade badge (PERFECT, GREAT, GOOD, EARLY, LATE, THIN, FAT,
  OVERSWING, PULLED, PUSHED). The control legend is now one line with `H` for the full list.
  Unreal: an equivalent Slate widget (`SGolfSwingMeter`) added to the viewport by the player pawn.
- **Sloped fairways (core, shared by both builds).** Each landing area has a sidehill cross-slope
  (1.8-4.2%), most holes have a crest or hollow down the fairway, rolling ground and tee-to-green
  elevation ranges were increased. The lie now affects the shot: uphill lies launch higher, downhill
  lower, ball above the feet starts left and draws (for a right-hander), below the feet right and fades.
  The shot panel shows the lie, for example "Fairway · Uphill 3% · Ball above feet 2%". In a sample of
  52 landing areas: 13 uphill, 23 downhill, 45 sidehill (over 1.5%).
- **Big Chef reworked.** Original character: natural proportions, pleated toque, double-breasted
  jacket with two rows of buttons, neckerchief, apron with waist ties, check trousers, black clogs,
  and a noticeably fuller, rounder seat silhouette. The Unreal project has no character system yet,
  so this exists only in the browser build.
- Fixed: the golfer's front and back were swapped for chest-mounted details (buttons, apron).

## Golfer, spin and graphics update (fourth session)

- **Spin control (real physics).** A strike-point control (`↑`/`↓`, shown as a ball with a red dot in
  the shot panel) changes spin rate and launch in the core impact model. Wedges have full authority,
  irons less, the driver least, and a thin strike loses most of the extra spin. Low spin releases and
  runs; maximum backspin checks, and a full wedge onto a soft green can spin back (about 6 m at the
  extreme). Two new tests cover this.
- **Articulated golfer.** Jointed body (pelvis, two-part spine, neck, head with face features, arms
  and legs with IK, shoes, gloved lead hand) driven by swing kinematics: hip and shoulder turn, spine
  tilt, wrist hinge and release, weight shift, head staying down through impact, trail heel rising
  in the finish, and a putting posture and stroke. Proportions and outfits differ per golfer. It is a
  stylised procedural figure, not a scanned or motion-captured character.
- **Graphics.** Post-processing on Balanced and Quality (HDR, bloom, filmic tone mapping, colour grade,
  vignette); five times of day (morning, midday, afternoon, sunset, evening) changing sun angle,
  colour, sky and shadows; fuller, smooth-shaded trees with baked shading; woodland belts beyond the
  boundary; a cloth flag that waves; a broadcast-style putting camera beside the golfer.
- **Swing analysis.** After each full shot the result card draws the club path and face angle
  against the target line, next to ball speed, launch, spin and apex.

## Terrain and graphics update (third session)

- **Greens are contoured ground, not decoration.** Each green has its own 0.5 m heightfield with an
  overall tilt, ridges and hollows, and on many greens a tier. Pins are only cut where the ground is
  flat enough (2-3.5% by difficulty) and are spread around the green. Ball physics, putting, the
  green-reading line and course validation all use this same ground.
- **Fairways roll.** Mounds and swales run through the playing corridor (more on links, coastal and
  alpine courses, less on parkland and desert) and a rolling ball reacts to them.
- **Shaders (browser build):** per-surface terrain detail (clumpy rough, sand grain, mown fairways,
  smoother greens), micro-relief bump shading, relief lighting that makes contours readable, animated
  water with sky reflection, sun glint and shoreline foam (ponds and the sea on coastal courses), a
  shader sky with sun glow and horizon haze, swaying grass tufts in the rough and wind-moved trees,
  weather-dependent lighting and exposure, and a vignette.
- **Putting grid** (`G`, or Settings): 1 m squares on the green, tinted white, amber or red by slope.
- A side effect of the real terrain: the AI golfers now score like real golfers (expert around
  par to a few under, amateur around par to a few over), where before the expert shot about 8 under.

## What was tested, and how

| What | Result |
|---|---|
| Core unit tests | 49 of 49 pass (new this session: lie-slope effects; uphill, downhill and sidehill landing areas) |
| Native (gcc, x86) vs WebAssembly (clang) | 12 generated courses identical |
| Browser: real pointer events at a mouse's report rate through the swing | Works (pull back, push through, strike; release early backs off). In this container the software renderer blocks the page for ~100 ms per frame, so this test skips drawing while it streams input |
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
- Browser build is stylised (low-poly trees and golfer, procedural textures), not the photoreal target the Unreal version is for. Unreal materials (the .uasset files) cannot be created without the editor, so the Unreal terrain still needs its material made there.
- Changing the terrain changed every seed: share codes and Daily Courses from before this update now produce different layouts.

## Known bugs and weaknesses

- Swing feel is unassessed by a human; default sensitivity and tempo are educated guesses.
- Driver carry is about 9 yards short of the tour reference.
- Each hole has its own terrain; neighbouring holes are not visible from one another.
- Trees are cylinders to the physics; hitting one is abrupt. The tree-hit sound is not timed to the hit.
- Share codes carry seed, difficulty, environment, hazards, pins and tees only.
- Statistics live in the browser's local storage; clearing site data erases them.

## Next priorities

1. A person plays the browser build and reports how the swing feels; tune from that.
2. Connect a Windows PC or Mac with Unreal Engine 5; compile, fix and launch the Unreal project.
3. Chaos Mode foundations in the core (event director, meteors, lava) with tests, then in the browser build.

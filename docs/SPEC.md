# Game Specification (permanent)

Condensed from the owner's master specification. Section numbers match the original so requirements
can be traced. Where this file and the original differ, the original wins.

## The three non-negotiable priorities (S1)

1. **Mouse swing is the signature mechanic.** Pull back for the backswing, push forward for the
   downswing. Backswing length, downswing speed, tempo, direction, path and timing all matter. Bad
   shots (early/late, hook, slice, push, pull, fat, thin) must have causes the player can understand;
   nothing random. Natural, Classic (timing) and Assisted schemes; adjustable sensitivity, swing
   distance, difficulty and assistance; a practice mode for learning it.
   *Gate: not complete until the swing feels genuinely enjoyable, responsive and consistent.*
2. **Procedural courses must feel professionally designed.** Hole templates, generated terrain,
   strategic bunkers and water, natural greens, landing zones, realistic lengths and pars, automated
   playability validation.
   *Gate: generate multiple unique nines, validate them, and a player finishes a full round.*
3. **MacBook performance is a design requirement.** Apple Silicon and Metal first, stable 60 FPS
   target, Performance/Balanced/Quality presets, Nanite/Lumen/VSM used only where measured to be
   worth it. Never claim a performance figure that was not measured.

## Systems

- **S2 Architecture.** UE5, C++ for performance-critical code, Blueprints where they help, custom
  ball physics, Niagara, data-driven config. Separate modules for swing, ball physics, shot types,
  putting, clubs, attributes, course management, generation, scoring, career, Chaos, power-ups,
  multiplayer, UI, audio, save/load, graphics settings.
- **S3 Swing.** Relative mouse input; calibration of backswing/downswing distance, sensitivity, speed,
  tempo, difficulty. Indicator showing power, tempo, path, contact, face, accuracy. Styles: full,
  three-quarter, half, controlled, punch, knockdown, recovery. Shapes and archetypes: straight, draw,
  fade, hook, slice, push, pull, push draw, pull fade, high, low, punch, knockdown, flop, chip,
  pitch, bunker, lob, stinger. Each changes real ball physics. Shapes available both from a menu
  and from execution, in every traditional mode.
- **S4 Ball physics.** Speed, launch, gravity, drag, lift, backspin, sidespin, wind, temperature,
  elevation, club, contact, ground friction. Outputs carry, total, apex, landing angle, curve, spin,
  rollout, bounce. Surfaces: fairway, first cut, deep rough, sand, green, fringe, hard, wet.
  Independent putting: green speed, slope, break, lip-outs, green-reading aid, optional grid.
- **S5 Clubs.** 14-club bag from driver to putter, each with distinct loft, distance, spin,
  forgiveness, launch, dispersion. Manual or recommended selection. Cosmetic/balanced equipment.
- **S6 Courses.** Three original 18-hole courses: The Highlands Championship Club, Pacific Coast
  Golf Club, Alpine Ridge Golf Club. Later: desert, woodland, parkland, links, tropical.
- **S7 Random 9.** Seeded nine-hole generator, par 34-37, four difficulties, custom environment,
  wind, weather, green speed, hazard density, pins, composition, tees. Save/share/replay seeds.
- **S8 Daily Course.** Deterministic date seed, identical for everyone; one official round,
  unlimited practice, leaderboard, history, streaks, countdown.
- **S9-S10 Chaos Mode.** A separate mode: dinosaurs (T-Rex, raptors, triceratops, pterodactyls,
  brontosaurus), meteors, volcanoes and lava, a library of further events, environmental destruction,
  four chaos levels, eight chaos environments, chaos scoring and achievements, ten power shots, a
  power-up inventory with trade-offs, chaos-only progression. **Must never alter traditional golf.**
- **S11 Modes.** Quick Play, Career, Daily Course, Random 9, Practice Range, Short Game Practice,
  Tournament, Challenge, Chaos.
- **S12 Career and customisation.** Created golfer, appearance and clothing, animations, events,
  rankings, earnings, attributes that influence but never replace skill.
- **S13 Cameras.** Address, ball flight, landing, green, overhead, free, replay; shot tracers.
- **S14 Presentation.** Grass, trees, water, sand, lighting, five times of day, six weathers,
  chaos effects; all scalable.
- **S15 UI.** Premium sports-game interface; twelve-item main menu; minimalist HUD; full scorecard;
  no dead buttons.
- **S16 Statistics and leaderboards.** Traditional and Chaos stats; local leaderboards first and
  never presented as online.
- **S17 Multiplayer.** Architecture for online 1v1, stroke/match play, tournaments, shared seeds,
  synchronised chaos events. Single-player first.
- **S18 Audio.** Event-driven golf and chaos sound, per-channel volume, spatial audio.
- **S19 Accessibility.** Beginner / Intermediate / Advanced / Simulation, plus per-setting control.
- **S20 Course designer.** Eventually: sculpt, place, validate, test, share.

## Working rules (S21-S23)

Build in phases; test the real game after each; never mark a feature done because it compiles; no
mock-ups presented as features; no dead buttons; physics never replaced by animation; Chaos never
leaks into traditional golf; document limitations honestly. Each phase report states what was
implemented, what is playable, what is unfinished, known bugs, performance measurements and next
priorities.

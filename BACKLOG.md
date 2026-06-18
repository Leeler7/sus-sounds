# Backlog — Plinko Physics Delay VST

Deferred items from the CEO review 2026-06-17. Nothing here is lost; each has enough
context to pick up cold.

## Moved out of v1

### Multiple simultaneous balls
- **What:** More than one ball dropping/looping at once for polyrhythmic textures.
- **Why deferred:** Multiplies the determinism, CPU-budget, and contact-explosion problems
  super-linearly, and 8 overlapping tap streams often sound like mud. One ball fully
  expresses the interactive-board idea. Ship one ball, add this in v1.1 once the core is
  proven and profiled.
- **Effort:** human ~1 week / CC ~half day. **Priority:** P2 (first post-v1 feature).
- **Prereq:** worst-case budget profiling from v1 must show headroom.

### Cross-machine bit-identical determinism — PROMOTED TO v1 (no longer deferred)
- Eng review 2026-06-17 found this is achievable with **Box2D v3** (cross-platform
  deterministic by design), not the research-grade fixed-point project we assumed. Now in
  v1 scope. See ARCHITECTURE.md §2. Kept here only as a record of the reversal.

### Linux support (build + determinism CI)
- **What:** A Linux VST3 build target and a Linux leg in the determinism CI matrix.
- **Why deferred:** Linux DAW usage (Reaper/Bitwig on Linux) is a rounding error in the
  VST/AU market; not a v1 ship target. Descoped from the determinism matrix 2026-06-17 to
  keep CI lean (kept Windows + macOS Intel + macOS arm64).
- **Note (2026-06-17):** the Linux render job FAILED to build in the first CI run (cause not
  yet diagnosed — likely a gcc/clang strictness nit in the spike harness, e.g. an unused
  result or a stricter warning-as-error). Diagnose if/when Linux is rescoped.
- **When worth it:** if Linux demand appears, or for a Bitwig/Reaper-Linux audience. Adding
  it back is a one-line matrix change.
- **Priority:** P3.

### CI follow-up: confirm macOS Intel determinism leg
- **What:** The macOS Intel (`macos-13`) determinism leg was still queued when T0a was
  called done 2026-06-17. Windows x64 and macOS arm64 already matched bit-for-bit, so Intel
  (same x64 arch as Windows) is low-risk, but the formal compare-job green wasn't stamped.
- **Action:** glance at the next CI run's `compare` job to confirm `macos-13` matches the
  `88bfd0f0...` hash. Auto-runs on every push; no work unless it diverges.
- **Priority:** P3 (confirmation only).

### Distribution pipeline
- **What:** Build/sign/notarize VST3 + AU, installer, release CI for Win/Mac.
- **Why deferred:** Not needed for personal use; required before any public release.
- **Priority:** P2 (before first external share).

## v2 candidates

### Modulation-out (MIDI / MPE / host modulation routing)
- **What:** Emit physics/collision events out of the plugin as MIDI/MPE notes or modulation
  signals, so the user can route them to ANY third-party reverb/delay/synth via the DAW's
  normal modulation routing.
- **Why:** Turns the board into a universal modulation source ("an instrument, not just an
  effect") without hosting anything. Works with every plugin ever made.
- **Prereq:** the internal event-stream seam (in v1 scope) must exist.
- **Effort:** human ~1-2 weeks / CC ~half day. Risk: low. DAW support for mod routing
  varies; MPE/MIDI is the most portable target.
- **Priority:** P2 (the natural v2 headline feature).

### Full nested VST hosting
- **What:** Scan, load, embed the GUI of, save the state of, and drive chosen third-party
  plugins internally (Blue Cat PatchWork / DDMF Metaplugin / Kushview Element style).
- **Why:** Self-contained "drive other VSTs" without leaving the plugin.
- **Risk / cost:** A genuinely separate, much larger product. Needs plugin scanning, GUI
  embedding, nested state save/load, latency compensation, and crash isolation (a hosted
  plugin crash must not take down your plugin or the DAW). Effort: human ~2-4 months /
  CC ~2-4 weeks.
- **Priority:** P3. Only after v1 ships and v2 mod-out proves demand. Prefer mod-out first.

## Intentionally not doing

### Faithful Python DSP prototype
- Decided against. The Python sketch (if built) proves *musical fun* only. The real
  architecture (deterministic audio-time physics, lock-free event queue, sample-accurate
  collisions) lives in the JUCE build and Python can't model it.

## Tuning follow-ups

### Cascade richness (T1 physics feel) — tune by ear when audio is wired
- **What:** The deterministic physics core (`core/`) cascades and loops correctly, but the
  ball can land near-centered on a peg and balance there until the no-stuck nudge frees it,
  making some drops slower/sparser than ideal (~17 hits/loop, ~1 loop/10s in the headless test).
- **Why deferred:** Cascade richness is a feel decision best tuned by ear with the audio
  mapping in the loop (exactly how the Python sound spike was tuned), not by twiddling
  gravity/restitution/peg-spacing against a headless pass/fail. The determinism is already
  proven and is independent of this tuning.
- **Where:** `core/PhysicsCore.h` BoardParams (gravity, restitution, peg spacing, dropX,
  initialVx, nudge). Consider: jitter peg positions slightly, vary drop point, or a small
  per-loop seeded vx for variety across loops.
- **Priority:** P2 (do alongside the audio engine, T7-ish).

## Sound engine expansions

### Multiple delay & reverb TYPES (sound character) — user request 2026-06-17
- **Why:** the v1 engine has ONE delay (clean digital feedback line) + ONE reverb (basic
  Schroeder), which sounds "digital"/metallic. Character is core to the product's appeal;
  users will want to pick the flavor. (User flagged the wet render sounding very digital.)
- **Reverb types (selectable):** current basic Schroeder → add **Room, Hall, Cathedral**
  (a size/decay/pre-delay family), **Plate**, and **Shimmer** (pitch-shifted feedback for the
  octave-up wash).
- **Delay types (selectable):** current clean **Digital** → add **Analogue/BBD** (filtered +
  gently saturated feedback so repeats darken/degrade), **Tape echo** (wow/flutter + saturation
  + filtering), **Ping-pong**.
- **Delay time / grid:** straight, **dotted** (dotted-8th = the classic rhythmic delay),
  **triplet**, and free (ms / note value). Currently fixed at an eighth note.
- **Architecture:** make Delay and Reverb pluggable modules behind a type selector (global
  Delay Type + Reverb Type macros); the per-peg delay-vs-reverb routing stays as-is. Quality
  reverb (esp. shimmer/cathedral) is non-trivial DSP — evaluate proven algorithms/libs
  (FDN reverbs, Dattorro plate, Freeverb family) rather than hand-rolling from scratch.
- **Priority:** P2. Best done once the JUCE shell + GUI exist so types are A/B-able by ear.

## GUI follow-ups

### Peg Parameter Interface (per-peg editing) — user request 2026-06-17
- **What:** A UI to select a peg and adjust its per-peg properties: **bounce** (restitution —
  there is NO separate "bumper type"; every peg just has adjustable bounciness), delay-vs-reverb
  type, and (future) per-peg pitch/pan/filter overrides. Likely: click-select a peg → a small
  inspector panel (or radial menu) with sliders, or scroll-over-peg to nudge bounce.
- **Why:** bounce is already a per-peg value in the data model (BoardParams.pegRest[]) but the
  GUI can't set it yet (new pegs all default to 0.5). This is how the player shapes a board's
  feel. (The red "bumper" coloring was removed — pegs are amber=delay/teal=reverb; bounce is a
  property, not a type.)
- **Priority:** P2, after start/stop.

## Open product questions to revisit

- **Transport-stopped behavior:** when the DAW transport stops, should the ball freeze or
  keep bouncing? Taste call; decide during UI build.
- **Free-run vs tempo-sync default:** baseline is tempo-synced with a free-run toggle.
  Confirm the default feels right in testing.
- **Accessibility:** ensure the sound is fully usable without watching the board, and pegs
  are placeable by numeric entry, not only drag.
- **Tempo-independent mode: should the delay echoes also be tempo-independent?** Surfaced by
  the T0b spike (2026-06-17). Currently independent mode keeps physics BPM-invariant but the
  multi-tap delay echoes stay tempo-synced (eighth-note spacing changes with BPM), so the
  audio isn't fully BPM-invariant. Decide: (a) keep tempo-synced echoes even in independent
  mode (physics free, delay locked), or (b) make echoes time-based in independent mode (fully
  BPM-invariant). Defaulting to (a) unless you want fully tempo-free output.

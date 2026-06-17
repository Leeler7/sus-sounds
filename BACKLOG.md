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

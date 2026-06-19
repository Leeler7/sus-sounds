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

### Delay & reverb TYPES — Phase 1 (character set) DONE on v0.2-dev 2026-06-18
- Per-bus **Delay Type** (Digital / Analogue / Tape / Ping-pong) + **Reverb Type** (Room / Hall /
  Cathedral / Plate), as ComboBoxes in each panel header; non-APVTS atomics busDelayType_/busReverbType_.
- Delay types modify the feedback path: Analogue/Tape lowpass it (darken repeats), Tape adds tanh
  saturation, Ping-pong crosses L<->R. Reverb types = (sizeScale, decayMul, HF-damp) on the Schroeder
  bank; combs allocated at max (Cathedral 1.7x) and read at the type's active length. Defaults =
  Digital / Hall (Hall ~ old reverb + light damping).
- STILL TODO (Phase 2): **Shimmer** reverb (octave-up pitch-shifted feedback — needs a pitch-shifter)
  and **per-bus delay TIME** (straight/dotted/triplet — needs a read-offset delay refactor; also
  enables Tape wow/flutter). Tape currently = LP + saturation, no wow/flutter yet.

### Multiple delay & reverb TYPES (sound character) — original request 2026-06-17 (superseded above)
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

### Board width adjustment — DONE on v0.2-dev 2026-06-18
- Width param (0.6-1.8, noon=1.0) -> physics_.setWidth() rebuilds the side walls live (ball kept);
  GUI syncs board_.width from a boardW atomic each frame (aspect + normalization). Pegs keep
  absolute x (pegs beyond a shrunk width are unreachable + clipped, non-destructive). The Master
  "Width" (pan) knob was renamed "Stereo" to disambiguate from the new Shape "Width".
- SYMMETRIC (2026-06-18): board now spans [center - w/2, center + w/2] about a fixed
  kBoardCenterX=0.5, so it expands/contracts equally both sides with existing pegs staying
  centered. Pegs keep absolute x (no data shift); coordinate mapping (walls, containment,
  pan nx, ball normalization, GUI sx/toBoardX + clamps, mirror) all offset by xMin.

### UI reorg into SECTIONS (the big GUI build) — user request 2026-06-18
Reorganize the editor into clear sections instead of one knob row:
- **Shape settings (the ball):** size, **bounce/springiness** (ball restitution — separate from
  peg bounce), and later alternate shapes.
- **Delay-peg settings (left):** the "brush" for newly placed delay pegs — bounce, size, and
  delay-specific sound params. Affects pegs being PLACED, not existing ones.
- **Reverb-peg settings (right):** same, for reverb pegs.
- **Master:** Dry/Wet, Tone, Level (and global delay/reverb mix, feedback, reverb size).
- **Notes:** needs per-peg SIZE (today pegRadius is global -> make per-peg like pegRest) and a
  ball-restitution param. Brush model: placing a peg uses the active side's settings.
- **Priority:** P2 — the next major build.

### Multi-select + right-click menu — DESIGN LOCKED 2026-06-18 (/plan-design-review)
The designer's-eye review resolved the input-model conflicts, selection feedback, undo, and
the musical operation set. Building this on `v0.2-dev`.

**Input map (rewrite — every gesture was already taken):**
- Left-click empty → add peg (unchanged). Left-click peg (no selection) → move it (unchanged).
- **Left-drag on empty → marquee select** (rubber-band rect; the only free gesture).
- **Shift+click peg → toggle** in/out of selection.
- **Left-drag a selected peg → move the whole selection** together.
- **Right-click inside a selection → context menu**; right-click/drag elsewhere → erase (kept).
- **Esc** deselect · **Delete** remove selection · **Ctrl+D** duplicate · **Ctrl+Z** undo · arrows nudge.

**Selection feedback:** white halo ring on each selected peg; live count baked into every menu
item ("Delete (12)"). Menu position clamped to stay on-board.

**v1 operation set (all chosen):**
- **Core edits:** Change type (Delay/Reverb/Flip), Grow, Shrink, Bounce +, Bounce -, Duplicate,
  Delete. Grow/shrink/bounce are RELATIVE nudges (×/± a step) so per-peg variety survives.
- **Mirror horizontal:** reflect selection across board center (x -> width-x). Because x = pan,
  this makes an instant stereo-balanced answer. Signature move.
- **Apply current brush to selection:** conform selected pegs to the active Delay/Reverb brush
  (type+bounce+size). Reuses the brush as the bulk editor.
- **Align & distribute:** snap selection to a shared row (avg Y) or column (avg X), and space
  evenly between extremes. Builds clean scale runs / arpeggios.

**Undo (Ctrl+Z) — chosen safety net:** an editor-side undo stack records each editor action
(add / move / delete / each bulk op) so one Ctrl+Z reverts a whole bulk operation, not just
deletes. Snapshots the peg arrays before each action; replays via the existing edit queue
(a full-board Reset edit is the simplest reliable apply — or add a BulkSet edit type).

**Touch points:** BoardComponent (selection set, marquee, input rewrite, juce::PopupMenu,
keyboard via keyPressed + setWantsKeyboardFocus), PluginProcessor Edit queue (bulk/undo apply),
PhysicsCore (bulk rebuild on Reset/BulkSet). Per-peg size already exists.
- **Priority:** P1 (next build).

### Live input path — DONE on v0.2-dev 2026-06-18
- Source choice param (Synth | Input). Input mode: processor writes incoming audio to a 2s mono
  ring (inRing_); each peg hit grabs the last ~grainSeconds of it (inputStart) and plays it as a
  grain through the per-peg delay/reverb. Engine forced to full-wet in input mode; processor
  crossfades the live dry signal back via the Dry/Wet macro. SoundEngine fromInput read now wraps
  (ring) + null-safe. ComboBox in the transport row.
- FOLLOW-UPS: input grains are NOT pitch-shifted (y/pitch ignored for input; only pan/bright/
  level/routing apply) -- could add resampling for pitched grains. No input-gain/trim control yet.
- LIVE MODE (reworked 2026-06-18, commit 3976fe6): Input Mode param (Granular | Live, default Live).
  Both are percussive VOICES (the gate model was scrapped). Granular = short backward snapshot
  (grainSeconds). Live = each peg = one voice reading the ring FORWARD from the strike (carries the
  ongoing notes, the A-G-C-B example), length = **Hold** knob, percussive attack envelope = impact.
  Input voices are heard DIRECTLY (engine inputMix_) + feed the bus delay/reverb, so strikes are
  prominent like the synth; the processor crossfades (throws+effects) vs the continuous loop via
  Dry/Wet. Hold knob in Master. Tone/brightness still NOT applied to input voices (pan/level/send/
  type/bus do). Pitch still not applied to input (no resampling).

### Effect buses (per-peg character) — Phase 1 DONE on v0.2-dev 2026-06-18
- 4 effect buses (kNumBuses, SoundEngine.h). Engine now has per-bus delay lines + reverbs; each
  bus has its own feedback / delayMix / reverbDecay / reverbMix. Voice/Hit carry `bus`. Per-peg
  `pegBus` in BoardParams; Collision carries `peg` index (peg shape userData now encodes INDEX, kept
  in sync on swap-remove, so type/bus are looked up from the arrays). Bus effect params are
  NON-APVTS atomics in the processor (busFeedback_[] etc.); GUI Bus selector + per-bus sliders edit
  the active bus; brush places new pegs on the active bus; right-click "Assign to bus"; colored ring
  shows bus (>0). Chosen over per-voice (cost: per-voice scales with polyphony x effect complexity +
  echo-tail voice-budget blowup; buses are fixed-cost and scale with future hall/shimmer/tape).
- PHASE 2 DONE 2026-06-18: per-peg send + level + tone (BoardParams pegSend/pegLevel/pegTone;
  stored in the bus preset per-bus-per-type; brush captures them; assign-to-bus / apply-brush stamp
  them). Send scales the wet feed to the bus in the engine (Hit/Voice.send); level multiplies the
  hit level; tone biases brightness (×(0.5+tone)), all applied in the processor. 7 sliders per
  Delay/Reverb panel now (layPanel takes a row list). STILL DEFERRED: per-bus delay TIME / effect
  TYPE selectors (-> folds into the delay/reverb TYPES task next).
- NOTE: bus effect params aren't persisted (non-APVTS); Standalone opens at defaults anyway, but a
  VST3 in a DAW won't recall them yet -> move to APVTS (or save in state) when persistence matters.

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

# Plinko Physics Delay — Architecture Spec (v1)

> The build document. Hand this to yourself (or a collaborator) when starting the JUCE project.
> Status: scope locked + hardened via CEO review + outside-voice review 2026-06-17.
> Target: VST3/AU, JUCE/C++.

## 1. Product in one sentence

A visual audio effect plugin: a single ball drops through a user-built Plinko board, and
each peg it hits becomes a delay tap whose character (pan, pitch, filter, level) is a
function of the peg's position. The rhythm is emergent from physics, tempo-locked. The
ball loops forever and can never get stuck. The board *is* the patch.

Differentiation: existing "bouncing ball delays" (Sinevibes Dispersion, Smart Electronix
Bouncy) use the bounce as a *math metaphor*. Ours is a real, interactive, screenshot-able
2D physics playfield. **Caveat from review:** interactivity alone is a UI difference, not a
sound difference. The real moat must be that boards produce tap patterns you cannot get
from a parametric model. The peg→sound mapping (Section 5) is therefore the most important
part of this document, not the physics.

## 2. Non-negotiable invariants

1. **Cross-platform reproducibility.** Same project sounds bit-identical on reload, on any
   other machine/OS/DAW, and on offline (faster-than-realtime) render. Achievable because
   **Box2D v3 is cross-platform deterministic by design** (it avoids fast-math/FMA and ships
   a portable atan2f — https://box2d.org/posts/2024/08/determinism/). Requirements we MUST
   hold to keep the guarantee:
   - **Box2D v3 only.** Chipmunk2D is eliminated (not cross-platform deterministic).
   - **No `-ffast-math`, no FMA contraction** anywhere in the physics/audio path.
   - **Portable PRNG** (self-implemented PCG/xorshift + own int→float mapping). NEVER
     `std::uniform_real_distribution` or other `std::` distributions — they are not portable
     across libstdc++/libc++/MSVC and silently break cross-machine identity (§5.1).
   - **Single-threaded or deterministic-threaded** Box2D solver.
   - A **cross-platform golden-output test** in CI (Win/Mac/Linux) guards it from silent
     regression. (Decided in eng review 2026-06-17, reversing the earlier same-binary-only call.)
2. **Offline-render parity.** A bounced/exported track sounds like what the user heard.
   This is why physics is driven by sample position, never wall-clock, and runs on the
   audio timeline (Section 3).
3. **Sample-accurate collisions.** A peg hit at sample N schedules its tap at sample N, not
   quantized to the visual frame rate.
4. **Real-time audio thread is sacred.** No allocation, no locks, no blocking in
   `processBlock`. The ball can never get stuck (guaranteed, Section 4).

### 2.1 Determinism correctness rules (eng-review outside-voice pass, 2026-06-17)
The cross-platform promise has subtle holes; these close them:
- **VERIFY before building.** Box2D v3's cross-platform determinism is engineered-for but
  unproven for our use. A 1-day spike (render one board on Win/Mac/Linux, diff bit-for-bit)
  runs FIRST, before the full build. If it fails, rethink before sinking infrastructure.
- **Presets store floats as exact bit patterns (hex floats), not decimal text.** Decimal
  round-trip can differ by an ULP across machines and change collisions. Audio is a function
  of exact peg coordinates, so this is load-bearing.
- **Substep accumulator keyed to TIME / musical position, not raw sample count.** Otherwise
  44.1k vs 48k vs 96k produce different physics. Sample-rate must not change the sound.
- **Ball in-flight state serialized as (loopIndex, samples-into-loop), not raw Box2D state.**
  On reload, replay deterministically from the seed so render-on-reopen matches.
- **Determinism scope = the COMMITTED board.** Live peg edits during playback are
  authoring-only and explicitly outside the "offline == live" guarantee (edit timing is
  wall-clock-dependent and can't reproduce). Editing a peg onto the moving ball is a
  deterministic reject/resolve, never undefined overlap/NaN.

## 3. Threading model (on-thread, hardened)

Physics runs **on the audio timeline** (required: an async worker cannot stay sample-synced
during faster-than-realtime offline render, which would break invariant #2). 2D physics
with one ball and a bounded peg set is cheap. But "cheap" is not "bounded," so it is
**hardened** against the real-time hazards the review surfaced:

- **Preallocate everything at construction:** the Box2D world, a fixed pool of bodies and
  contacts sized to the max caps. **Zero allocation in `processBlock`.** (Box2D allocates
  internally on geometry/contact churn — preallocation + fixed caps is what prevents an
  xrun.)
- **Hard caps sized by profiling**, not by hope: profile the worst case (max pegs, densest
  collision frame) and set caps so that worst case always fits the per-block budget
  (~2.6ms at 48k/128, shared with the whole project, so target a small fraction).
  Start point: **1 ball, ≤64 pegs, fixed substep count per block.** Lower if profiling says.
- **No watchdog.** A watchdog that fires *is* the glitch (bailing mid-substep corrupts
  determinism; finishing blows the deadline). Correctness comes from caps that fit, proven
  by profiling — not from runtime bail-out.

```
  ┌──────────────────────────────┐         ┌──────────────────────────────┐
  │  GUI THREAD (~60fps)          │  reads  │  shared state (lock-free)     │
  │  - draw board / pegs / ball   │◀────────│  - double-buffered physics    │
  │  - user edits pegs, drop pt,  │ snapshot│    snapshot (positions)       │
  │    gravity, ball size         │────────▶│  - atomic param block (macros)│
  └──────────────────────────────┘  writes  └───────────────┬──────────────┘
                                                             │ read params
                                                             ▼
  ┌───────────────────────────────────────────────────────────────────────┐
  │  AUDIO THREAD (real-time, AUTHORITATIVE over physics)                   │
  │  per processBlock(nSamples):                                            │
  │    advance preallocated physics in fixed substeps keyed to sample count │
  │    collision callbacks → push events to an internal event queue         │
  │    schedule delay taps at exact sample offsets from those events        │
  │    render multi-tap delay + reverb send; NO alloc, NO locks             │
  │    publish new physics snapshot to GUI (lock-free double buffer)        │
  └───────────────────────────────────────────────────────────────────────┘
```

The internal collision-event queue (audio-thread-local) is the **v2 seam**: a later
version can also drain it to MIDI/MPE/mod-out. Keeping it is justified by clean separation
now, not by paying v2 architecture tax.

## 4. Physics

- Engine: **Box2D v3** (C, embeddable, cross-platform deterministic). Not Chipmunk2D.
  Do not write a solver.
- **Preallocated body pool + lock-free command queue (the peg-edit path):** at construction,
  create MAX_PEGS Box2D bodies once. A peg edit (add/move/remove/shape-change) is encoded as
  a command pushed onto a lock-free SPSC queue; the audio thread drains it between substeps
  and enables/disables/repositions a pooled body. NEVER `b2CreateBody`/`b2DestroyBody` at
  runtime (that allocates and would break zero-allocation). The ball keeps its in-flight
  state across edits (no audible jump). (Decided in eng review 2026-06-17.)
- Fixed timestep, accumulator pattern, advanced by sample count, independent of block size.
  **Block-size independence is a tested invariant from commit 1** (offline hands you wildly
  varying block sizes; if block size leaks into the substep loop, render != live).
- Seeded RNG for any randomness so same-binary reproducibility holds.
- **Continuous collision detection (CCD)** or a velocity cap to prevent a fast ball
  tunneling through a thin peg.

### No-stuck guarantee (bias to edit-time, runtime backstop)
The runtime nudge/teleport are *audible* (they change or end the tap stream), so treat them
as sound design, not just safety:
1. **Edit-time:** detect likely trap geometry as the user places pegs (a pocket) and warn /
   visualize it, so boards rarely stick in the first place.
2. **Runtime, gentle:** small seeded nudge impulse when kinetic energy drops below a floor.
3. **Runtime backstop (rare):** hard timeout → teleport to exit + respawn. Mathematically
   guarantees no stuck state. Acknowledged audible; should almost never fire on a
   well-designed board.

### Ball lifecycle (state machine)
```
  [SPAWNED at drop point]
        │ gravity applies
        ▼
  [FALLING] ──collision──▶ [BOUNCING] ──┐ (energy → audio event)
        ▲                                │
        │<───────────────────────────────┘
        │ low-energy detected
        ▼
  [STUCK?] ──seeded nudge──▶ [FALLING]
        │ timeout exceeded (rare)
        ▼
  [TELEPORT TO EXIT]
        │ reached exit zone
        ▼
  [EXITED] ── respawn ──▶ [SPAWNED]
```

## 5. The peg → sound mapping (THE product — concrete, opinionated)

One **pure function** `pegToTap(normPos, collisionEnergy, globals) → TapParams`, used by
BOTH the audio engine and the GUI preview. Never duplicate it. Board coords normalized
0..1: x = 0 (left) .. 1 (right); y = 0 (top, near drop) .. 1 (bottom, near exit).

| Output | Driven by | Curve / rationale |
|---|---|---|
| **Tap time** | the *actual sample-accurate collision time* | The rhythm is EMERGENT from physics, not a per-peg knob. The loop period (drop→exit→restart) snaps to N bars, so emergent rhythms stay tempo-locked. This is the core idea: you don't dial a rhythm, you build a board that produces one. |
| **Pan** | peg x | Linear x→pan, hard-left at 0, hard-right at 1, scaled by a global Pan Width. Echoes sweep across the field as the ball traverses (matches Dispersion's stereo motion). |
| **Pitch** | peg y | y→pitch, **quantized to a user-selected key/scale** (not continuous, or it sounds atonal). Top = higher, bottom = lower, over a global Pitch Range (±12/±24 semitones). |
| **Filter** | collision energy | Harder hit = brighter (lowpass cutoff rises with energy). Physically intuitive: loud bounces are bright. Global Filter Amount scales the effect. |
| **Level** | collision energy | Harder bounce = louder tap. The ball naturally loses energy each bounce → an emergent diminuendo series of echoes. |

### 5.1 Randomness discipline (cross-platform determinism)
The only randomness is the no-stuck nudge. Rules (eng review 2026-06-17):
- One **portable, self-implemented PRNG** (PCG or xorshift) with your own int→float mapping.
- **NEVER** `std::uniform_real_distribution` / `std::*_distribution` — not portable across
  standard libraries; would silently break cross-machine identity.
- Seed stored in the preset. **Reseed deterministically from (boardSeed, loopIndex)** at each
  loop restart; consume the stream ONLY inside the sample-driven physics step. No mid-stream
  RNG state to serialize.

**Per-peg bounce / bumper (in the core as of T1):** each peg also carries its own
restitution. > 1.0 makes a pinball-style **bumper** that returns EXTRA energy — fun, and it
doubles as a liveliness tool (counteracts energy loss / the ball resting on a peg). Map a
bumper hit to an *accented* tap (louder + brighter) so the bumper is heard, not just seen.
Cap around 1.6 so the ball can't gain runaway energy. (User idea, 2026-06-17.)

**Global controls (these are the automatable macro params, Section 6):** Key/Scale,
Pitch Range, Pan Width, Filter Amount, Feedback, Reverb Send, Dry/Wet, Loop Length (bars),
Gravity, Ball Size, Drop-Point X, **Sync Mode**.

**Sync Mode (user-selectable, all deterministic — eng review + spike, 2026-06-17):**
- *Tempo-locked:* physics time is scaled so the loop fits N bars (validated by the sound
  spike). Same relative pattern at any tempo; gravity/velocity scale with tempo, so a
  board's feel is tempo-relative.
- *Physics-free (DEFAULT):* fixed real-world gravity; physics ignores host tempo, but the
  delay echoes still snap to the host grid. The usual "delay syncs to the project" behavior.
- *Self-clocking:* the board free-runs and **derives its own tempo from the ball's loop
  period** (drop → exit → restart = T seconds; declare loop = N bars ⇒ implied
  BPM = N·4·60/T). The plugin becomes a tempo *source*: echo spacing and groove come from
  the physics, not the host. Editing gravity/board changes the derived tempo (a feature in
  this mode). Notes: (1) the period is known only after one full run, so echo timing locks
  from loop 2 (bootstrap); (2) most plugin formats can't write host tempo, so this drives
  the plugin's internal timing and free-runs against the host — offer "snap derived tempo to
  nearest whole BPM" for alignment. Derived tempo is a pure function of params+seed, so it
  stays deterministic.

All three are pure functions of params + seed, so all keep the determinism guarantee.

**Tap gain-staging:** the active-tap cap (§7) bounds count; tap levels must also be managed
(headroom/soft-limit) so dense bounces don't sum into hard clipping.

**Musical thesis to validate first:** the producer composes melody+rhythm *in space* — x
spreads echoes across stereo, y picks scale degrees, physics supplies organic-but-locked
timing, and the ball's energy decay shapes a natural echo tail. If this doesn't sound
distinct from a parametric bouncing-ball delay, the product has no moat — test this mapping
as early as possible (a quick spike is cheap insurance even though we deferred a full
prototype).

### 5.2 Sound engine model (AGREED 2026-06-17 — build against this)

**Source: HYBRID.** Two layers, mixable:
- *Input path:* incoming audio is the thing delayed/reverbed (true effect). Does nothing on
  silence.
- *Built-in exciter:* a short enveloped tone the ball "pings" so the plugin plays standalone
  (like the Python spike). Pitch from the peg (scale-quantized, §5). Use when input is silent,
  or blend with input via an Exciter Mix macro.

**A peg hit = a discrete transient EVENT, FAST ATTACK, scheduled at the exact collision
sample.** Never a parameter ramp. Fast attack is the tactile magic — the sound is clearly
caused by the bounce. Events are additive: many hits = denser texture, but each stays crisp,
so the ball's path reads as a rhythm.

**Per-peg TYPE routes the event** (a per-peg property, board state §6 — default delay; user
can also blend):
- *Delay peg:* spawns a discrete **echo** (of input and/or exciter). Level = impact energy,
  pan = peg x, pitch = peg y (scale-quantized), filter = energy. Decay = the global Feedback
  (a few fading repeats, ~0.5–3 s). This is the RHYTHMIC layer — short, locatable.
- *Reverb peg:* injects a **splash** into the reverb send. Fast attack, LONG decay = the
  reverb tail (~1–4 s). Size = impact energy. This is the ATMOSPHERIC layer — and it is
  exactly the "effect swells then decays over time" behavior, localized to reverb pegs.

**Energy and bumpers:** impact energy sets event size (louder echo / bigger splash). A bumper
hit (pegRest > 1, §5) is an **accent** — louder + brighter.

**Tempo:** event timing follows the chosen Sync Mode (tempo-locked / physics-free /
self-clocking, §5).

So: every hit is fast-attack; delay pegs decay fast (rhythm), reverb pegs decay long (bloom);
it's discrete additive events, not knobs ramping.

**Delay/reverb are currently single minimal algorithms** (clean digital feedback delay +
basic Schroeder reverb) and sound "digital" by design — placeholders. Build the delay and
reverb as **pluggable modules behind a type selector**; selectable character types (reverb:
room/hall/cathedral/plate/shimmer; delay: digital/analogue/tape/ping-pong; timing:
straight/dotted/triplet) are a planned expansion — see BACKLOG "Sound engine expansions".

## 6. Plugin state vs automatable parameters (review gap — resolved)

A 128-peg board × 5 properties would be ~640 parameters; the VST3 parameter model and most
DAWs choke on that. So:

- **Board geometry is plugin STATE, not parameters.** Peg positions/shapes/count, ball
  params, drop point → serialized as a `ValueTree`/JSON blob saved with the project and in
  presets. Not automatable (it's structural, like a sequencer pattern).
- **Expose a small fixed set (~12-16) of automatable macro parameters** — the global
  controls in Section 5. These are the things worth automating and they fit the VST3 model
  cleanly.
- This split must be decided before building the editor (it shapes the whole save/load and
  undo design).

## 7. Failure registry (all MUST-handle)

| Codepath | Failure | Action | User sees | Logged |
|---|---|---|---|---|
| physics step | NaN/Inf blowup | reset sim to last good seeded state | brief silence, respawn | once |
| physics step | tunneling | CCD / velocity cap | nothing (correct) | no |
| physics step | stuck ball | edit-time warn + nudge + timeout teleport | nothing / rare teleport | once |
| collision | event burst | bounded event queue, fixed caps | density limit | once |
| delay write | delay > buffer | clamp to max delay | tap at max | no |
| reverb tail | denormals | FTZ/DAZ | nothing | no |
| param layer | gravity=0 / size<=0 | clamp at param layer | knob limited | no |
| preset load | corrupt/old preset | reject, load default, notify | toast + default | yes |
| host | sample-rate change | rebuild buffers, reseed physics clock | seamless | no |
| host | block-size change | accumulator independent of block size | seamless | no |

No catch-all error handling. Each path is named and specific. The two silent killers if
missed: **denormals** (CPU storm on silence) and **NaN propagation** (permanent DC/silence).

## 8. Latency / PDC (review gap — flagged)

- Per-tap **pitch shifting introduces latency**. Decide the pitch-shift method (delay-line
  resampling vs FFT) and report correct latency to the host via `setLatencySamples`, or the
  plugin will sit out of time in the mix.
- Tempo-sync + sample-rate changes interact with the sample-keyed physics clock — re-derive
  loop length in samples on any SR or BPM change.

## 9. Presets

- Format: JSON or JUCE `ValueTree`/XML. **Never** eval/pickle-style deserialization.
- **Version field from v1.** Every future version must load old presets or degrade cleanly.
- Validate and clamp every field on load. Round-trip test: save→load→save → identical.

## 10. Testing strategy

| Target | Type | Key test |
|---|---|---|
| same-binary reproducibility | unit | same seed+params → identical collision sequence (golden file) |
| **cross-platform reproducibility** | CI matrix | render board on Win/Mac/Linux → bit-identical output |
| offline-render parity | integration | render twice (varying block sizes) → identical output |
| block-size independence | unit | same audio at 64/128/512 block sizes |
| **peg edit: zero allocation** | unit | edit during playback → audio-thread allocation detector stays at 0 |
| **peg edit: continuity** | unit | edit mid-flight → ball keeps velocity/position, no jump |
| **command queue** | unit | concurrent edit + audio drain → no torn read, FIFO order |
| **portable RNG** | unit | PRNG sequence bit-identical across platforms; reseed-per-loop reproduces |
| collision→tap timing | unit | hit at sample N → tap at sample N |
| stuck recovery | unit | construct a pocket → ball exits within T |
| tunneling | unit | max-velocity ball vs thin peg → collision registers |
| denormals | unit | feed silence → CPU flat, output flushes to zero |
| budget under load | perf | max pegs + densest frame → worst-case block time < target |
| param clamps | unit | gravity=0, size=-1 → clamped |
| preset round-trip | unit | save→load→save identical |
| host contract | system | JUCE `pluginval` strictest; load in Reaper/Ableton/FL |

- **2am-Friday test:** offline-render parity.
- **Hostile-QA test:** max gravity + huge ball + 64 dense pegs + automated macros, bounce at
  8x → no NaN, no dropout, identical on repeat.
- **Chaos test:** randomize all macros every block for 10 min → no crash, no non-finite.

## 11. Observability (dev-time)

- Debug overlay: ball velocity/energy, collisions/sec, active tap count, per-block physics
  time (verify it stays under budget live).
- Deterministic replay: log seed + macro timeline so any "sounded weird" report reproduces
  exactly on the same binary.
- Debug-build assertions fire once per failure class.

## 12. v1 scope vs deferred

**In v1:** ONE ball, draggable pegs + walls, extra peg/ball shapes, gravity/ball-size/
drop-point, tempo-synced multi-tap delay, reverb send, per-peg pitch+filter (Section 5),
savable presets (versioned), debug overlay + deterministic replay, the internal
event-queue seam.

**In v1 (upgraded by eng review):** cross-platform deterministic identity — now achievable
via Box2D v3, no longer deferred. Shared boards sound identical on every machine.

**Deferred (see BACKLOG.md):**
- **Multiple simultaneous balls** (moved out of v1 — multiplies determinism/CPU cost
  super-linearly; one ball fully expresses the idea).
- Modulation-out (MIDI/MPE/host routing) — v2, via the event queue.
- Full nested VST hosting — v2/v3, separate product.
- **Distribution pipeline** (build/sign/notarize/installer/release CI) — post-v1, but
  required before any public release.

## 13. Reversibility notes

- Same-binary determinism + on-thread physics: ~one-way door. Locked + hardened here.
- Framework (JUCE): one-way door. Locked.
- Peg→sound mapping (Section 5): easily reversible — iterate freely, this is where taste lives.

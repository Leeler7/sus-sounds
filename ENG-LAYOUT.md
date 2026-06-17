# Engineering Layout — Plinko Physics Delay VST (v1)

> The one-page map of files, classes, responsibilities, and thread boundaries.
> Input for `/plan-eng-review`. Derived from ARCHITECTURE.md (section refs in parens).
> Status: draft for review 2026-06-17. JUCE/C++, VST3/AU.

## 1. File / module tree

```
PlinkoDelay/
  CMakeLists.txt              JUCE CMake project (juce_add_plugin)
  Source/
    PluginProcessor.{h,cpp}   audio-thread entry; owns engine; processBlock      (§3)
    PluginEditor.{h,cpp}      plugin window; hosts BoardView + MacroPanel
    Params.h                  macro parameter IDs, ranges, APVTS layout           (§6)

    physics/
      PhysicsWorld.{h,cpp}    preallocated Box2D v3 world; body pool; fixed substep (§3,§4)
      Board.{h,cpp}           peg/wall/ball geometry + edit-time validation       (§4,§6)
      CommandQueue.h          lock-free SPSC queue: GUI peg-edits -> audio thread  (§4)
      Rng.h                   portable PCG/xorshift; NO std:: distributions       (§5.1)
      Snapshot.h              POD struct of positions for the GUI (double-buffer) (§3)

    audio/
      EventQueue.h            lock-free SPSC collision-event ring (the v2 seam)   (§3)
      CollisionEvent.h        POD: sampleOffset, normX, normY, energy            (§3,§5)
      PegToTap.h              pure §5 mapping: event -> TapParams (shared)         (§5)
      DelayEngine.{h,cpp}     tempo-synced multi-tap delay + reverb send          (§5,§8)
      Voices.h                preallocated active-tap pool + voice-steal          (§3,§7)

    state/
      StateManager.{h,cpp}    ValueTree board state + presets (versioned)         (§6,§9)
      Macros.h                the ~12-16 automatable macro definitions            (§5,§6)

    gui/
      BoardView.{h,cpp}       draw board; drag-place pegs; read Snapshot
      MacroPanel.{h,cpp}      knobs bound to APVTS macros
      DebugOverlay.{h,cpp}    dev-only: velocity, hits/sec, per-block time         (§11)
  Tests/
    determinism_test.cpp      golden-file + offline-parity + block-size invariance (§10)
    xplatform_golden_test     CI matrix (Win/Mac/Linux) bit-identical render        (§10)
    safety_test.cpp           NaN, tunneling, stuck, denormals, tap overflow        (§7,§10)
    edit_safety_test.cpp      peg-edit zero-allocation + continuity + queue order   (§4,§10)
    rng_test.cpp              portable PRNG cross-platform + reseed reproducibility  (§5.1)
    profiling_harness         worst-case per-block time at max caps (gates the caps) (§3)
    alloc_detector.h          fails any test whose processBlock allocates           (§3)
```

## 2. Who owns what (responsibilities)

| Class | Thread | Owns / does | Must NOT |
|---|---|---|---|
| `PluginProcessor` | audio | drives the per-block loop; holds `PhysicsWorld`, `DelayEngine`, APVTS; reports latency (§8) | allocate or lock in `processBlock` |
| `PhysicsWorld` | audio | advances preallocated Box2D in fixed substeps keyed to sample count; emits `CollisionEvent`s into the queue; publishes `Snapshot` | grow geometry at runtime; touch wall-clock |
| `Board` | gui (edit) / audio (read) | the peg/wall/ball geometry; edit-time trap detection (§4) | be the audio-thread source of truth (the audio thread reads an immutable copy) |
| `EventQueue` | audio (prod+cons) | lock-free SPSC ring of collision events; also the v2 mod-out tap point | dynamic-allocate |
| `PegToTap` (pure fn) | both | map (normX, normY, energy, macros) -> TapParams; single source of truth | hold state |
| `DelayEngine` | audio | schedule taps at sample offsets; multi-tap delay; reverb send; FTZ/DAZ (§7) | block |
| `Voices` | audio | preallocated active-tap pool; voice-steal quietest on overflow (§7) | allocate |
| `StateManager` | gui / message | ValueTree board state, preset save/load with version field, validation (§9) | run on audio thread |
| `BoardView` | gui | render board; drag-to-place/move pegs; read latest `Snapshot` | mutate audio-thread state directly |
| `MacroPanel` | gui | knobs bound to APVTS macro params | — |

## 3. Thread boundaries (the contract)

```
  GUI THREAD                          AUDIO THREAD (authoritative)
  ----------                          ----------------------------
  BoardView  ──edit──▶ Board (gui copy)
        │                                  PhysicsWorld.step(nSamples)
        │  on commit: push immutable          │ emits CollisionEvent
        │  board geometry  ───────────────▶  (consumed same thread)
        ▼                                       │
  MacroPanel ─▶ APVTS (atomic) ─────────────▶ read macros
                                                │
        ▲                                  PegToTap() ─▶ DelayEngine + Voices
        │  read for drawing                      │
  Snapshot ◀── lock-free double buffer ◀──── publish positions
```

Only three things cross the boundary:
1. **APVTS macro params** (atomic) — GUI writes, audio reads.
2. **Board geometry** — GUI commits an immutable snapshot the audio thread swaps in
   between blocks (lock-free pointer swap). Peg drags don't mutate live audio state.
3. **Physics `Snapshot`** — audio writes a double-buffer, GUI reads to draw. GUI may lag.

No other shared mutable state. No locks on the audio thread.

## 4. Key data types (define these first)

```cpp
struct CollisionEvent {        // audio/CollisionEvent.h
    int   sampleOffset;        // exact offset within the current block (§3)
    float normX, normY;        // 0..1 board position
    float energy;              // impact speed along normal
};

struct TapParams {             // output of PegToTap (§5)
    float freqOrPitch;         // scale-quantized (see §5 mapping)
    float panL, panR;          // equal-power
    float level;
    float brightness;          // -> filter cutoff
};

struct Snapshot {              // physics/Snapshot.h  (GUI draw only)
    float ballX, ballY;
    // peg positions live in Board's immutable copy; Snapshot is just dynamic state
};
```

Board geometry (pegs, walls, ball, drop point) is serialized by `StateManager` as a
`ValueTree` with a top-level `version` int. Macro params live in an `AudioProcessor
ValueTreeState` (APVTS), separate from board geometry (§6).

## 5. Build order (dependency-first, each step testable)

0. **Phase 0 — de-risk spikes FIRST (eng review 2026-06-17).** Before any infrastructure:
   - **Cross-platform determinism spike:** minimal Box2D v3 sim, render one board on
     Win/Mac/Linux, diff bit-for-bit. If it fails, rethink the determinism promise before
     building on it. (This is the riskiest unverified assumption in the whole plan.)
   - **Tempo-portability spike:** extend the existing Python sound spike to confirm a board
     stays musical across tempos (90 vs 140 BPM) in both Sync Modes.
   Only proceed if both pass.
1. **Skeleton** — `juce_add_plugin` CMake, empty `PluginProcessor`/`PluginEditor`, passes
   `pluginval`.
2. **PhysicsWorld + CollisionEvent + EventQueue** — preallocated, fixed substep, seeded;
   no audio yet. Test: golden-file determinism + block-size invariance (§10). *This is the
   riskiest piece; prove it before building on it.*
3. **PegToTap + DelayEngine + Voices** — turn events into taps; multi-tap delay; reverb
   send; FTZ/DAZ. Test: collision-at-sample-N -> tap-at-N; tap overflow; denormals (§10).
4. **StateManager + Params/Macros** — APVTS macros, ValueTree board state, versioned preset
   save/load. Test: preset round-trip; old-version load (§9).
5. **BoardView + MacroPanel** — draw from Snapshot; drag-place pegs commit immutable board;
   knobs to APVTS. *Decide the §6 state-vs-params boundary before writing this.*
6. **DebugOverlay + deterministic replay logging** (§11).
7. **Offline-render parity test** end-to-end (§10), then load in Reaper/Ableton/FL.

## 6. Decisions settled by `/plan-eng-review` (2026-06-17)

- **RESOLVED — Determinism:** cross-platform (Box2D v3), reversing the earlier same-binary
  call. Chipmunk dropped. No -ffast-math/FMA; portable PRNG; cross-platform golden CI test.
- **RESOLVED — Peg-edit path:** preallocated body pool + lock-free SPSC command queue drained
  by the audio thread. No runtime b2CreateBody/b2DestroyBody. Ball keeps in-flight state.
- **RESOLVED — RNG:** portable self-implemented PCG/xorshift, reseed per loop from
  (boardSeed, loopIndex); never std:: distributions.
- **RESOLVED — State-vs-params:** board geometry = ValueTree (non-automatable);
  ~12-16 macros = APVTS. Settle the editor UX against this before step 5.
- **STILL OPEN — Pitch-shift method + reported latency (§8):** delay-line resampling vs FFT;
  affects PDC. Decide during audio-engine build (step 3).
- **STILL OPEN — Caps by profiling:** the profiling harness (added as a P1 task) must confirm
  1 ball / ≤64 pegs / fixed substeps fits the per-block budget. Lower caps if not. No watchdog.

## 7. Implementation Tasks (from eng review)

- [~] **T0a (P0, SPIKE FIRST)** — cross-platform determinism spike. Harness BUILT + RUN on Windows (CMake 4.3.3 + MSVC, Box2D v3.1.1). SAME-BINARY determinism CONFIRMED: 4 runs identical. Output is raw IEEE-754 bit patterns (not %a, which drifts across platforms). Windows reference SHA256 `88BFD0F0...E537D48C`. REMAINING (cross-platform half): run on macOS + Linux and diff. CI workflow `.github/workflows/determinism.yml` does this automatically across ubuntu/windows/macOS-x64/macOS-arm64 once the repo is on GitHub.
- [x] **T0b (P0, SPIKE FIRST)** — tempo-portability spike RAN (`spike/spike_tempo.py`). Result: locked mode time-scales the pattern (90vs140 corr -0.14); independent mode keeps physics BPM-invariant but echoes stay tempo-synced (corr 0.78). Musicality verdict: user listening. Surfaced design Q below.
- [ ] **T1 (P1)** physics/ — Box2D v3 deterministic sim: fixed substep keyed to TIME/musical position (not raw sample count, for sample-rate independence), no -ffast-math/FMA. Verify: golden-file + block-size-invariance + 44.1/48/96k same-output tests.
- [ ] **T1b (P1)** state/ — presets store floats as exact bit patterns (hex floats); ball state serialized as (loopIndex, samples-into-loop). Verify: cross-machine preset reload bit-identical.
- [ ] **T1c (P1)** physics/ — transport-discontinuity model: define ball behavior on loop jump / locate / pre-roll; peg-onto-ball deterministic resolve. Verify: loop-jump + spawn-on-ball tests.
- [ ] **T2 (P1)** physics/ — preallocated body pool + lock-free SPSC command queue for peg edits. Verify: edit_safety_test (zero-alloc + continuity + queue order).
- [ ] **T3 (P1)** physics/ — portable PCG/xorshift RNG, reseed per loop; no std:: distributions. Verify: rng_test cross-platform + reseed.
- [ ] **T4 (P1)** Tests/ — cross-platform golden CI matrix (Win/Mac/Linux bit-identical). Verify: CI green on all three.
- [ ] **T5 (P1)** Tests/ — profiling harness at max caps; sets/validates the caps. Verify: worst-case per-block time under budget.
- [ ] **T6 (P1)** Tests/ — audio-thread allocation detector wired into the test suite. Verify: any allocating processBlock fails.
- [ ] **T7 (P2)** audio/ — PegToTap pure fn + multi-tap DelayEngine + reverb send + FTZ/DAZ. Verify: collision-at-N→tap-at-N, denormals flat-CPU.
- [ ] **T8 (P2)** state/ — ValueTree board state + APVTS macros + versioned presets. Verify: round-trip + old-version load.
- [ ] **T9 (P2)** audio/ — decide pitch-shift method, report latency to host (PDC). Verify: pluginval latency check.
- [ ] **T10 (P3)** gui/ — BoardView (drag pegs, read Snapshot) + MacroPanel + DebugOverlay.

## GSTACK REVIEW REPORT

| Review | Trigger | Why | Runs | Status | Findings |
|--------|---------|-----|------|--------|----------|
| CEO Review | `/plan-ceo-review` | Scope & strategy | 1 | clean | 6 decisions, scope locked, 1 ball |
| Outside Voice | Claude subagent | Independent 2nd opinion | 1 | issues_found | 3 tensions + 6 folded fixes |
| Eng Review | `/plan-eng-review` | Architecture & tests (required) | 1 | clean | 3 issues resolved + outside voice absorbed, 0 critical gaps |
| Design Review | `/plan-design-review` | UI/UX gaps | 0 | — | — |
| DX Review | `/plan-devex-review` | Developer experience gaps | 0 | — | — |

- **CROSS-MODEL:** outside voice (Claude subagent) caught real gaps the section review missed: build-order risk (spike cross-platform determinism + tempo-portability FIRST), tempo-lock semantics (resolved as a user-selectable Sync Mode), offline-vs-live-edit scope (determinism = committed board), plus 6 folded correctness fixes (hex-float presets, time-keyed substep for sample-rate independence, ball-state serialization, transport-discontinuity model, integration budget, tap gain-staging). All presented to the user; all resolved.
- **VERDICT:** ENG CLEARED (architecture, code quality, tests, performance + outside voice; all findings resolved). CEO CLEARED (prior session). Ready to implement, starting with the Phase 0 spikes (T0a/T0b). Note: gstack review-log/dashboard could not run in this shell-less environment, so status is recorded here in the plan file only.

NO UNRESOLVED DECISIONS
```

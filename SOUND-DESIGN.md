# Sound design — source of truth

The peg→sound philosophy, captured from a /design-consultation on percussive impact
(2026-06-18/19). This guides future sound work the way DESIGN.md guides visual work.

## The memorable thing
**When a peg is hit, you should KNOW it was hit.** Each strike is a discrete, percussive
event with impact — not a swell that gets lost in a wash. The board is an *instrument being
struck*, even when the source is sustained audio.

## Core principle: percussion = transient
A hit reads as "percussive" because of its **transient** — a sharp energy spike at onset
(the click of a pick, the tok of a mallet). 
- The **synth/exciter** source already has this (fast-attack tone from silence).
- The **input/WAV** source does NOT: grabbing a slice of a *sustained* chord and fading it in
  is a swell, no transient. More level just makes the wash louder.

## Decisions
1. **Impact must come from the input itself — no synthetic layers.** This is an EFFECT, not a
   synth. So we manufacture the transient by *shaping* the input, not by adding clicks/mallets.
   (User call: rejected tuned-pluck/click/drum layers in favor of input-derived shaping.)
2. **Transient designer ("Impact" knob):** each input throw punches in then settles to a
   sustain level over ~45 ms. Impact 0 = smooth swell (old), 1 = pure percussive stab, mid =
   punch + sustained body. Implemented in SoundEngine voice (tEnv/tMul, fromInput only).
3. **Throws are heard directly + echoed** (engine inputMix_), so strikes are present like the
   synth, not faint echoes. Hold = how much phrase rides under the punch.
4. **Live vs Granular:** Live = forward-from-strike Hold-length throw (carries the phrase);
   Granular = short backward snapshot (glitch).

## Open / future sound ideas
- If amplitude-only punch isn't enough: add an input-derived **HF/attack emphasis** (high-passed
  onset, or a transient-band boost) for more "click" — still from the input, still an effect.
- Per-peg tone/brightness on input throws (currently not applied).
- Pitched input throws (resample so peg-y transposes the source).
- Filter-envelope "pluck" on the throw (bright->dark snap from the input's own spectrum).

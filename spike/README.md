# Sound spike — does a Plinko board make musical patterns?

Throwaway test of the one hypothesis that decides whether this product has a moat:
**do physics-built boards produce distinct, musical patterns you couldn't easily dial in
on a parametric bouncing-ball delay?** Not the VST architecture — just the §5 mapping.

## Run
```
python spike_plinko.py
```
Outputs 6 stereo WAVs to `./out/` (3 boards × 2 scales).

## How to listen (the actual experiment)
1. Play `board_1_*`, `board_7_*`, `board_42_*`. Do the three boards sound like **three
   different riffs**, or samey? Different = good (the board is doing real work).
2. Play `board_1_minor_pent` vs `board_1_major`. Same rhythm, different pitch set?
   That confirms the mapping: physics owns timing, the scale owns the notes.
3. Gut check vs a normal delay: could you get *these specific* evolving patterns by
   turning knobs on a regular delay? If no, the interactive board earns its place.

## If it sounds boring/samey
The mapping is wrong, not the idea. Tweak `peg_to_tap()` and `make_grain()` in
`spike_plinko.py` and re-run. Things to try: map energy→pan-spread, y→filter instead of
pitch, add velocity→delay-feedback, change ROOT_HZ/scales, vary grain timbre.

## What this spike deliberately is NOT
- Not deterministic-on-the-audio-thread, not sample-accurate, not real-time. The real
  build (JUCE/C++) handles all that — see `../ARCHITECTURE.md`.
- Physics is hand-rolled and rough on purpose. It only needs to emit a plausible
  (time, x, y, energy) collision stream to test the sound.

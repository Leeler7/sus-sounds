"""
spike_tempo.py -- T0b: tempo-portability spike.

Question: does a board stay musical across tempos, in BOTH Sync Modes?
  - Tempo-locked:      physics time is scaled so the loop fits N bars. Same board
                       at 90 vs 140 BPM = same relative pattern, different speed.
  - Tempo-independent: fixed real physics time; the loop just snaps to the nearest
                       bar. The audio is BPM-invariant (same at any tempo).

Reuses the physics + mapping from spike_plinko.py (run from this folder).
Out: ./out/tempo/*.wav
"""

import os
import math
import numpy as np
from spike_plinko import (
    build_board, simulate, SCALES, peg_to_tap, make_grain, add_reverb,
    write_wav, SR, GRAIN_LEN,
)


def synth_mode(collisions, exit_t, scale, bpm, sync_mode,
               loop_bars=2, echoes=3, feedback=0.5, reverb=0.18):
    beat = 60.0 / bpm
    if sync_mode == "locked":
        # scale physics time so the loop fits loop_bars bars
        loop_len = loop_bars * 4 * beat
        scale_factor = loop_len / max(exit_t, 1e-3)
    else:  # "independent": real physics time, snap loop to nearest bar
        bar = 4 * beat
        scale_factor = 1.0
        loop_len = math.ceil(max(exit_t, 1e-3) / bar) * bar

    echo_step = beat / 2.0
    n_loops = 4
    n = int((loop_len * n_loops + 1.0) * SR) + GRAIN_LEN + 1
    buf = np.zeros((n, 2))
    emax = max((c[3] for c in collisions), default=1.0)

    for loop_i in range(n_loops):
        loop_t0 = loop_i * loop_len
        for (t, nx, ny, energy) in collisions:
            freq, pan, level, brightness = peg_to_tap(nx, ny, energy, emax, scale)
            grain = make_grain(freq, brightness)
            lg = math.cos(pan * math.pi / 2) * level
            rg = math.sin(pan * math.pi / 2) * level
            t_loop = loop_t0 + t * scale_factor
            for rep in range(echoes):
                g = feedback ** rep
                start = int((t_loop + rep * echo_step) * SR)
                if start + GRAIN_LEN >= n:
                    break
                buf[start:start + GRAIN_LEN, 0] += grain * lg * g
                buf[start:start + GRAIN_LEN, 1] += grain * rg * g

    if reverb > 0:
        buf = add_reverb(buf, amount=reverb)
    peak = np.max(np.abs(buf))
    if peak > 0:
        buf = buf / peak * 0.89
    return buf, loop_len


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "tempo")
    os.makedirs(out, exist_ok=True)
    pegs, drop_x, init_vx = build_board(1)
    collisions, exit_t = simulate(pegs, drop_x, init_vx)
    scale = SCALES["minor_pent"]

    print(f"board 1: {len(pegs)} pegs, {len(collisions)} hits, exit_t={exit_t:.2f}s\n")
    print(f"{'file':36s} {'mode':14s} {'bpm':>4} {'loop_s':>7}")
    print("-" * 64)
    jobs = [
        ("locked_90bpm",   "locked",      90),
        ("locked_140bpm",  "locked",      140),
        ("independent_90", "independent", 90),
        ("independent_140","independent", 140),
    ]
    for name, mode, bpm in jobs:
        buf, loop_len = synth_mode(collisions, exit_t, scale, bpm, mode)
        fname = f"board1_{name}.wav"
        write_wav(os.path.join(out, fname), buf)
        print(f"{fname:36s} {mode:14s} {bpm:4d} {loop_len:7.2f}")

    print("\nListen for:")
    print(" - locked_90 vs locked_140: SAME pattern, different speed -> tempo-locked works.")
    print(" - independent_90 vs independent_140: should sound the SAME (BPM-invariant);")
    print("   only the bar grid differs. Confirms tempo-independent physics.")
    print(" - All four: still musical? If yes, both Sync Modes are viable.")


if __name__ == "__main__":
    main()

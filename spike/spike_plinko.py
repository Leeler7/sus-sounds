"""
spike_plinko.py  -- one-evening throwaway to PROVE THE SOUND.

Purpose (and only purpose): test the hypothesis from ARCHITECTURE.md section 5 --
do physics-built Plinko boards produce *distinct, musical* delay/echo patterns that
you could NOT easily dial in on a parametric bouncing-ball delay?

It is NOT the VST architecture. Minimal hand-rolled physics, offline render to WAV.
Tweak the MAPPING section -- that is where the taste lives.

Run:  python spike_plinko.py
Out:  ./out/board_<seed>_<scale>.wav   (stereo, 44.1k, 16-bit)

Deps: numpy + stdlib only.
"""

import math
import os
import wave
import numpy as np

SR = 44100

# ----------------------------------------------------------------------------
# Board / physics (minimal, throwaway -- believable Plinko bounces, that's all)
# ----------------------------------------------------------------------------
W, H = 1.0, 1.4            # board width / height (normalized units)
BALL_R = 0.018
PEG_R = 0.024
GRAVITY = 2.0
RESTITUTION = 0.86         # <1 so energy decays -> emergent diminuendo
DT = 1.0 / 1200.0          # fixed physics timestep (stable, deterministic)
MAX_SIM_T = 9.0            # safety cap (seconds of simulated time)
ENERGY_FLOOR_V = 0.05      # below this speed, nudge (no-stuck, deterministic)


def build_board(seed, rows=9):
    """Staggered Plinko grid with per-seed jitter + random gaps -> distinct boards."""
    rng = np.random.default_rng(seed)
    pegs = []
    for r in range(rows):
        y = 0.18 + r * (H - 0.30) / rows
        offset = 0.0 if (r % 2 == 0) else 0.5
        cols = 7
        for c in range(cols):
            if rng.random() < 0.18:          # random gaps -> different boards
                continue
            x = (c + offset) * (W / cols) + (W / cols) * 0.5
            x += float(rng.normal(0, 0.012))  # jitter
            y_j = y + float(rng.normal(0, 0.010))
            if 0.06 < x < W - 0.06:
                pegs.append((x, y_j))
    drop_x = W * 0.5 + float(rng.normal(0, 0.06))
    drop_x = min(max(drop_x, 0.12), W - 0.12)
    init_vx = float(rng.normal(0, 0.05))
    return np.array(pegs, dtype=np.float64), drop_x, init_vx


def simulate(pegs, drop_x, init_vx):
    """Return list of peg-collision events: (t_seconds, norm_x, norm_y, energy)."""
    pos = np.array([drop_x, 0.04], dtype=np.float64)
    vel = np.array([init_vx, 0.0], dtype=np.float64)
    collisions = []
    t = 0.0
    rr = (BALL_R + PEG_R)
    px, py = pegs[:, 0], pegs[:, 1]
    while t < MAX_SIM_T and pos[1] < H:
        vel[1] += GRAVITY * DT
        pos += vel * DT
        t += DT

        # walls
        if pos[0] < BALL_R:
            pos[0] = BALL_R; vel[0] = -vel[0] * RESTITUTION
        elif pos[0] > W - BALL_R:
            pos[0] = W - BALL_R; vel[0] = -vel[0] * RESTITUTION

        # nearest overlapping peg (resolve one per step -- fine for a spike)
        dx = pos[0] - px; dy = pos[1] - py
        d2 = dx * dx + dy * dy
        i = int(np.argmin(d2))
        dist = math.sqrt(d2[i])
        if dist < rr and dist > 1e-9:
            nx, ny = dx[i] / dist, dy[i] / dist          # collision normal
            vn = vel[0] * nx + vel[1] * ny               # normal velocity
            if vn < 0:                                   # moving into the peg
                energy = -vn
                # reflect + restitution
                vel[0] -= (1 + RESTITUTION) * vn * nx
                vel[1] -= (1 + RESTITUTION) * vn * ny
                # push out of overlap
                pos[0] += (rr - dist) * nx
                pos[1] += (rr - dist) * ny
                collisions.append((t, pos[0] / W, pos[1] / H, energy))

        # no-stuck nudge (deterministic-ish: tiny upward+side kick)
        if abs(vel[0]) + abs(vel[1]) < ENERGY_FLOOR_V:
            vel[0] += 0.08 * (1 if pos[0] < W * 0.5 else -1)
            vel[1] -= 0.05

    return collisions, t


# ----------------------------------------------------------------------------
# MAPPING  <-- this is the product. Tweak freely; this is what we're testing.
# ----------------------------------------------------------------------------
SCALES = {
    "minor_pent": [0, 3, 5, 7, 10],
    "major":      [0, 2, 4, 5, 7, 9, 11],
    "lydian":     [0, 2, 4, 6, 7, 9, 11],
}
ROOT_HZ = 220.0           # A3
N_OCTAVES = 3


def degree_table(scale):
    degs = []
    for octv in range(N_OCTAVES):
        for d in scale:
            degs.append(octv * 12 + d)
    return degs


def peg_to_tap(norm_x, norm_y, energy, energy_max, scale):
    """The §5 mapping: x->pan, y->scale-quantized pitch, energy->level+brightness."""
    e = min(energy / (energy_max + 1e-9), 1.0)

    # pitch: higher peg (small y) -> higher note, quantized to scale
    degs = degree_table(scale)
    idx = int(round((1.0 - norm_y) * (len(degs) - 1)))
    idx = min(max(idx, 0), len(degs) - 1)
    semis = degs[idx]
    freq = ROOT_HZ * (2.0 ** (semis / 12.0))

    pan = min(max(norm_x, 0.0), 1.0)          # 0 = L, 1 = R
    level = 0.20 + 0.80 * e                    # energy -> loudness (with floor)
    brightness = e                             # energy -> harmonic content
    return freq, pan, level, brightness


# ----------------------------------------------------------------------------
# Synthesis
# ----------------------------------------------------------------------------
GRAIN_LEN = int(0.38 * SR)
_t_grain = np.arange(GRAIN_LEN) / SR


def make_grain(freq, brightness):
    """Plucky enveloped tone; higher harmonics scale with brightness (energy)."""
    env = np.exp(-_t_grain / 0.16)
    env[: int(0.003 * SR)] *= np.linspace(0, 1, int(0.003 * SR))  # soft attack
    amps = [1.0, 0.5 * brightness, 0.28 * brightness, 0.14 * brightness]
    sig = np.zeros(GRAIN_LEN)
    for k, a in enumerate(amps, start=1):
        if a <= 0:
            continue
        sig += a * np.sin(2 * np.pi * freq * k * _t_grain)
    return sig * env


def synth(collisions, exit_t, scale, bpm=120, loop_bars=2,
          echoes=3, feedback=0.5, reverb=0.18):
    """Tempo-lock the emergent rhythm, render stereo, tile a few loops, add echoes+reverb."""
    beat = 60.0 / bpm
    loop_len = loop_bars * 4 * beat
    scale_factor = loop_len / max(exit_t, 1e-3)   # time-scale physics into the loop
    echo_step = beat / 2.0                          # eighth-note echoes
    n_loops = 4
    total_t = loop_len * n_loops + 1.0
    n = int(total_t * SR) + GRAIN_LEN + 1
    buf = np.zeros((n, 2))

    energies = [c[3] for c in collisions] or [1.0]
    emax = max(energies)

    for loop_i in range(n_loops):
        loop_t0 = loop_i * loop_len
        for (t, nx, ny, energy) in collisions:
            freq, pan, level, brightness = peg_to_tap(nx, ny, energy, emax, scale)
            grain = make_grain(freq, brightness)
            lg = math.cos(pan * math.pi / 2) * level   # equal-power pan
            rg = math.sin(pan * math.pi / 2) * level
            t_loop = loop_t0 + t * scale_factor
            for rep in range(echoes):
                g = (feedback ** rep)
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
    return buf


def add_reverb(buf, amount=0.18, decay=0.42):
    """Tiny synthetic-IR reverb via FFT convolution (numpy only)."""
    ir_len = int(decay * SR)
    rng = np.random.default_rng(7)
    ir = rng.normal(0, 1, ir_len) * np.exp(-np.arange(ir_len) / (0.16 * SR))
    out = np.zeros_like(buf)
    L = buf.shape[0] + ir_len
    nfft = 1 << (L - 1).bit_length()
    IR = np.fft.rfft(ir, nfft)
    for ch in range(2):
        wet = np.fft.irfft(np.fft.rfft(buf[:, ch], nfft) * IR, nfft)[: buf.shape[0]]
        wp = np.max(np.abs(wet)) + 1e-9
        out[:, ch] = buf[:, ch] + amount * wet / wp * np.max(np.abs(buf[:, ch]) + 1e-9)
    return out


def write_wav(path, buf):
    data = np.clip(buf, -1, 1)
    pcm = (data * 32767).astype(np.int16)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


# ----------------------------------------------------------------------------
def main():
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
    os.makedirs(out_dir, exist_ok=True)
    print(f"Rendering to {out_dir}\n")
    print(f"{'file':40s} {'pegs':>5} {'hits':>5} {'exit_t':>7}")
    print("-" * 64)
    for seed in (1, 7, 42):
        pegs, drop_x, init_vx = build_board(seed)
        collisions, exit_t = simulate(pegs, drop_x, init_vx)
        for scale_name in ("minor_pent", "major"):
            buf = synth(collisions, exit_t, SCALES[scale_name])
            fname = f"board_{seed}_{scale_name}.wav"
            write_wav(os.path.join(out_dir, fname), buf)
            print(f"{fname:40s} {len(pegs):5d} {len(collisions):5d} {exit_t:7.2f}")
    print("\nListen for: do different boards give *recognizably different* rhythms")
    print("and melodic shapes? Same board + different scale should share rhythm,")
    print("differ in pitch set. If boards all sound samey -> the moat is weak; if")
    print("they each feel like their own little riff -> the hypothesis holds.")
    print("\nTweak the MAPPING section (peg_to_tap) and re-run to taste.")


if __name__ == "__main__":
    main()

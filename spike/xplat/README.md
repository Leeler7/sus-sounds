# T0a — cross-platform determinism spike (Box2D v3)

**Status: NOT YET RUN.** This machine (Windows) has no C/C++ compiler and no CMake,
and the test is inherently multi-machine anyway. The harness is ready; you run it.

## The question
Does Box2D v3 produce **bit-identical** physics on Windows, macOS, and Linux? The
"shared boards sound identical for everyone" promise (ARCHITECTURE.md §2) rests on
a yes. Box2D v3 is *engineered* for this, but it's unverified for our use. This is
the riskiest unproven assumption in the whole plan, so it goes first.

## What it does
`main.c` drops one ball through a fixed peg layout and prints the ball's position
every step as exact hex floats (`%a`). Same physics => same trajectory => identical
output file. The output is a faithful fingerprint of the simulation.

## Prerequisites (per machine)
- A C compiler (MSVC/clang on Windows, clang/gcc on macOS/Linux)
- CMake >= 3.20
- git (CMake fetches Box2D v3 automatically)

Windows install if missing: `winget install Kitware.CMake` and Visual Studio Build
Tools (or `winget install LLVM.LLVM`).

## Build + run (same on every OS)
```sh
cd spike/xplat
cmake -B build
cmake --build build --config Release
# run, capturing output per OS:
./build/xplat > out_<os>.txt          # macOS/Linux
./build/Release/xplat.exe > out_windows.txt   # Windows (MSVC layout)
```

## Compare across machines
Copy the three `out_*.txt` files to one place, then:
```sh
sha256sum out_windows.txt out_macos.txt out_linux.txt   # hashes must match
# or see exactly where they diverge:
diff out_windows.txt out_macos.txt
```

## Reading the result
- **All three hashes identical** => GREEN. Box2D v3 cross-platform determinism holds
  for our sim. Proceed with the full build and keep the promise.
- **Any difference** => the cross-platform promise is at risk. Find the first
  diverging step (`diff`), then decide: tighten flags, pin a different Box2D config,
  or downgrade the promise to same-binary only (the original CEO-review position).
  Better to know now than after building the engine on the assumption.

## Windows reference result (recorded 2026-06-17)
Built and run on this machine. Toolchain: CMake 4.3.3 + MSVC 19.44 (VS Build Tools 2022),
Box2D **v3.1.1** via FetchContent.

- **Same-binary determinism: CONFIRMED.** 4 repeat runs → identical output every time.
- **Windows reference SHA256 of `out_windows.txt` (LF-normalized):**
  `88BFD0F014C3E83277493C576586F4028BDD62C37E428931654393D5E537D48C`
- Output is 3000 lines of `step hex(xbits) hex(ybits)` — the raw IEEE-754 bit patterns,
  NOT `%a`. (`%a`'s digit count is implementation-defined, so it would falsely differ
  across platforms even for identical values. Bit patterns are platform-stable.)

To complete the cross-platform gate: build+run on macOS and Linux, normalize with
`tr -d '\r'`, then check each hash against the Windows hash above. All equal => GREEN.

**Easiest path — let CI do it:** `.github/workflows/determinism.yml` runs this on
ubuntu + windows + macOS (Intel x64 and Apple Silicon arm64), normalizes, and fails if
any platform disagrees. Push the repo to GitHub and it runs automatically. Requires the
project to be a git repo on GitHub first.

Exact commands that worked on Windows (from `spike/xplat/`):
```
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\xplat.exe > out_windows.txt
```
On macOS/Linux the default generator is fine: `cmake -B build && cmake --build build --config Release`.

Note: the harness has no floor/walls, so after passing the pegs the ball free-falls for the
rest of the 3000 steps. That's intentional — we only need a deterministic trajectory
fingerprint, not a realistic sim.

## Caveats
- The Box2D v3 C API symbols in `main.c` are from memory; if the build errors on a
  name, check it against the fetched headers (`build/_deps/box2d-src/include/box2d/`)
  and adjust. The *method* is what matters, not the exact call names.
- Keep the determinism compiler flags (CMakeLists.txt) on every platform. A stray
  `-ffast-math` anywhere is the classic silent determinism break.

# MPC Plaits for 64-bit MPCs (aarch64)

A 64-bit build of MPC Plaits 1.0.1 for "Gen2" MPCs (MPC Key 37 G2, One G2, Live III, XL), installed the official
way: a plugin folder in `VST2 Plugins`, read by MPC OS 3.10.1 or later with External Plugin Support enabled. The
1.0.1 release is 32-bit (armv7) only, which a Gen2 MPC cannot load. This is a contributed, unofficial build route,
not a release; it does not touch `build.sh` or the Docker path.

The port needed no change to compile and run on 64-bit. Testing it on a device turned up two audible bugs and one
table over-read in the source, none of them specific to 64-bit: the v1.0.1 source shows the first two on 32-bit
ARM as well. Their fixes are recorded in `src/VENDORED.md` and summarised under "What was changed".

## Build

```
git clone --branch poloq-dev https://github.com/poloq-instruments/mpc-vst-plugins ../mpc-vst-plugins-poloq-dev
./build-aarch64.sh
```

No Docker. Needs zig (a `zig` binary, or a Python with the `ziglang` package: `ZIG=...`; by default the one inside
the MPC Plugin Builder SDK at `../mpc-plugin-builder-sdk/.sdk-tools/zigenv`), `python3`, `curl`, `unzip`, `shasum`.
Takes a few seconds. Everything is written under `vst/build/` (git-ignored):

- `vst/build/aarch64/plaits.so`
- `vst/build/aarch64/poloq - VST - MPC Plaits/`: the folder to copy into `VST2 Plugins`
- `vst/build/aarch64/harness/render`, `modelbench`: the port's two `dlopen()` test programs, built for the device
- `vst/build/release/`: the downloaded 1.0.1 zip, unpacked

Pinned inputs:

| Input | Revision |
|---|---|
| This repository | `v1.0.1` (`b113c83`) plus the three fixes below (`git diff v1.0.1 -- src`) |
| Wrapper, Schwung adapter, `gen_vst.py` | `poloq-instruments/mpc-vst-plugins`, branch `poloq-dev`, `dd50b07` (2026-10-05) |
| Compiler | zig 0.17.1 (`zig cc` / `zig c++`), target `aarch64-linux-gnu.2.31`, `-mcpu=cortex_a55` |
| Skin | `MPC-Plaits-1.0.1-mpc-armv7.zip`, sha256 `76986f7a6d221bcba9cb8e3289b659bb25527af146dde7df2b535cb259f1c763` |

With these, `plaits.so` comes out as sha256 `36e48ddba93a66b830aabb3355f3f26d1abc9bc39cd2fe0ff37291ff7f45abf6`.
(The build of the v1.0.1 source without the fixes was `b6dc255d…e025`, the same on a cold and a warm compiler
cache.)

## What was changed

### Nothing for 64-bit

`src/` compiled for aarch64 without an edit. The code already avoids the 32-bit-only parts of Mutable Instruments'
`stmlib` (`-DTEST` selects the portable `Sqrt`/`Clip16` instead of the ARMv7 inline assembly). To check that word
size and compiler play no part, the same source was built as a static 32-bit ARM program (`arm-linux-musleabihf`,
Cortex-A17) and run on the MPC next to the 64-bit build: a held note on each of the 24 models gave **bit-identical**
output, and so did an unoptimised (`-O0`) build on the Mac.

### Three fixes to the port's source

All marked `mpc-vst-plaits:` in the code and recorded in `src/VENDORED.md` ("Render chunk size, Chiptune gate, sine
table"); `git diff v1.0.1 -- src/dsp` shows them (4 files).

1. **The 6-op FM models (6-Op FM I, II, III) broke their waveform once per audio block** (heard on the device as
   grain, "like a sample-rate mismatch on the carrier"). Plaits' six-operator engine renders its two internal voices
   on alternate calls, twice the call's length each, and carries half over to the next call, so it needs every call
   to have the same length. The bridge cut a 128-frame host block into ten 12-frame chunks and one of 8.
   Fix: `render_block()` in `src/dsp/freak_plugin.cpp` only ever asks the engine for whole 12-frame chunks and keeps
   what a host block has not used for the next one (`PPF_CHUNK_FRAMES` in `plaits_move_engine.h`). Side effect: a
   note or parameter change can be heard up to 11 frames (0.25 ms) later than before.
   Test: `tests/blockedge.c`. v1.0.1: break 90 to 220000 times the median at 128- and 64-frame blocks; fixed: 1.1
   to 1.3.
2. **A released Chiptune key kept sounding at full level** in the default GATE amp mode (until the voice was stolen,
   or the 30 s safety cap). With TRIG patched, Plaits bypasses the low-pass gate for Chiptune, and at TIMBRE ATT 0
   the engine has no decay of its own, so nothing closed the note.
   Fix: in GATE mode the key gate is applied to a Chiptune voice as a gain with a ~3 ms slew
   (`src/dsp/plaits_move_engine.cpp`, `gate_gain`). Other amp modes and other models are untouched; PING with
   Chiptune still rings on, as before.
   Test: `tests/chiprelease.c`. v1.0.1: -18.8 dBFS one second after release; fixed: silent.
3. **`SineOscillator::Next` could read one entry past `lut_sine`** (`plaits/dsp/oscillator/sine_oscillator.h`, the
   bass drum's sustained mode): `phase_ + 0.25f` rounds up to exactly 1.25 when `phase_` is the last float below 1.
   ASan found it in `tests/fuzz.c` once fix 1 had shifted the fuzzer's timing. Fix: 1.25 is taken as 0.25, the same
   point of the sine.

A steady note on every pitched model other than the three 6-op ones measures the same before and after (level
and spectrum); the noise-driven models differ in their random detail only.

Files added for the build (all new, none replaces an existing file):

| File | Purpose |
|---|---|
| `build-aarch64.sh` | the build described here |
| `tools/check_aarch64.py` | offline checks of the result (below) |
| `tools/compare_raw.py` | compares two outputs of `tests/render.c` model by model |
| `tests/run_native.sh` | runs the port's tests natively (no Docker) |
| `BUILD-AARCH64.md` | this note |

How the build differs from the release's armv7 build (`mpc-vst-plugins` `tools/build_port.sh`):

| | 1.0.1 release (armv7) | this build (aarch64) |
|---|---|---|
| Compiler | gcc 11 in Docker (`arm32v7/gcc:11-bullseye`) | clang via zig 0.17.1, cross-compiled on macOS |
| Common flags | `-fPIC -fvisibility=hidden`, `vst.json` `cflags` | the same, plus `-ffunction-sections -fdata-sections`, `-Wl,--gc-sections`, and a version script that exports only `VSTPluginMain` |
| CPU flags | `-mcpu=cortex-a17 -mfpu=neon-vfpv4 -O3 -funsafe-math-optimizations` | `-mcpu=cortex_a55 -O3` |
| C++ runtime | `libstdc++.so.6` from the device | LLVM libc++, linked statically |
| glibc | symbols up to 2.27 | built against 2.31, symbols used up to 2.27 |
| Wrapper C files | `-O2 -std=gnu11` | the same |

`-funsafe-math-optimizations` was left out on purpose. On armv7 gcc needs it to use NEON for float maths at all;
on aarch64 NEON is IEEE-conformant and used without it, and clang's reading of the flag is not what the release
was tested with. The cost is CPU, which is not short here (see the device numbers).

## What was reused from the 1.0.1 release

From `MPC-Plaits-1.0.1-mpc-armv7.zip` (zip digest and all 449 entries of its `SHA256SUMS` verified), unchanged:

- `Plugin Skins/` (439 files: `TUI.json`, `Q-Links.json`, `Q-Links - 8by1.json`, the PNGs). The skin does not depend
  on the CPU. Rebuilding it needs Docker with headless Chromium (`"art": "html"`), which the machine used here does
  not have.
- `version.xml`.

Not used from the zip: `plaits.so` (armv7), `mpc-plugin.json` and `plugin-meta.xml` (they describe the armv7
package and the `MPC.settings` route), `install.sh`, `uninstall.sh`, `plugin_list.awk` (never run; they edit
`MPC.settings`).

Added to the folder: the aarch64 `plaits.so` at the folder root (where the release keeps it), `plugin.json` (name
`MPC Plaits`, manufacturer `poloq`, uid `0x4d69506c` = `MiPl`, `"format": "vst2"`, `"file64": "plaits.so"`; no
`file32`), the repository's `LICENSE`, and `CREDITS.txt`.

## What was tested

### Offline, on the build (every run of `build-aarch64.sh`)

`tools/check_aarch64.py`:

- `plaits.so` is an ELF64 little-endian aarch64 shared object, exports only `VSTPluginMain`, needs only `libm`,
  `libc`, `libpthread`, `libdl`, and no glibc symbol newer than 2.27 (the device has 2.39).
- The skin binds parameters 0 to 148 and the build has 149.
- The parameter table compiled into this build equals the one inside the released armv7 binary in every field
  (149 parameters, 17 fields each: key, name, unit, min, max, default, options, momentary, display kind, step and
  popup links). So the released skin fits, and projects saved with the armv7 plugin address the same parameters.

`tools/validate.py` of inMusic's MPC Plugin Builder SDK on the folder: binary, folder name, `TUI.json` (6 tabs, every image present) and
`Q-Links.json` pass. It reports one error, `plugin.json is missing required keys: file32`, which is expected for a
64-bit-only folder (a 64-bit MPC reads only `file64`), and one warning, `version.xml has no <uiversion>` (the
release's file; the SDK documents a missing `<uiversion>` as compatible, and other catalogue plugins on the device
ship the same shape).

### Offline, natively on an Apple Silicon Mac (`tests/run_native.sh`)

A 64-bit ARM host with clang: the same pointer width, alignment rules and NEON as the device, not the same libc.
All linked with the wrapper, the adapter and the engine under ASan + UBSan:

- `mpc-vst-plugins` `tools/host_test.c`: 15 checks pass (two instances, every parameter's name and display,
  set/get, options, popups, note to audio, chunk save and restore, `step_of`, transport, threads).
- The port's own `controls`, `sustain`, `voices`, `voicing`, `modulation`, `deadvoice`, `notes -1` (every note on
  every model in three voice modes) and `fuzz` (20000 blocks, seed 1), plus the new `blockedge` and `chiprelease`:
  all pass. `fuzz` was also run by hand with seeds 2 to 8 and for 200000 blocks with seed 1: clean.
- Before the source fixes, the same tests built as x86-64 and run under Rosetta printed identical output for
  `host_test`, `controls`, `sustain`, `voices`, `voicing`, `modulation` and `notes`. Not repeated after the fixes.
- The SDK's `host_sim` on an optimised native library: 18 checks pass (MPC load sequence, block sizes 32/128/512,
  200 random parameter writes, no NaN/Inf, all-notes-off, audio written to `vst/build/native/host_sim.wav`,
  peak -7.4 dBFS). Its chunk round-trip check also lists some of the 13 hidden `... List` popup flags as not
  restored. That is by design: they are screen state the wrapper keeps out of the saved chunk (`host_test` checks
  exactly that), and `run_native.sh` counts only other failures.

### On the device, outside the MPC app (MPC Key 37 G2, MPC OS 3.10.1.12, glibc 2.39, 2026-11-15/16)

The finished `plaits.so`, loaded with `dlopen()` by small harnesses run from the plugin folder (deleted afterwards):

- `render` (the port's): all 24 models rendered. Compared with the same phrase rendered by the native Mac build
  (`tools/compare_raw.py`): every model at the same level, difference 54 to 116 dB below the signal.
- A held-then-released note per model, old binary against fixed binary, both on the device: the block-edge break
  of the three 6-op FM models goes from 86 to 220000 times the median to 1.1 to 1.3, and Chiptune one second after
  release from -18.8 dBFS to silence. VA VCF, as a control, is unchanged.
- A steady middle C on VA VCF and Harmonic under host-like conditions (odd block sizes, flush-to-zero, channel
  aftertouch, mod wheel, transport running, the host's parameter-0 toggle): harmonics only, everything else at
  least 73 dB down, 261.8 Hz.
- `modelbench` (the port's), 6 voices, 2 s per model, share of one 128-frame block (2.9 ms): 13.2 % on average on a
  Cortex-A55 core (cpu3, 1.8 GHz), the costliest models around 17 to 18 %. On a Cortex-A76 core (cpu7, 2.4 GHz) the
  build before the fixes averaged 4.4 %.

### On the device, in the MPC app

See "Device load" at the end of this note.

### Not tested

- One folder carrying both `file32` and `file64`: each build writes its own folder with one of them.
- Any Gen2 device other than the Key 37 G2, and any MPC OS other than 3.10.1.12.
- The `MPC.settings` / modded-MPC install route (the zip's `install.sh`): not used here.
- ASan/UBSan on aarch64 Linux itself (the sanitizer runs were on macOS arm64 and x86-64).
- A build with `-funsafe-math-optimizations`, and a build with gcc for aarch64 (what the Docker path would use).
- A freshly rendered skin: the skin is the 1.0.1 release's, not rebuilt from `vst/layout.conf`.

## Found on the way

- **A NaN can reach the output stage (not 64-bit specific, not fixed).** With clang's UBSan, which unlike gcc's
  includes `float-cast-overflow`, `tests/fuzz.c` (seed 1) and `tests/deadvoice.c` stop at `plaits/dsp/voice.h:112`:
  a NaN sample is converted to `int`. It reproduces the same way as arm64 and as x86-64, with the v1.0.1 source and
  with the fixes, so the armv7 release has it too. It needs the fuzzer's random parameter jumps; on ARM the
  conversion gives 0 (a silent sample) and `deadvoice` passes without that check, so voices recover. The Docker
  test runs (gcc) cannot see it. `STRICT=1 tests/run_native.sh` shows it.
- **How the two audible bugs were found.** The first load on the device sounded grainy on the FM models. The device
  log, CPU load, sample rate (44100 Hz, 128-frame buffer) and underrun counters were all clean, and every reference
  available (Mac, x86-64, the device binary run outside the app) agreed with every other, because all of them were
  built from the same source. What located it was measuring the output itself: the energy of the second difference
  by position inside the host block (`tests/blockedge.c`).
- **The framework revision matters.** `build.sh` asks for `mpc-vst-plugins` on `poloq-dev`; that branch is now at
  `poloq-instruments/mpc-vst-plugins`. The port also builds against the current `open-mpc/mpc-vst-plugins`
  wrapper (24-field `param_t`), but two things then behave differently and `host_test` / `controls` report them:
  the newer wrapper ignores a second press of a button within 250 ms (MODEL `<` / `>`), and a Q-Link sweep of
  NOTES (polyphony) comes back one step short. On macOS that wrapper also needs
  `-Wl,-U,_mpc_engine_changed` for its weak engine hooks. This build uses `poloq-dev`, as the release did.

## The 32-bit (armv7) build

`./build-armv7.sh` (= `./build-aarch64.sh armv7`) builds the same sources with the same compiler for the 32-bit
MPCs: target `arm-linux-gnueabihf.2.31`, `-mcpu=cortex_a17`, output in `vst/build/armv7/`, `plugin.json` with
`"file32": "plaits.so"`. Its purpose is a 32-bit library that carries the three fixes; the 1.0.1 release does not.
`plaits.so` comes out as sha256 `58a8e529ddd066e6a0e680d182ee94260c8f124ff2b271e1e8e3ac08f1a6771f` (ELF32 hard-float
ARM, exports `VSTPluginMain` only, needs `libm`, `libc`, `libdl` and the loader, glibc symbols up to 2.27, C++
runtime linked in; the parameter table equals the released binary's in every field).

Unlike the aarch64 build it keeps the release's `-O3 -funsafe-math-optimizations`. Measured on an MPC Live II
(Cortex-A17, `modelbench`, 6 voices, share of one 128-frame block, average over the 24 models):

| Library | average | costliest model (mean) |
|---|---|---|
| 1.0.1 release (gcc 11, `-O3 -funsafe-math-optimizations`) | 16.5 to 16.7 % | Harmonic 25.3 % |
| this build, `-O3` only | 17.9 % | Harmonic 26.7 % |
| this build, `-O3 -funsafe-math-optimizations` (the default) | 16.6 to 16.7 % | Harmonic 25.7 % |

`-O3` alone gives output bit-identical to the aarch64 build but costs 7 % more CPU than the release on average (up
to 27 % on Snare Drum), on a device where CPU is short. With the flag a steady note differs from the strict build
by 70 to 105 dB below the signal.

Tested on the MPC Live II (MPC 3.10.1.12, glibc 2.39), outside the MPC app, with the harnesses loading the
installed 1.0.1 release binary and this build side by side:

- Held-then-released note: the release binary shows both bugs (block-edge break of 6-Op FM I / II / III 14441 /
  221723 / 86 times the median; Chiptune at -18.8 dBFS one second after release); this build 1.1 / 1.2 / 1.3 and
  silence. These are the released binary's own numbers, not a rebuild's.
- Steady middle C on all 24 models, release binary against this build: every pitched model the same in level and
  spectrum (0.0 dB in every quarter-octave band) except the three repaired 6-op models; the noise-driven models
  (Swarm, Noise, Particle, Inharmonic String, Hi-Hat) differ in random detail.
- `render` (all 24 models, every parameter swept each block): no NaN/Inf, levels within 1.3 dB of the strict build.
  This test separates any two builds that round differently: the release binary and a strict clang build of the
  v1.0.1 source differ by as much (levels within 1.5 dB, Swarm 3.9 dB), and the unsafe-math build is the closer of
  the two to the release binary.
- Natively (`XFLAGS=-funsafe-math-optimizations tests/run_native.sh`): all eleven host tests pass.

In the MPC app on the Live II (2026-11-16): the library replaced the release's `plaits.so` in the installed folder
(the release's skin and `version.xml` kept, `plugin.json` with `file32`); after an app restart the log shows
`Attempting to load VST`, `Creating VST instance: plaits`, `Initialising VST: plaits (1.0.0.1)`, no crash, and the
6-op FM models and Chiptune sound right by ear.

Not tested for armv7: the Docker/gcc route with the fixes, MPC OS 2.x (the build needs nothing newer than glibc
2.27, as the release), any 32-bit device other than the Live II.

## For a Gen 2 release

This script is one way to get a Gen 2 binary, not the only one. In the catalogue framework's own terms the port
needs `"targets": ["armv7", "aarch64"]` in `vst/vst.json` (plus `"cflags_aarch64": ["-O3"]`) and a framework
revision that has the aarch64 target, i.e. current `open-mpc/mpc-vst-plugins`; mind the two behaviour differences
noted above when moving off `poloq-dev`. A `plugin.json` like the one written here makes the folder loadable by MPC
OS 3.10.1's `VST2 Plugins` loader with no `MPC.settings` edit.

## Install on the device

```
F="poloq - VST - MPC Plaits"
# copy as "<folder>.new" into "/storage/VST2 Plugins/" on the MPC, compare sha256 of every file, then:
ssh root@<mpc> 'cd "/storage/VST2 Plugins" && mv "poloq - VST - MPC Plaits.new" "poloq - VST - MPC Plaits"'
```

Plugins load only from the internal drive (`/storage/VST2 Plugins/`); the SD card is mounted `noexec`. If the
running MPC app does not list the new folder under External Plugins, restart the app (that discards unsaved work).
To remove the plugin, delete the folder and restart the app.

## Device load

**First install (v1.0.1 source, no fixes), 2026-11-15.** Folder installed 23:46 UTC at
`/storage/VST2 Plugins/poloq - VST - MPC Plaits/` (444 files, every sha256 equal to the build). MPC app
restarted 23:49 (process 16092). Log, `az01-launch-MPC[16092]`:

```
23:49:58 Attempting to load VST: /storage/VST2 Plugins/poloq - VST - MPC Plaits/plaits.so
23:49:58 Creating VST instance: plaits
23:49:58 Initialising VST: plaits (1.0.0.1)
```

and the same three lines again at 23:54:08 (a second instance); same process afterwards, no crash. `MemAvailable`
2.75 GB after the restart, 2.40 GB with Plaits and another plugin loaded. Played model by model: it loads and
plays, the FM models sound grainy and Chiptune's envelope is wrong. Both traced to the port's source and
fixed (above).

**Fixed build, 2026-11-16.** `plaits.so` (`36e48ddb…abf6`) and the new `CREDITS.txt` were staged in the plugin
folder as `.new` files, checked on the device with the harnesses above, then renamed into place (checksums verified;
the first build's binary deleted). MPC app restarted 00:18 UTC (process 16975). Log, `az01-launch-MPC[16975]`:

```
00:19:54 Attempting to load VST: /storage/VST2 Plugins/poloq - VST - MPC Plaits/plaits.so
00:19:54 Creating VST instance: plaits
00:19:54 Initialising VST: plaits (1.0.0.1)
```

Same process afterwards, no crash. `MemAvailable` 2.77 GB after the restart, 2.40 GB with Plaits loaded and played.
The 6-op FM models and Chiptune, played again, sound right.

Checked in the app by ear: it loads from External Plugins, the models play, the 6-op FM grain is gone and Chiptune
notes stop on release. Not checked one by one in the app: each skin page and Q-Link assignment, tempo-synced LFOs
and TRIG RATE against the sequencer, the mod matrices, saving a project and loading it again, more than one
instance in a project, long sessions.

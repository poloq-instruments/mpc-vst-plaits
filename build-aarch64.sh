#!/usr/bin/env bash
# Build MPC Plaits for 64-bit (aarch64, "Gen2") MPCs without Docker, and assemble a plugin folder for
# MPC OS 3.10.1's official loader (<drive>/VST2 Plugins/). See BUILD-AARCH64.md.
#
#   ./build-aarch64.sh            build + assemble + offline checks
#
# Needs:
#   MPC_VST  an mpc-vst-plugins checkout on the branch build.sh asks for (poloq-dev; wrapper/, adapters/,
#            tools/gen_vst.py). Read only, nothing is written there. The 1.0.1 release was built from
#            https://github.com/poloq-instruments/mpc-vst-plugins poloq-dev at dd50b07; another revision builds a
#            different wrapper (see BUILD-AARCH64.md). Default: ../mpc-vst-plugins, else ../mpc-vst-plugins-poloq-dev.
#   ZIG      a zig executable, or a python that has the `ziglang` package (pip install ziglang).
#            Default: `zig` on PATH, else the one inMusic's MPC Plugin Builder SDK installs for itself, if that SDK is
#            at ../mpc-plugin-builder-sdk (.sdk-tools/zigenv).
#   curl, unzip, shasum, python3.
#
# Output (all under vst/build/, which is git-ignored):
#   vst/build/aarch64/plaits.so
#   vst/build/aarch64/poloq - VST - MPC Plaits/     the folder to copy into "VST2 Plugins"
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"

# ---- pinned inputs --------------------------------------------------------------------------------------------------
REL_VERSION=1.0.1
REL_ZIP="MPC-Plaits-$REL_VERSION-mpc-armv7.zip"
REL_URL="https://github.com/poloq-instruments/mpc-vst-plaits/releases/download/v$REL_VERSION/$REL_ZIP"
REL_SHA256=76986f7a6d221bcba9cb8e3289b659bb25527af146dde7df2b535cb259f1c763   # GitHub's published digest of the asset
FOLDER="poloq - VST - MPC Plaits"
WRAPPER_REV=dd50b07                   # poloq-instruments/mpc-vst-plugins, branch poloq-dev, 2026-10-05
ZIG_TARGET=aarch64-linux-gnu.2.31     # glibc 2.31 symbols at most; the device has 2.39
ZIG_CPU=cortex_a55                    # the little cores of the RK3588(S); runs on the A76 cores too
# The armv7 release is built with -mcpu=cortex-a17 -mfpu=neon-vfpv4 -O3 -funsafe-math-optimizations (vst.json cflags_arm).
# -O3 is kept. -funsafe-math-optimizations is NOT: on armv7 gcc needs it to use NEON for float maths at all, on aarch64
# NEON is IEEE-conformant and used anyway, and clang's reading of the flag is not what the release was tested with.
ARCH_CFLAGS="-O3"

# ---- tools ----------------------------------------------------------------------------------------------------------
if [ -z "${MPC_VST:-}" ]; then
  for d in "$here/../mpc-vst-plugins" "$here/../mpc-vst-plugins-poloq-dev"; do
    [ -f "$d/wrapper/vst2_wrap.c" ] && MPC_VST="$d" && break
  done
fi
[ -f "${MPC_VST:-}/wrapper/vst2_wrap.c" ] || { echo "need an mpc-vst-plugins checkout (set MPC_VST)" >&2; exit 1; }
MPC_VST="$(cd "$MPC_VST" && pwd)"

if [ -n "${ZIG:-}" ]; then
  case "$(basename "$ZIG")" in python*) zig=("$ZIG" -m ziglang) ;; *) zig=("$ZIG") ;; esac
elif command -v zig >/dev/null; then
  zig=(zig)
elif [ -x "$here/../mpc-plugin-builder-sdk/.sdk-tools/zigenv/bin/python" ]; then
  zig=("$here/../mpc-plugin-builder-sdk/.sdk-tools/zigenv/bin/python" -m ziglang)
else
  echo "need zig (set ZIG to a zig binary or to a python with the ziglang package)" >&2; exit 1
fi
MPC_VST_REV="$(git -C "$MPC_VST" rev-parse --short=7 HEAD 2>/dev/null || echo unknown)"
echo "zig $("${zig[@]}" version); mpc-vst-plugins at $MPC_VST ($MPC_VST_REV)"
[ "$MPC_VST_REV" = "$WRAPPER_REV" ] || echo "note: the $REL_VERSION release was built with mpc-vst-plugins $WRAPPER_REV (poloq-dev), this is $MPC_VST_REV" >&2

B="$here/vst/build/aarch64"
OBJ="$B/obj"
GEN="$B/gen"
rm -rf "$OBJ" "$GEN" "$B/$FOLDER" "$B/plaits.so"
mkdir -p "$OBJ" "$GEN/vst" "$GEN/src"

# ---- 1. params.h (pure Python; run on copies so nothing is written outside vst/build/) -------------------------------
cp "$here/vst/vst.json" "$here/vst/layout.conf" "$GEN/vst/"
cp "$here/src/module.json" "$GEN/src/"
(cd "$GEN" && PYTHONDONTWRITEBYTECODE=1 python3 -B -I "$MPC_VST/tools/gen_vst.py" "$GEN/vst/vst.json" --params-h)
PARAMS_DIR="$GEN/vst/build"

# ---- 2. compile ------------------------------------------------------------------------------------------------------
# Same shape as mpc-vst-plugins tools/build_port.sh for a C++ port: every source on its own (C++ by g++ rules, the wrapper
# and the Schwung adapter as plain C), one C++ link. Sources and common flags come from vst/vst.json.
SOURCES=()
while IFS= read -r line; do SOURCES+=("$line"); done < <(python3 -I -c 'import json,sys; print("\n".join(json.load(open(sys.argv[1]))["build"]["sources"]))' "$here/vst/vst.json")
CFLAGS=()
while IFS= read -r line; do CFLAGS+=("$line"); done < <(python3 -I -c 'import json,sys; print("\n".join(json.load(open(sys.argv[1]))["build"]["cflags"]))' "$here/vst/vst.json")
TGT=(-target "$ZIG_TARGET" "-mcpu=$ZIG_CPU")
COMMON=(-fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter -Wno-unused-local-typedef)

cd "$here"
objs=()
for f in "${SOURCES[@]}"; do
  o="$OBJ/${f//\//_}.o"
  "${zig[@]}" c++ "${TGT[@]}" $ARCH_CFLAGS "${COMMON[@]}" -fvisibility-inlines-hidden "${CFLAGS[@]}" \
      -I"$PARAMS_DIR" -I"$MPC_VST/wrapper" -c "$f" -o "$o" &
  objs+=("$o")
  while [ "$(jobs -r | wc -l)" -ge 8 ]; do sleep 0.1; done
done
"${zig[@]}" cc "${TGT[@]}" -O2 "${COMMON[@]}" -std=gnu11 -I"$PARAMS_DIR" -c "$MPC_VST/wrapper/vst2_wrap.c" -o "$OBJ/vst2_wrap.o" &
"${zig[@]}" cc "${TGT[@]}" -O2 "${COMMON[@]}" -std=gnu11 -c "$MPC_VST/adapters/schwung/schwung_engine.c" -o "$OBJ/adapter.o" &
fail=0
for j in $(jobs -p); do wait "$j" || fail=1; done
[ "$fail" = 0 ] || { echo "compile failed" >&2; exit 1; }

printf '{ global: VSTPluginMain; local: *; };\n' > "$OBJ/exports.map"
"${zig[@]}" c++ "${TGT[@]}" -O2 -shared -fPIC -fvisibility=hidden "${objs[@]}" "$OBJ/vst2_wrap.o" "$OBJ/adapter.o" \
    -lm -lpthread -ldl -Wl,--no-undefined -Wl,--gc-sections -Wl,-s "-Wl,--version-script=$OBJ/exports.map" -o "$B/plaits.so"

# The port's two dlopen() harnesses for the device itself (tests/render.c, tests/modelbench.c): they load the finished
# plaits.so outside the MPC app. Not part of the plugin folder; see BUILD-AARCH64.md "On-device check".
mkdir -p "$B/harness"
for t in render modelbench; do
  "${zig[@]}" cc "${TGT[@]}" -O2 -w -std=gnu11 -I"$PARAMS_DIR" "tests/$t.c" -lm -ldl -o "$B/harness/$t"
done

# ---- 3. the skin, from the published release (architecture-independent; rebuilding it needs Docker + headless Chromium) -
REL="$here/vst/build/release"
mkdir -p "$REL"
[ -f "$REL/$REL_ZIP" ] || curl -fsSL -o "$REL/$REL_ZIP" "$REL_URL"
echo "$REL_SHA256  $REL/$REL_ZIP" | shasum -a 256 -c - >/dev/null || { echo "release zip checksum mismatch" >&2; exit 1; }
rm -rf "$REL/MPC-Plaits-$REL_VERSION"
unzip -q "$REL/$REL_ZIP" -d "$REL"
(cd "$REL/MPC-Plaits-$REL_VERSION" && shasum -a 256 -c SHA256SUMS >/dev/null) || { echo "release SHA256SUMS mismatch" >&2; exit 1; }
UP="$REL/MPC-Plaits-$REL_VERSION/portable/$FOLDER"

# ---- 4. assemble the folder ------------------------------------------------------------------------------------------
OUT="$B/$FOLDER"
mkdir -p "$OUT"
cp -R "$UP/Plugin Skins" "$OUT/"
cp "$UP/version.xml" "$OUT/"
cp "$B/plaits.so" "$OUT/plaits.so"          # at the folder root, where the release keeps it
cp "$here/LICENSE" "$OUT/LICENSE"
cat > "$OUT/CREDITS.txt" <<EOF
MPC Plaits $REL_VERSION, unofficial 64-bit (aarch64) build.

Port, skin and MPC integration by poloq: https://github.com/poloq-instruments/mpc-vst-plaits
Built on Mutable Instruments Plaits by Emilie Gillet (MIT; panel artwork CC-BY-SA 3.0), schwung-mrhyde by
handcraftedcc, Schwung by charlesvestal and mpc-vst-plugins by sd88me. Full notices: LICENSE (beside this file)
and the README of the repository above.

This build: plaits.so compiled for aarch64 by build-aarch64.sh (BUILD-AARCH64.md in the source tree) from the
v$REL_VERSION source plus the fixes for the 6-op FM models (waveform breaks once per audio block), Chiptune (a released
key kept sounding) and a one-entry table over-read; "Plugin Skins" and version.xml taken unchanged from
$REL_ZIP; plugin.json added for MPC OS 3.10.1's "VST2 Plugins" loader.
Not a poloq release: report problems with this build to whoever gave it to you.
Not affiliated with or endorsed by Akai Professional or Mutable Instruments.
EOF
cat > "$OUT/plugin.json" <<EOF
{
  "name": "MPC Plaits",
  "manufacturer": "poloq",
  "version": "$REL_VERSION",
  "format": "vst2",
  "kind": "instrument",
  "uid": "0x4d69506c",
  "category": "Synth",
  "numInputs": 0,
  "numOutputs": 2,
  "file64": "plaits.so",
  "description": "Mutable Instruments Plaits (all 24 models) as a native instrument, with modulation and a touchscreen skin. Unofficial aarch64 build of poloq's port.",
  "license": "MIT",
  "url": "https://github.com/poloq-instruments/mpc-vst-plaits"
}
EOF

# ---- 5. offline checks -----------------------------------------------------------------------------------------------
python3 -I "$here/tools/check_aarch64.py" "$OUT" "$PARAMS_DIR/params.h" "$UP/plaits.so"
(cd "$B" && shasum -a 256 plaits.so)
echo "built: $OUT"

#!/usr/bin/env bash
# Run the offline tests natively, without Docker (written for an Apple Silicon Mac, i.e. a 64-bit ARM host: the same
# pointer width, alignment rules and NEON as a Gen2 MPC, with clang as in build-aarch64.sh; not the same libc).
#
#   tests/run_native.sh [fuzz blocks, default 20000]
#
# 1. mpc-vst-plugins' tools/host_test.c and this port's tests (controls notes sustain voices voicing modulation
#    deadvoice blockedge chiprelease fuzz), each linked with the wrapper, the Schwung adapter and the engine under ASan + UBSan.
# 2. An optimised native plugin library run through the MPC Plugin Builder SDK's host_sim (if the SDK is at
#    ../mpc-plugin-builder-sdk or $MPC_SDK), which also writes vst/build/native/host_sim.wav.
# Uses the params.h that build-aarch64.sh generated (run that first). Output in vst/build/native/ (git-ignored).
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
if [ -z "${MPC_VST:-}" ]; then
  for d in "$here/../mpc-vst-plugins" "$here/../mpc-vst-plugins-poloq-dev"; do
    [ -f "$d/wrapper/vst2_wrap.c" ] && MPC_VST="$d" && break
  done
fi
MV="$(cd "${MPC_VST:?need an mpc-vst-plugins checkout (set MPC_VST)}" && pwd)"
SDK="${MPC_SDK:-$here/../mpc-plugin-builder-sdk}"
PARAMS_DIR="$here/vst/build/aarch64/gen/vst/build"
[ -f "$PARAMS_DIR/params.h" ] || { echo "run ./build-aarch64.sh first (params.h)" >&2; exit 1; }
CC="${CC:-cc}"; CXX="${CXX:-c++}"
N="${NATIVE_DIR:-$here/vst/build/native}"   # XFLAGS: extra compile+link flags, e.g. XFLAGS="-arch x86_64" NATIVE_DIR=vst/build/native-x86 (Rosetta)
read -r -a XF <<< "${XFLAGS:-}"
rm -rf "$N"; mkdir -p "$N/san" "$N/opt"
cd "$here"

SOURCES=()
while IFS= read -r l; do SOURCES+=("$l"); done < <(python3 -I -c 'import json,sys; print("\n".join(json.load(open(sys.argv[1]))["build"]["sources"]))' vst/vst.json)
CFLAGS=()
while IFS= read -r l; do CFLAGS+=("$l"); done < <(python3 -I -c 'import json,sys; print("\n".join(json.load(open(sys.argv[1]))["build"]["cflags"]))' vst/vst.json)
INC=(-I"$PARAMS_DIR" -I"$MV/wrapper")
# gcc's -fsanitize=undefined (what tests/run_c.sh uses) has no float-cast-overflow check; clang's does, and it
# fires in Plaits' output stage under the fuzzer (BUILD-AARCH64.md, "Found on the way"). STRICT=1 leaves it on.
SAN=(${XF[@]+"${XF[@]}"} -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer -g -O1)
[ -n "${STRICT:-}" ] || SAN+=(-fno-sanitize=float-cast-overflow)
LIBS=(-lm -lpthread)
if [ "$(uname -s)" = Darwin ]; then   # the wrapper's optional engine hooks are weak symbols: ld64 wants them named
  LIBS+=(-Wl,-U,_mpc_engine_changed -Wl,-U,_mpc_engine_sample_draw)
else
  LIBS+=(-ldl)
fi

build_objs() {   # build_objs <dir> <flags...>  -> OBJS
  local dir="$1"; shift
  OBJS=()
  for f in "${SOURCES[@]}"; do
    o="$dir/${f//\//_}.o"
    "$CXX" "$@" -w "${CFLAGS[@]}" "${INC[@]}" -c "$f" -o "$o" &
    OBJS+=("$o")
    while [ "$(jobs -r | wc -l)" -ge 8 ]; do sleep 0.1; done
  done
  for f in "$MV/wrapper/vst2_wrap.c" "$MV/adapters/schwung/schwung_engine.c"; do
    o="$dir/$(basename "$f").o"
    "$CC" "$@" -w -std=gnu11 "${INC[@]}" -c "$f" -o "$o" &
    OBJS+=("$o")
  done
  local fail=0
  for j in $(jobs -p); do wait "$j" || fail=1; done
  [ "$fail" = 0 ] || { echo "compile failed" >&2; exit 1; }
}

# ---- 1. sanitizer tests ------------------------------------------------------------------------------------------------
build_objs "$N/san" "${SAN[@]}"
export ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=print_stacktrace=1
failed=()
run_test() {   # run_test <name> <source.c> [args...]
  local name="$1" src="$2"; shift 2
  "$CC" "${SAN[@]}" -w -std=gnu11 "${INC[@]}" -c "$src" -o "$N/san/test_$name.o"
  "$CXX" "${SAN[@]}" "${OBJS[@]}" "$N/san/test_$name.o" "${LIBS[@]}" -o "$N/san/$name"
  if "$N/san/$name" "$@" > "$N/san/$name.log" 2>&1; then
    echo "PASS $name $*  ($(grep -c '^ok  ' "$N/san/$name.log" || true) ok lines, $(grep -c 'FAIL' "$N/san/$name.log" || true) FAIL lines)"
  else
    echo "FAIL $name $*  (exit $?; see $N/san/$name.log)"; failed+=("$name")
  fi
}
run_test host_test "$MV/tools/host_test.c"
for t in controls sustain voices voicing modulation deadvoice blockedge chiprelease; do run_test "$t" "tests/$t.c"; done
run_test notes tests/notes.c -1
run_test fuzz tests/fuzz.c "${1:-20000}" 1

# ---- 2. optimised native library through the SDK's host_sim ------------------------------------------------------------
if [ -x "$SDK/.sdk-tools/host_sim" ]; then
  build_objs "$N/opt" ${XF[@]+"${XF[@]}"} -O3 -fPIC -fvisibility=hidden
  "$CXX" ${XF[@]+"${XF[@]}"} -O2 -shared -fPIC "${OBJS[@]}" "${LIBS[@]}" -o "$N/plaits-native.dylib"
  "$SDK/.sdk-tools/host_sim" "$N/plaits-native.dylib" test --seconds 4 --notes 48,60,67 --wav "$N/host_sim.wav" > "$N/host_sim.log" 2>&1 || true
  # host_sim expects every parameter back from a saved chunk. The 13 hidden "<key>__open" popup flags ("... List") are
  # screen state the wrapper keeps out of the chunk on purpose (host_test: "popup flag not saved in the chunk"), so
  # those lines, and the summary line that counts them, are expected; anything else is a failure.
  other=$(grep '^FAIL' "$N/host_sim.log" | grep -v -e ' List": saved ' -e 'state round-trip: ' || true)
  popups=$(grep -c '^FAIL.* List": saved ' "$N/host_sim.log" || true)
  if grep -q '^Result:' "$N/host_sim.log" && [ -z "$other" ]; then
    echo "PASS host_sim  ($(grep -c '^PASS' "$N/host_sim.log") checks passed; $popups popup flag(s) not restored from the chunk, by design; $(grep -o 'peak.*dBFS' "$N/host_sim.log" | head -n 1))"
  else
    echo "FAIL host_sim  (see $N/host_sim.log)"; echo "$other"; failed+=(host_sim)
  fi
  # the port's own renderer on the same library: every model, a fixed phrase, float32 stereo. A reference to compare with
  # the device build's output (tools/compare_raw.py).
  "$CC" ${XF[@]+"${XF[@]}"} -O2 -w -std=gnu11 "${INC[@]}" tests/render.c -o "$N/render"
  "$N/render" "$N/plaits-native.dylib" "$N/render.raw" >/dev/null && echo "render reference: $N/render.raw"
else
  echo "SKIP host_sim  (no MPC Plugin Builder SDK at $SDK)"
fi

[ "${#failed[@]}" = 0 ] || { echo "failed: ${failed[*]}"; exit 1; }
echo "all native tests passed"

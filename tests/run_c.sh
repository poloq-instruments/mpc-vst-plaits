#!/usr/bin/env bash
# Build ${TEST:-tests/fuzz.c} against the port (ASan/UBSan, x86, gcc:12 in Docker) and run it: tests/run_fuzz.sh [blocks] [seed]
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
MV="${MPC_VST:-$here/../mpc-vst-plugins}"
python3 "$MV/tools/gen_vst.py" "$here/vst/vst.json" --params-h >/dev/null
eval "$(python3 "$MV/tools/gen_vst.py" "$here/vst/vst.json" --shell)"
SOURCES="$SOURCES ${EXTRA:-}"; export SOURCES CFLAGS LIBS MV
IMG=gcc:12; PLAT=""; BUILD=tests/build
if [ "${ARCH:-}" = arm ]; then IMG=arm32v7/gcc:12; PLAT="--platform linux/arm/v7"; BUILD=tests/build-arm; fi
export BUILD
docker run --rm -e ASAN_OPTIONS=detect_leaks=0 $PLAT -u "$(id -u):$(id -g)" -v "$here":"$here" -v "$MV":"$MV":ro -w "$here" -e TEST -e BUILD -e XFLAGS -e SOURCES -e CFLAGS -e LIBS -e MV $IMG bash -c '
set -e; SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"; OBJS=""; mkdir -p $BUILD
for f in $SOURCES; do o="$BUILD/${f//\//_}.o"; [ "$o" -nt "$f" ] || g++ $SAN $CFLAGS $XFLAGS -Ivst/build -c "$f" -o "$o"; OBJS="$OBJS $o"; done
for f in ${TEST:-tests/fuzz.c} "$MV/wrapper/vst2_wrap.c" "$MV/adapters/schwung/schwung_engine.c"; do o="$BUILD/$(basename "$f").o"; gcc $SAN -std=gnu11 -Ivst/build -c "$f" -o "$o"; OBJS="$OBJS $o"; done
g++ $SAN $OBJS $LIBS -o $BUILD/$(basename ${TEST:-tests/fuzz.c} .c)'"
$BUILD/$(basename ${TEST:-tests/fuzz.c} .c) ${1:-200000} ${2:-1} ${3:-999999999}"
'

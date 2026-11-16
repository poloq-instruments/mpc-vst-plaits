#!/usr/bin/env bash
# The 32-bit (armv7, "Gen1") build of build-aarch64.sh: same sources, same compiler (zig), output in vst/build/armv7/.
exec "$(cd "$(dirname "$0")" && pwd)/build-aarch64.sh" armv7

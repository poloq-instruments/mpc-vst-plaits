#!/usr/bin/env bash
# Grab what the MPC's screen shows right now as a 1280x800 PNG in docs/screenshots/<name>.png.
#   tools/screenshot.sh <name> [ssh-host]      (default host: mpc)
# MPC draws through the GPU, so /dev/fb0 stays black: this reads the display plane with ffmpeg's kmsgrab instead.
# Needs an ffmpeg with kmsgrab on the device ($FFMPEG; the default is the static one Crate Digger installs).
# Nothing is installed; the frame goes through /tmp on the device and is removed.
set -euo pipefail
name="${1:?usage: tools/screenshot.sh <name> [ssh-host]}"; host="${2:-mpc}"
here="$(cd "$(dirname "$0")/.." && pwd)"
out="$here/docs/screenshots/$name.png"
FFMPEG="${FFMPEG:-/media/az01-internal/Synths/sd88me - VST - Crate Digger/cratedigger/bin/ffmpeg}"
mkdir -p "$(dirname "$out")"
ssh -o LogLevel=ERROR "$host" "\"$FFMPEG\" -hide_banner -loglevel error -y -device /dev/dri/card0 -f kmsgrab -i - \
    -vf 'hwdownload,format=bgr0,transpose=1' -frames:v 1 /tmp/mpc-shot.png"
scp -q -o LogLevel=ERROR "$host:/tmp/mpc-shot.png" "$out"
ssh -o LogLevel=ERROR "$host" 'rm -f /tmp/mpc-shot.png'
echo "$out"

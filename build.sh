#!/usr/bin/env bash
# Build the Plaits port as an MPC OS VST2 instrument via mpc-vst-plugins' generic port builder
# (vst/vst.json). DSP source is vendored directly in src/ (see src/VENDORED.md for exactly what
# it is and where it came from) -- no network fetch, fully self-contained given a sibling
# mpc-vst-plugins checkout for the shared wrapper/tools/adapters.
# Needs a sibling checkout of https://github.com/sd88me/mpc-vst-plugins on its poloq-dev branch (HAS_TRANSPORT,
# popup wheel=1, settle(), per-column Q-Link bounds; not upstream yet) -- set MPC_VST if it's not at ../mpc-vst-plugins.
#   vst/build/plaits.so, vst/build/skin/<folder>/, vst/build/pluginlist-entry.xml
#   -> packaged as one plugin folder and installed by install_mpc_one.sh
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
MPC_VST="${MPC_VST:-$here/../mpc-vst-plugins}"
[ -f "$MPC_VST/tools/build_port.sh" ] || { echo "need an mpc-vst-plugins checkout (MPC_VST)" >&2; exit 1; }
exec "$MPC_VST/tools/build_port.sh" "$here/vst/vst.json"

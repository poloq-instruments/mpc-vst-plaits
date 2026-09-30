#!/usr/bin/env bash
# Package MPC Plaits as a release zip (mpc-vst-plugins tools/release.py) and install it on the MPC One reachable as
# `ssh mpc`, as ONE plugin folder in /media/az01-internal/Synths (ext4: the SD card is noexec, so the .so can't live
# there; that folder must be listed in MPC.settings' SynthContentLocations). The zip's own install.sh stops MPC, backs
# up MPC.settings, replaces the plugin-list entry (same uid) and starts MPC again. Leftovers of the old split install
# (/media/az01-internal/vst/plaits.so, the skin on the SD card) are moved to /tmp on the device.
#   ./install_mpc_one.sh [--bench bench.json]      (run ./build.sh first)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
MPC_VST="${MPC_VST:-$here/../mpc-vst-plugins}"
B="$here/vst/build"
SYNTHS=/media/az01-internal/Synths
eval "$(python3 -c 'import json,sys; v=json.load(open(sys.argv[1]))
print("SKIN=%s" % repr("%s - VST - %s" % (v["vendor"], v["name"])))
n=v["version"]; print("VERSION=%d.%d.%d" % (n // 1000, n // 100 % 10, n % 100))' "$here/vst/vst.json")"
[ -f "$B/plaits.so" ] && [ -d "$B/skin/$SKIN" ] || { echo "build first (./build.sh)" >&2; exit 1; }

dist=$(mktemp -d); trap 'rm -rf "$dist"' EXIT
python3 "$MPC_VST/tools/release.py" --so "$B/plaits.so" --skin "$B/skin/$SKIN" --entry "$B/pluginlist-entry.xml" \
    --version "$VERSION" --id mpc-plaits --repo poloq-instruments/mpc-vst-plaits --license MIT \
    --about "Mutable Instruments Plaits (all 24 models) as a native instrument, with modulation and a touchscreen skin." \
    "$@" -o "$dist"
zip=$(ls "$dist"/*.zip); pkg=$(basename "$zip" -mpc-armv7.zip)
scp -q "$zip" mpc:/tmp/

ssh mpc "PKG='$pkg' SYNTHS='$SYNTHS' sh -s" <<'EOF'
set -e
grep -q "<Location>$SYNTHS</Location>" /media/az01-internal/Settings/MPC/MPC.settings ||
    { echo "$SYNTHS isn't in MPC's SynthContentLocations" >&2; exit 1; }
cd /tmp && rm -rf "$PKG" && unzip -q "$PKG-mpc-armv7.zip" && rm -f "$PKG-mpc-armv7.zip"
sh "$PKG/install.sh" -y -t "$SYNTHS"
old=/tmp/plaits-old-$(date +%Y%m%d-%H%M%S); mkdir -p "$old"
for f in /media/az01-internal/vst/plaits.so /media/*/Synths/"sd88me - VST - MPC Plaits"; do
    [ -e "$f" ] && mv "$f" "$old/" && echo "moved old $f to $old"
done
rm -rf "/tmp/$PKG"
EOF

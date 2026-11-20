#!/bin/bash
# Ф1 step 2: the helper firmware on a PC boots CRISS CP/M from the .kg data ROM and a floppy image,
# writes a file and runs ZORK1 (a few seconds). Needs: make roms (src/roms/kg/criss_data.bin) and
# the CRISS1.cpm image of the CRISS site (default ~/Downloads/karabas-sd-criss, local only).
#   fw/host/test.sh [CRISS1.cpm]
set -e
cd "$(dirname "$0")/../.."
IMG=${1:-$HOME/Downloads/karabas-sd-criss/CRISS1.cpm}
make -s -C fw/host
T=$(mktemp -d)
trap 'rm -r "$T"' EXIT
cp "$IMG" "$T/a.cpm"
fw/build-host/criss_host --rom roms/kg/criss_data.bin --disk "A:$T/a.cpm" \
	--keys 'DIR\rSAVE 1 T.COM\rDIR T.*\rZORK1\r' --steps 60000000 > "$T/out.txt"
cat "$T/out.txt"
ok=1
for s in 'XONIX    COM' 'A: T        COM' 'West of House'; do
	grep -qF "$s" "$T/out.txt" || { echo "MISSING: $s"; ok=0; }
done
[ $ok = 1 ] && echo PASS || { echo FAIL; exit 1; }

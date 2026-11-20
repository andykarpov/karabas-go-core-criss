#!/bin/bash
# Ф2 console tests on the PC build (seconds): t1..t4.com on a fresh image, each run to its end, the
# text screen checked. With --png <dir>: also the frames through criss_video (sim/video).
#   sim/tests/run.sh [--png DIR]
set -e
cd "$(dirname "$0")"
T=$(pwd); SRC=$(cd ../.. && pwd)
W=$(mktemp -d); trap 'rm -r "$W"' EXIT
for t in t1 t2 t3 t4; do /opt/sjasmplus/sjasmplus --nologo --msg=err --outprefix=$W/ $t.asm; done
python3 -I $SRC/tools/cpmimg.py new $W/tst.cpm
python3 -I $SRC/tools/cpmimg.py add $W/tst.cpm $W/t1.com $W/t2.com $W/t3.com $W/t4.com > /dev/null
make -s -C $SRC/fw/host
fail=0
check() {   # test, strings...
	local t=$1; shift
	$SRC/fw/build-host/criss_host --rom $SRC/roms/kg/criss_data.bin --disk A:$W/tst.cpm --keys "${t^^}\\r" --steps 30000000 > $W/$t.txt
	for s in "$@"; do grep -qF -- "$s" $W/$t.txt || { echo "$t: MISSING '$s'"; fail=1; }; done
}
check t1 'red   green yellow blue' 'inside the window' 'row 7, col 11 (ESC =)' 'STATUS: bottom line 24' 'after the status line, row 10'
check t2 'screen 80x24 cp 1'
check t3 'screen 64x30 cp 0' 'row 30: last line'
check t4 'screen 80x24 cp 7' 'row 3: CP3 Robotron EN'
if [ "$1" = "--png" ]; then
	for t in t1 t2 t3 t4; do IMG=$W/tst.cpm $SRC/sim/video/run.sh "$2/$t.png" --keys "${t^^}\\r" --steps 30000000 > /dev/null; done
	echo "frames in $2"
fi
[ $fail = 0 ] && echo PASS || { echo FAIL; exit 1; }

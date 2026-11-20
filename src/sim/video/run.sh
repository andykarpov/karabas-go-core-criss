#!/bin/bash
# criss_video: a frame of a screen of the PC build (fw/host) as a PNG.
#   sim/video/run.sh <out.png> [host options, e.g. --keys 'DIR\r'] (the image: IMG=..., default CRISS1.cpm)
set -e
cd "$(dirname "$0")"
T=$(pwd); SIM=$(cd .. && pwd); SRC=$(cd ../.. && pwd)
OUT=$(realpath "$1"); shift
B=$SIM/build/video
mkdir -p $B
make -s -C $SRC/fw/host
IMG=${IMG:-$HOME/Downloads/karabas-sd-criss/CRISS1.cpm}
cp "$IMG" $B/a.cpm
$SRC/fw/build-host/criss_host --rom $SRC/roms/kg/criss_data.bin --disk A:$B/a.cpm --dump-mem $B/mem.bin "$@" > $B/host.txt
BIN=$B/obj/Vcriss_video
if [ ! -x $BIN ] || [ -n "$(find $SRC/rtl/criss_video.v tb_video.cpp run.sh -newer $BIN 2>/dev/null)" ]; then
	verilator -O2 -Wno-fatal -Wno-lint -Wno-style --no-timing --top-module criss_video -Mdir $B/obj --cc \
		$SRC/rtl/criss_video.v $SRC/../modules/rtl/mem/kg_sdpram.v --exe $T/tb_video.cpp -CFLAGS "-std=c++17 -O2" -o Vcriss_video > $B/verilator.log 2>&1 \
		|| { grep -E '%Error' $B/verilator.log | head; exit 1; }
	make -s -j8 -C $B/obj -f Vcriss_video.mk > $B/make.log 2>&1 || { tail -20 $B/make.log; exit 1; }
fi
$BIN $B/mem.bin $SRC/roms/kg/criss_data.bin $B/frame.ppm ${VSTAT:-}
python3 -I -c "from PIL import Image; Image.open('$B/frame.ppm').save('$OUT')"
echo "$OUT"

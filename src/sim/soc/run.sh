#!/bin/bash
# Ф1 step 3: the SoC RTL boots CRISS CP/M from the helper firmware and a floppy image (verilator).
#   sim/soc/run.sh [--z80div N] [tb_soc options: --until s --keys s --then s --max M]
# Needs fw/build/criss_fw.bin (make -C fw), roms/kg/criss_data.bin (make roms), the image
# ~/Downloads/karabas-sd-criss/CRISS1.cpm (or IMG=...).
set -e
cd "$(dirname "$0")"
T=$(pwd)
SIM=$(cd .. && pwd)
SRC=$(cd ../.. && pwd)
DIV=18
if [ "$1" = "--z80div" ]; then DIV=$2; shift 2; fi
B=$SIM/build/soc$DIV
mkdir -p $B/v
make -s -C $SRC/fw > /dev/null
if [ ! -f $B/vhdl_ghdl.v ] || [ -n "$(find $SRC/rtl/T80 $SIM/vhdl2v.sh -newer $B/vhdl_ghdl.v 2>/dev/null)" ]; then
	$SIM/vhdl2v.sh $B > $B/ghdl.log 2>&1 || { grep -v -i warning $B/ghdl.log | tail -30; exit 1; }
fi
BIN=$B/obj/Vsim_top
VLOG="$T/sim_top.v $SRC/rtl/criss_soc.v $SRC/rtl/criss_sd.v $SRC/rtl/criss_z80.v $SRC/rtl/doom_sdram.v $SRC/rtl/vexriscv/VexRiscv.v"
if [ ! -x $BIN ] || [ -n "$(find $B/vhdl_ghdl.v $VLOG tb_soc.cpp run.sh -newer $BIN 2>/dev/null)" ]; then
	rm -f $B/v/*.v
	python3 -I $SIM/fixcase.py $B/vhdl_ghdl.v $B/v $VLOG
	verilator -O3 -Wno-fatal -Wno-lint -Wno-style -Wno-BLKANDNBLK -Wno-MULTIDRIVEN -Wno-COMBDLY -Wno-UNOPTFLAT \
		--no-timing +1364-2005ext+v --top-module sim_top -GZ80_DIV=$DIV -Mdir $B/obj --cc $B/v/*.v \
		--exe $T/tb_soc.cpp -CFLAGS "-std=c++17 -O2" -MAKEFLAGS "OPT_FAST=-O3 OPT_SLOW=-O1" -o Vsim_top > $B/verilator.log 2>&1 \
		|| { grep -E '%Error' $B/verilator.log | head -30; exit 1; }
	make -s -j8 -C $B/obj -f Vsim_top.mk > $B/make.log 2>&1 || { tail -30 $B/make.log; exit 1; }
fi
IMG=${IMG:-$HOME/Downloads/karabas-sd-criss/CRISS1.cpm}
exec $BIN $SRC/fw/build/criss_fw.bin $SRC/roms/kg/criss_data.bin "$IMG" "$@"

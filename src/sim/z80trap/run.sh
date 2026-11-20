#!/bin/bash
# Test of criss_z80 (T80s + BIOS traps): verilator model + sim_trap.cpp, Z80 program trap_test.asm.
#   sim/z80trap/run.sh
set -e
cd "$(dirname "$0")"
T=$(pwd)
SIM=$(cd .. && pwd)
SRC=$(cd ../.. && pwd)
B=$SIM/build/z80trap
mkdir -p $B/v
/opt/sjasmplus/sjasmplus --nologo --msg=err trap_test.asm
if [ ! -f $B/vhdl_ghdl.v ] || [ -n "$(find $SRC/rtl/T80 $SIM/vhdl2v.sh -newer $B/vhdl_ghdl.v 2>/dev/null)" ]; then
	$SIM/vhdl2v.sh $B > $B/ghdl.log 2>&1 || { grep -v -i warning $B/ghdl.log | tail -30; exit 1; }
fi
BIN=$B/obj/Vcriss_z80
VLOG="$SRC/rtl/criss_z80.v"
if [ ! -x $BIN ] || [ -n "$(find $B/vhdl_ghdl.v $VLOG sim_trap.cpp run.sh -newer $BIN 2>/dev/null)" ]; then
	rm -f $B/v/*.v
	python3 -I $SIM/fixcase.py $B/vhdl_ghdl.v $B/v $VLOG
	verilator -O2 -Wno-fatal -Wno-lint -Wno-style -Wno-BLKANDNBLK -Wno-MULTIDRIVEN -Wno-COMBDLY -Wno-UNOPTFLAT \
		--no-timing +1364-2005ext+v --top-module criss_z80 -Mdir $B/obj --cc $B/v/*.v \
		--exe $T/sim_trap.cpp -CFLAGS "-std=c++17 -O2" -o Vcriss_z80 > $B/verilator.log 2>&1 \
		|| { grep -E '%Error' $B/verilator.log | head -30; exit 1; }
	make -s -j8 -C $B/obj -f Vcriss_z80.mk > $B/make.log 2>&1 || { tail -30 $B/make.log; exit 1; }
fi
exec $BIN trap_test.bin "$@"

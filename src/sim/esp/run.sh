#!/bin/bash
# criss_esp loopback test (verilator, seconds)
set -e
cd "$(dirname "$0")"
B=../build/esp
verilator -Wno-fatal --cc ../../rtl/criss_esp.v --top-module criss_esp --exe $(pwd)/tb_esp.cpp -Mdir $B -o tb > /dev/null
make -s -C $B -f Vcriss_esp.mk > /dev/null
$B/tb && echo PASS

# The CRISS helper firmware on a PC (Ф1 step 2). make -C src/fw/host; ./build/criss_host --rom ...
B ?= ../build-host
SRC = ../criss.c ../con.c ../clr.c ../kbd.c host.c
$(B)/criss_host: $(SRC) ../*.h z80/z80.c z80/z80.h Makefile
	mkdir -p $(B)
	cc -std=gnu11 -O2 -g -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -o $@ $(SRC)

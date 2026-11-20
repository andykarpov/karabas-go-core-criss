# The helper firmware for the VexRiscv of the core (xPack riscv-none-elf-gcc): build/criss_fw.bin = the
# first ROM of the .kg (SDRAM 0x10000000). The PC build of the same sources: host/.
XPACK   ?= /opt/xpack-riscv-none-elf-gcc-15.2.0-1/bin
CROSS   := $(XPACK)/riscv-none-elf-
B       := build
ABI     := -march=rv32im -mabi=ilp32
CFLAGS  := $(ABI) -Os -g -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter -I.
LDFLAGS := $(ABI) -nostartfiles --specs=nano.specs -Wl,--gc-sections -T link.ld
SRC     := criss.c con.c clr.c kbd.c hw_rv.c
OBJ     := $(addprefix $(B)/,$(SRC:.c=.o)) $(B)/crt0.o

all: $(B)/criss_fw.bin

$(B)/%.o: %.c *.h Makefile
	mkdir -p $(B)
	$(CROSS)gcc $(CFLAGS) -c $< -o $@
$(B)/crt0.o: crt0.S
	mkdir -p $(B)
	$(CROSS)gcc $(ABI) -c $< -o $@
$(B)/criss_fw.elf: $(OBJ) link.ld
	$(CROSS)gcc $(LDFLAGS) -o $@ $(OBJ) -lgcc
	$(CROSS)size $@
$(B)/criss_fw.bin: $(B)/criss_fw.elf
	$(CROSS)objcopy -O binary $< $@

clean:
	rm -rf $(B)
.PHONY: all clean

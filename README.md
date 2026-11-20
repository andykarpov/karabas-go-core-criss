# CRISS CP/M core for Karabas Go / Mini / Mini G

[Русская версия](README.ru.md)

A core of [CRISS CP/M](https://criss.radio.ru/) (КРИСС CP/M) for the Karabas Go board family (Xilinx Spartan-6 XC6SLX25). CRISS CP/M is a DIY computer by I. S. Reshetnikov, published in the magazine «Радио» in 2021–2024. It is a Z80 CP/M 2.2 machine with an 80-column text screen, 8 colours, sound, a clock and floppy images on an SD card. The original has no Z80 chip: an ATmega1284P emulates it and other AVR microcontrollers drive the keyboard, sound and VGA. This core is written from scratch. It has a real Z80 (T80) and a small RISC-V CPU that does the job of the AVR firmware.

| Board | Video | Sound | Network |
|---|---|---|---|
| Karabas Go | VGA | I2S DAC (PCM5102) | the ESP8266 of the board |
| Karabas Mini | HDMI | HDMI, analog output (sigma-delta) | the ESP8266 of the board |
| Karabas Mini G | HDMI | HDMI, I2S DAC | the ESP8266 of the board |

The core file `src/criss.kg` is one file for all three boards.

Status: checked on Mini G (CP/M, programs, console, ports, platforms, snapshots, network). The Go and Mini builds have not been run on a board yet.

Contents: [Getting started](#getting-started) · [OSD menu](#osd-menu) · [Keys](#keys) · [Disks](#disks) · [The machine](#the-machine) · [Network](#network) · [How the core works](#how-the-core-works) · [Source tree](#source-tree) · [Building](#building) · [Tests](#tests) · [Differences from the original](#differences-from-the-original) · [Licence and sources](#licence-and-sources)

## Getting started

1. Copy `src/criss.kg` to the root of the SD card.
2. Make the directory `/criss` on the card. Put floppy images (`.cpm`) there. The images of the CRISS site are a good start: [CRISS1.cpm](https://criss.radio.ru/criss/img/CRISS1.cpm) (the system disk of the site: utilities, games such as Zork), [CRISS2.cpm](https://criss.radio.ru/criss/img/CRISS2.cpm), [tm2.cpm](https://criss.radio.ru/criss/img/tm2.cpm), [G1715.cpm](https://criss.radio.ru/criss/img/G1715.cpm). More programs are on the [software page](https://criss.radio.ru/?soft).
3. For settings, RAM snapshots and the network you also need an NVRAM image in `/criss`. Make an empty one with `make nvr` (gives `src/roms/kg/criss.nvr`) or `python3 src/tools/nvrset.py criss.nvr --new`, and copy it to `/criss/criss.nvr`.
4. Choose CRISS CP/M in the core menu. Open the OSD (**Menu+Esc**) and choose the disk A image (**Menu+F1**) and the NVRAM image. The core remembers them. CP/M starts at once: `a>`. CP/M itself is in the core (it is the boot image of CRISS); the floppy is only needed for the programs.

"Menu" is the Windows key of the keyboard.

## OSD menu

| Item | Values | Hotkey |
|---|---|---|
| Disk A | `.cpm` image in `/criss` | Menu+F1 |
| Disk B | `.cpm` image in `/criss` | Menu+F2 |
| NVRAM | `.nvr` image in `/criss`: the settings (96 bytes of the DS1305 RAM) and two 64 KB RAM snapshots | |
| Platform | CRISS / R1715 RU / R1715 EN / Kaypro / TRS-80 / Kaypro 8b / VT100: the terminal the console acts as (see [Platforms](#platforms)); a change clears the screen | |
| Boot | CP/M / Snapshot 1 / Snapshot 2: what starts after a reset | |
| Reset | restarts the machine | F12 |

The board firmware adds "Setup RTC" at the end of the menu: the clock of CRISS is the clock of the board. The original system console of CRISS (Ctrl+Alt+Del) is replaced by this menu and by `nvrset.py`.

## Keys

| Keys | Action |
|---|---|
| Menu+Esc | the OSD |
| F12, Ctrl+Alt+Shift+Esc | reset |
| Ctrl+Alt+F1 | save the running task in snapshot 1, switch to snapshot 2 |
| Ctrl+Alt+F2 | save the running task in snapshot 2, switch to snapshot 1 |
| Scroll Lock | Latin / Russian layout (as the CRISS keyboard controller) |
| Caps Lock | capitals |
| Num Lock | Robotron platforms: s0 / s1 (Latin / Cyrillic of KOI-7) |

The keyboard follows the tables of the CRISS peripheral controller (firmware 0.24): the Russian layout gives CP866 (CRISS) or KOI-7 (Robotron).

The two snapshots work like the A / B blocks of the 25LC1024 chip of CRISS: you can keep two programs and jump between them. They live in the NVRAM image; without it Ctrl+Alt+F1 / F2 do nothing. The "Boot" item starts a snapshot instead of CP/M.

## Disks

A `.cpm` image is a CRISS floppy: 819 200 bytes, 80 tracks of 80 sectors of 128 bytes, 2 system tracks, 2 KB blocks, 128 directory entries. It is the same format as the images of the CRISS site and as the Robotron 1715 (cpmtools: diskdef `ris1715`).

To make, list and fill images on a PC use `src/tools/cpmimg.py` (Python 3, no packages):

```
python3 src/tools/cpmimg.py new my.cpm
python3 src/tools/cpmimg.py add my.cpm GAME.COM README.TXT
python3 src/tools/cpmimg.py ls my.cpm
python3 src/tools/cpmimg.py get my.cpm GAME.COM
```

`cpmtools` (diskdef `ris1715`) and the Far plugin of the CRISS site open these files too. Writes in CP/M go to the image file on the SD card at once.

The original CRISS uses a raw SD card without a file system (a card name, up to 239 floppies and 16 boot images in fixed blocks). This core uses files on a FAT card instead. A raw CRISS card is not supported.

## The machine

What the core has of CRISS (firmware versions: CPU 4.67, peripheral controller 0.24, VGA 1.24, colour module 0.5):

- **CPU:** Z80 at 4.17 MHz, 64 KB RAM. All the BIOS functions and the extra calls of the CRISS firmware (`ED xx` traps): CP/M BIOS 0–16, the BDOS shortcut, `MUL`, `DIV`, the 32-bit float operations.
- **Console:** the full ESC terminal of CRISS: modes 80×24, 80×30, 64×24 and others, 8 code pages, windows, the status line, points / lines / circles, pseudo graphics 160×96, the colour module CLR (8 colours per character cell, static or by cells).
- **Platforms:** the console also acts as Robotron 1715 (RU / EN), Kaypro, TRS-80, Kaypro 8 bit and VT100 / ANSI / VT52 (chosen in the OSD), with the I/O ports 00–CF of those machines (IN 0C, OUT 18 / 19).
- **Ports D0–FF:** the registers of the peripheral controller (keyboard, timer, events, interrupts), the banks F4–FD with the system variables, the commands of ports FE / FF (version, FPU).
- **Interrupts:** IM 1 / IM 2 with the vectors and masks of CRISS (the events of the peripheral controller: keyboard, timer, melody, RS-232). The network and clock interrupt lines of CRISS are not raised.
- **Sound:** the bell (BEL) and the melody player of the peripheral controller (ESC m, port D6).
- **Clock:** the DS1305 (FE 80–87) on the clock of the board; its 96 bytes of RAM in the NVRAM image.
- **RS-232:** the USB serial port of the board (the second CDC port of the board, `-if02`), with flow control. Its speed setting is accepted and ignored.
- **Mouse:** the USB mouse of the board (ports D7–DA, events).
- **Network:** see [Network](#network).

Not done: the second UART (FF 37–39: there is no second line on the boards), the I2C GPIO expander (the calls return "error"), CAN, the SPI expansion slot, booting from the 25LC1024 without an SD card.

### Platforms

| OSD value | Machine | Font | Output | Keyboard |
|---|---|---|---|---|
| CRISS | CRISS CP/M | CP866 | all ESC codes of CRISS | CP866 |
| R1715 RU | Robotron 1715, Russian | code page 2 (KOI-7) | Robotron ESC codes, cursor by ESC + 0x7F | Latin upper case, Cyrillic (KOI-7) |
| R1715 EN | Robotron 1715, English | code page 3 | Robotron ESC codes | 7 bit (the Russian keys give nothing) |
| Kaypro | Kaypro | CP866, 7 bit | Kaypro control codes | CP866 |
| TRS-80 | TRS-80 | 7 bit | as CRISS | CP866 |
| Kaypro 8b | Kaypro, 8 bit | CP866 | Kaypro control codes | CP866 |
| VT100 | VT100 / ANSI / VT52 | CP866 | CSI sequences | CP866 |

The platforms are checked on Mini G for CRISS, VT100 and R1715 RU, and on a PC for Kaypro. R1715 EN, TRS-80 and Kaypro 8b are coded from the firmware and not checked.

## Network

The original CRISS has an ENC28J60 Ethernet chip and a frame-level API (port FE, commands 48–79; the bank FA / FB). The Karabas boards have an ESP8266 Wi-Fi module instead. **The core keeps the stock AT firmware of the ESP8266** (1.7.x): it is not reflashed and the core sends it no command that writes its flash, because all cores share it.

### Setting up

1. Connect the board to Wi-Fi once in the boot core: OSD → Setup Wi-Fi. The ESP8266 keeps the network and gets its IP by DHCP.
2. Start CRISS. The core finds the module in a second or two: bit 5 (LAN) of the system variable HW_FOUND is set, and the bank FA / FB gets the IP and MAC of the module as `IP_MY` / `MAC_MY`.
3. For the CP/M channels over the network, put the addresses of the servers into the NVRAM image on a PC (on the original the system console does this):

```
python3 src/tools/nvrset.py criss.nvr                           # show
python3 src/tools/nvrset.py criss.nvr con=192.168.1.10:5000     # the console server
python3 src/tools/nvrset.py criss.nvr prn=5001                  # the network printer port (on the console server)
python3 src/tools/nvrset.py criss.nvr pr=192.168.1.10:5002      # the PUNCH / READER server
python3 src/tools/nvrset.py criss.nvr lst=0x04                  # LST to the network printer at start
python3 src/tools/nvrset.py criss.nvr lan=off                   # switch the network off
```

### What works

The core gives the Z80 the same API as the CRISS firmware 4.67; programs need no change. Inside, the helper CPU turns it into AT commands:

| CRISS (ENC28J60) | In the core |
|---|---|
| UDP: build and send (FE 61, 69, 54) | `AT+CIPSEND` from the UDP link of our port (`PORT_MY` is the source port) |
| UDP: receive (FE 55, 66, 67) | the datagrams of the module (`+IPD`) as full Ethernet / IP / UDP frames to `MAC_MY` / `IP_MY`, with checksums |
| ICMP echo request (FE 70 + 54) | `AT+PING`; the answer comes back as an echo reply frame (class 0x8B, `ECHO_CNTR` + 1) |
| ARP request (FE 68 + 54) | answered by the core: MAC `02:00:<IP>` (class 0x82, `MAC_REMOTE`) — the module does ARP itself |
| frame commands (FE 50, 51, 59, 60, 62, 63, 64, 71) | as in 4.67, on a 768-byte frame buffer |
| ENC28J60 registers (FE 52, 53, 56, 57, 58, 65) | plausible values: link up, `EPKTCNT` = frames waiting, `ESTAT` ready |
| CON / LST / PUNCH / READER over UDP (IOBYTE) | one datagram per character to the server, the server port on both ends (as 4.67) |
| incoming ping, ARP of other hosts | answered by the ESP8266 itself |

The CP/M channels use the IOBYTE bits of CRISS:

| Channel | Variable | Output | Input |
|---|---|---|---|
| CON | CON_IOBYTE (system variable 1) | bit 2: to the console server (bit 0: screen) | bit 6: from the console server |
| LST | LST IOBYTE (system variable 88) | bit 2: to the network printer port | |
| PUNCH / READER | PR_IOBYTE (system variable 72) | bit 2: to the PR server | bit 6: from the PR server (READER does not wait: 0 = nothing) |

For example, `OUT (F6),1 : OUT (F7),55h` sends the console to the console server and takes keys from it too; a PC with `nc -u -p 5000 <CRISS IP> 5000` is then a remote terminal.

### What does not work

- TCP (FE 72): the 4.67 firmware builds broken segments and keeps no state, so no program can use it. The core drops these frames.
- Non-IP frames, raw Ethernet, frames to other MAC addresses: the module has only sockets.
- Access to CRISS from outside a NAT, other than by the router.
- The UDP checksum is 0 (as 4.67). At most 726 data bytes per datagram (the 768-byte frame buffer of CRISS); at most 5 local ports open at once (the oldest is closed).
- Datagrams come only from the moment a program uses its port: the core opens a link when `PORT_MY` is used (FE 48 / 49 / 55 / 56 / 66 / 69) or a channel is switched to the network.

### Inside

- `src/rtl/criss_esp.v` — the UART to the module: 115200 8N1, a 2 KB receive buffer, CTS to the module (the AT firmware runs with hardware flow control).
- `src/fw/esp.c` — the AT engine. It never blocks: one parser takes every byte (lines, the `>` prompt, `+IPD` with data, which may come in the middle of any answer), one command is in flight with a time-out, the rest wait in a queue. At start: `AT` until it answers, `ATE0`, close all links, `AT+CIPMUX=1`, `AT+CIPDINFO=1`, `AT+CIFSR`. A module restart (`ready`) or three time-outs start it again. One UDP link per local port: `AT+CIPSTART=<n>,"UDP","0.0.0.0",0,<port>,2` (listen to everyone), send by `AT+CIPSEND=<n>,<len>,"<ip>",<port>`.
- `src/fw/net.c` — the CRISS side: the bank FA / FB, the frame buffer, the FE commands, taking sent frames apart, making received frames, the classification of 4.67 (0x81–0x8B), the CP/M channels.

## How the core works

```
          SD card (files)           USB keyboard / mouse / serial
                │                               │
        board MCU (RP2040) ── link (kg_mcu2) ── OSD, images, PS/2, port 0, clock
                │
   ┌────────────┴───────────── FPGA ─────────────────────────────┐
   │  T80 Z80 4.17 MHz ── SRAM 64 KB (Z80 RAM, F800+ = screen)   │
   │     │ ED xx / IN / OUT: the Z80 stops (WAIT)                │
   │  helper VexRiscv RV32IM 75 MHz ── SDRAM (program, data ROM) │
   │     │ MMIO: Z80 registers / RAM, video, ports, SD, UART     │
   │  criss_video 640×480 ── OSD ── HDMI / VGA                   │
   │  criss_esp UART ── ESP8266                                  │
   └─────────────────────────────────────────────────────────────┘
```

- **The Z80** (T80) runs the CP/M of CRISS from its 64 KB RAM in the board SRAM. The original firmware serves the BIOS through `ED xx` opcodes and all I/O ports in software. In the core `criss_z80.v` stops the Z80 (WAIT) at such an opcode and at every `IN` / `OUT`, and the helper CPU does the work: it reads and writes the Z80 registers and RAM, then lets it go. Interrupts get their vector from the helper and are acknowledged in hardware.
- **The helper** is a VexRiscv RV32IM soft CPU (from core-doom) with caches. Its program (`src/fw`, C) and a data ROM (the CP/M boot image, the fonts, the keyboard and compatibility tables of the CRISS firmware) are the two ROMs of the `.kg`, loaded into the SDRAM. It plays the parts of the CPU, peripheral, VGA and colour controllers of CRISS: BIOS, console with all ESC codes and platforms, keyboard, timers, melodies, clock, mouse, RS-232, NVRAM, snapshots, network.
- **Video** (`criss_video.v`) reads the screen of CRISS (Z80 RAM F800+, also written by the helper for ESC | ), the 8 fonts and a colour RAM, and makes 640×480 at 60 Hz: character cells of 8×20 / 19 / 16 / 15 pixels by the mode, the cursor, the colours of the CLR module.
- **Floppies and NVRAM** are image files mounted by the board firmware (`kg_l2_img` slots: A 0, B 1, NVRAM 4); `criss_sd.v` gives the helper 512-byte sectors.
- **Board glue** (`modules/`: `kg_mcu2` link v2, OSD overlay, HDMI, PLL, audio) is the shared karabas-go-modules repository.

The helper firmware also runs on a PC (`src/fw/host`): the same C files with a Z80 interpreter instead of the FPGA. All console, port and network tests run there in seconds.

## Source tree

| Path | What |
|---|---|
| `src/criss.kg` | the core file (all boards) |
| `src/core.json` | the core description for `make_core.py`: ROMs, boards, OSD items |
| `src/rtl/karabas_minig_top.v` | the Mini G top: clocks, `kg_mcu2`, SoC, video, audio, HDMI |
| `src/rtl/karabas_go_top.v`, `karabas_mini_top.v` | made from the Mini G top by `src/tools/mktops.py` (do not edit) |
| `src/rtl/criss_soc.v` | the SoC: T80, VexRiscv, SDRAM / SRAM, the MMIO registers (their map is in the file header) |
| `src/rtl/criss_z80.v` | the Z80 trap unit: stop at `ED xx` and at I/O, freeze, registers in / out |
| `src/rtl/criss_video.v` | the text video, fonts, colour RAM |
| `src/rtl/criss_sd.v` | the image slots for the helper |
| `src/rtl/criss_esp.v` | the UART to the ESP8266 |
| `src/rtl/T80/` | T80 (VHDL), with the karabas changes (REG / DIR access, marked `karabas:`) |
| `src/rtl/vexriscv/`, `doom_sdram.v` | the helper CPU and the SDRAM controller of core-doom |
| `src/fw/criss.c` | boot, BIOS traps, IOBYTE channels, the main loop |
| `src/fw/con.c`, `clr.c` | the console (ESC codes, modes, windows, graphics, platforms) and the CLR colour module |
| `src/fw/io.c`, `io_ext.c`, `periph.c` | the ports: peripheral controller registers, banks, system variables, FE / FF, interrupts, ports 00–CF, RS-232, mouse |
| `src/fw/kbd.c`, `sound.c`, `nvram.c` | keyboard (PS/2 → CRISS codes), melodies, clock / NVRAM / snapshots |
| `src/fw/esp.c`, `net.c` | the network (above) |
| `src/fw/hal.h`, `hw_rv.c` | the hardware interface and its FPGA side (`host/host.c`: the PC side) |
| `src/fw/rom.h` | the layout of the data ROM |
| `src/tools/mkroms.py` | makes the data ROM from the upstream firmware files |
| `src/tools/cpmimg.py` | `.cpm` images: new / ls / add / get |
| `src/tools/nvrset.py` | `.nvr` images: the settings (network servers, LST, LAN on / off) |
| `src/make_core.py`, `src/tools/mktops.py` | `.kg` packer (copy of karabas-go-tools), Go / Mini tops |
| `src/sim/` | tests (below) |
| `docs/PLAN.md` | decisions and the history of the work (Russian) |
| `docs/re/` | the disassembly of the CRISS firmware: CPU (`cpu-core.md`, `cpu-io.md`), peripheral / VGA / CLR (`periph.md`) — the port map, BIOS, ESC codes, network API (Russian) |
| `upstream/` | the files of the CRISS site: firmware, manuals, schematic (`tools/fetch_upstream.sh`) |
| `modules/` | karabas-go-modules (git submodule) |

## Building

Needs: Xilinx ISE 14.7 (`/opt/Xilinx/14.7`), the xPack RISC-V GCC (`riscv-none-elf-gcc` 15.2, path in `src/fw/Makefile`), Python 3, `git submodule update --init`.

```
cd src
make fw          # the helper firmware (fw/build/criss_fw.bin)
make roms        # the data ROM from upstream/ (tools/fetch_upstream.sh fetches it)
make minig       # a board: XST, map, PAR, bitgen, then criss.kg
make             # all boards (go, mini, minig) and criss.kg
make minig-report
make nvr         # an empty NVRAM image
```

`make` packs `criss.kg` with every board built so far. A change of the Mini G top needs `python3 tools/mktops.py` for the Go / Mini tops.

## Tests

| Command | What | Time |
|---|---|---|
| `src/sim/tests/run.sh` | the firmware on a PC with a Z80 interpreter: console (t1–t4), ports / FPU / timer / interrupts / clock (t5), platforms (t9), registers across traps (t10), network (t11 against a fake ESP8266 on 127.0.0.2 and `netpeer.py`), snapshots. `--png DIR` also draws frames | ~30 s |
| `src/fw/host/test.sh` | CP/M on `CRISS1.cpm`: DIR, SAVE, ZORK1 | seconds |
| `src/sim/soc/run.sh` | the whole SoC in verilator (T80 by ghdl, VexRiscv, SDRAM model) boots CP/M; `--keys`, `--then` | ~40 s |
| `src/sim/esp/run.sh` | the ESP8266 UART in a loop | seconds |
| `src/sim/z80trap/`, `src/sim/video/` | the trap unit, video frames to PNG | seconds |

Needs: sjasmplus (`/opt/sjasmplus`), verilator, ghdl. The test programs are `src/sim/tests/t*.asm`; t11 takes the peer address by `-DP1=.. -DP4=..` for a check on a board against a PC.

## Differences from the original

The core follows the firmware 4.67 in what programs see, with these exceptions:

- The Z80 is a real Z80 (T80) at 4.17 MHz: the instructions behave as the chip, not as the AVR emulator.
- Fixed bugs of 4.67: `MUL` (`ED 77`), FF 39 (UART1 sent a wrong byte), ESC g, the network buffer overflow (FE 50 > 768 bytes), datagrams cut to 22 bytes on the CON / READER channels.
- 64-column modes show all 64 columns.
- No system console: the OSD and `nvrset.py` instead. The settings, the clock RAM and the snapshots are files (`.nvr`) on the SD card, the floppies are `.cpm` files.
- The network is the ESP8266 (above).

Kept as 4.67 on purpose: the visible quirks of the console (clear screen and mode change at the old border, scroll from the corner character, ESC Z takes the width from A, unknown ESC eats 5 bytes, …), a single key register (fast typing may lose keys, as on CRISS).

## Licence and sources

Our files: GPL-3 (`LICENSE`). Imported code keeps its own headers: T80 (BSD-style licence, Daniel Wallner, MiSTer changes by Sorgelig), VexRiscv (MIT, SpinalHDL), the Z80 interpreter of the PC tests (MIT, superzazu/z80), karabas-go-modules (their own headers).

The CRISS CP/M computer, its firmware, documents, schematic, CP/M 2.2 build and software are the work of I. S. Reshetnikov (Решетников И. С.) and the magazine «Радио» (radio.ru), published at https://criss.radio.ru/ and https://www.criss.fun/ (© Radio / Радио / OGSS / НГСС / I. Reshetnikov, 2020–2024). **Their licence is unknown**: no terms are stated on the site. Copies are kept in `upstream/` (fetched by `tools/fetch_upstream.sh`), and the data ROM of the `.kg` holds parts of them (the CP/M 2.2 image `CPM22.bin`, the character generators of the VGA controller firmware, the keyboard and compatibility tables of the CPU and peripheral controller firmware). The core itself is written from scratch; the behaviour of the original firmware was studied by disassembly (`docs/re/`).

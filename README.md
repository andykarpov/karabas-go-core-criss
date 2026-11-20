# CRISS CP/M core for Karabas Go / Mini / Mini G

A core of the [CRISS CP/M](https://criss.radio.ru/) DIY computer (КРИСС CP/M, I. Reshetnikov, magazine «Радио» 2021-2023) for the Karabas Go board family (Xilinx Spartan-6 XC6SLX25): a Z80 CP/M 2.2 machine with an 80-column text screen, 8 colours and floppy images on the SD card.

Status: work in progress, nothing to run yet. The plan is in `docs/PLAN.md` (Russian).

## Licence and sources

Our files: GPL-3 (`LICENSE`).

The CRISS CP/M computer, its firmware, documents, schematic, CP/M 2.2 build and software are the work of I. S. Reshetnikov (Игорь Сергеевич Решетников) and the magazine «Радио» (radio.ru), published at https://criss.radio.ru/ and https://www.criss.fun/ (© Radio / Радио / OGSS / НГСС / I. Reshetnikov, 2020-2024). **Their licence is unknown**: no terms are stated on the site. Copies are kept in `upstream/` (fetched by `tools/fetch_upstream.sh`) and the data ROM of the `.kg` holds parts of them (the CP/M 2.2 image `CPM22.bin`, the character generators of the VGA controller firmware, the keyboard and compatibility tables of the CPU / peripheral controller firmware). The core itself is written from scratch; the behaviour of the original firmware was studied by disassembly (`docs/re/`).

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

A CRISS CP/M core (КРИСС CP/M, I. Reshetnikov, magazine «Радио» 2021-2023, https://criss.radio.ru/) for the Karabas Go, Mini and Mini G boards (Xilinx Spartan-6 XC6SLX25, ISE 14.7, XST). The original has no HDL: an ATmega1284P emulates the Z80, other AVRs drive the keyboard, sound and VGA. Our core is written from scratch: T80, BIOS traps `ED xx` served by a helper CPU, text video, disk images on SD. Docs, in Russian: `docs/PLAN.md` (decisions, blocks, phases). Follow the phases; record a changed decision in PLAN.md.

User decisions (2026-11-20): the repo is local for now, public at the first release; ROMs (boot image, character sets) go into the `.kg`, floppy images `.cpm` are files on SD; do NOT write to the author: the port map and undocumented functions come from the firmware disassembly.

Upstream files (firmware, site pages, schematic) have no licence: `tools/fetch_upstream.sh` puts them into `upstream/` (git-ignored, sha256 checked). Never commit them.

## Rules of the core

The rules of the other karabas-go cores apply: standard video timing with syncs always present, `FT_CLK_OUT` about 8 MHz on Mini / Mini G, Go `FDC_*` outputs 0 when unused, Mini / Mini G drive T3 (`FLASH_CS_N`) = 0, OSD switch index = item position unless `"index"`, an OSD value shows 10 characters at most, FL slots 0..7 only, Verilog-2001 for XST (`sv2v` for SV), VHDL-93 for XST, imported code marked `karabas:`, builds through `src/Makefile`.

- Licences: our files GPL-3; keep every imported header as it is.
- **Ask the user before every check on the board** (and the HDMI capture). Ask before a firmware flash.
- One branch per phase of `docs/PLAN.md`, one PR at its end (while the repo is local: a merge into master at the end of the phase).
- Heavy builds: Mini G only while working on one feature, all boards for a release; map with cost table 2 first.

## Memory

The Claude Code memory of this repo is shared with all karabas-go repos: `~/.claude/projects/<this project>/memory` is a symlink to `../karabas-go-notes` (private repo `andykarpov/karabas-go-notes`). Its `MEMORY.md` is split by repo; commit and push it after every memory change.

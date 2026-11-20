# CRISS CP/M core: the ISE 14.7 flow of the boards (as core-doom).
#
#   make minig            a board: <board>-xst (.ngc), <board>-impl (.bin), <board>-core (.kg)
#   make minig-report     LUTs, slices, block RAMs and the timing result of the last build
#   make roms             the data ROM of the .kg from upstream/ (tools/fetch_upstream.sh first)
#   make fw               the helper firmware (fw/build/criss_fw.bin)
#   make nvr              an empty NVRAM image (roms/kg/criss.nvr: settings + two RAM snapshots) for /criss
#   make clean            the intermediate files
# The .kg and the data ROM hold files of the CRISS project (licence unknown, README): committed (user 2026-11-20).

ISE_SETTINGS ?= /opt/Xilinx/14.7/ISE_DS/settings64.sh
PART = xc6slx25-ftg256-2
BOARDS = go mini minig

SHELL = /bin/bash
.SHELLFLAGS = -e -o pipefail -c
.NOTPARALLEL:

XIL = source $(ISE_SETTINGS) > /dev/null;

KG = criss.kg
JSON = core.json
ROMS = fw/build/criss_fw.bin roms/kg/criss_data.bin

MAP_T_go ?= 2
MAP_T_mini ?= 2
MAP_T_minig ?= 2
MAP = -logic_opt off -ol high -xe n -xt 3 -register_duplication off -r 4 -global_opt off -mt 2 -detail -ir off -pr b -lc auto -power off
PAR = -ol high -xe c -mt 4

INCLUDES = $(wildcard ../modules/rtl/mcu/*.vh)

nvr:
	mkdir -p roms/kg
	python3 -c "open('roms/kg/criss.nvr', 'wb').write(bytes(261 * 512))"

.PHONY: all clean roms fw nvr $(BOARDS) $(BOARDS:=-xst) $(BOARDS:=-impl) $(BOARDS:=-core) $(BOARDS:=-report)

all: $(BOARDS)

prj_src = $(shell sed -n 's/^[a-z]* work "\(.*\)"/\1/p' $(1) 2>/dev/null)

fw:
	$(MAKE) -C fw

fw/build/criss_fw.bin: $(wildcard fw/*.c fw/*.h fw/*.S fw/*.ld) fw/Makefile
	$(MAKE) -C fw

roms roms/kg/criss_data.bin: tools/mkroms.py
	mkdir -p roms/kg
	python3 -I tools/mkroms.py ../upstream roms/kg/criss_data.bin

define board
SRC_$(1) := $$(call prj_src,karabas_$(1).prj)

karabas_$(1).ngc: karabas_$(1).prj karabas_$(1).xst $$(SRC_$(1)) $(INCLUDES)
	mkdir -p xst/projnav.tmp
	$(XIL) xst -intstyle ise -ifn karabas_$(1).xst -ofn karabas_$(1).syr

karabas_$(1).bin: karabas_$(1).ngc karabas_$(1).ucf karabas_$(1).ut
	$(XIL) ngdbuild -intstyle ise -dd _ngo -nt timestamp -uc karabas_$(1).ucf -p $(PART) karabas_$(1).ngc karabas_$(1).ngd
	$(XIL) map -intstyle ise -p $(PART) -w $(MAP) -t $(MAP_T_$(1)) -o karabas_$(1)_map.ncd karabas_$(1).ngd karabas_$(1).pcf
	$(XIL) par -w -intstyle ise $(PAR) karabas_$(1)_map.ncd karabas_$(1).ncd karabas_$(1).pcf
	$(XIL) trce -intstyle ise -v 3 -s 2 -n 3 -fastpaths -xml karabas_$(1).twx karabas_$(1).ncd -o karabas_$(1).twr karabas_$(1).pcf -ucf karabas_$(1).ucf
	$(XIL) bitgen -intstyle ise -f karabas_$(1).ut karabas_$(1).ncd

kg_$(1).stamp: karabas_$(1).bin $(JSON) $(ROMS)
	python3 tools/check_osd.py $(JSON)
	python3 make_core.py $(JSON) $(KG) --boards $(1)
	touch $$@

$(1): kg_$(1).stamp
$(1)-xst: karabas_$(1).ngc
$(1)-impl: karabas_$(1).bin
$(1)-core: kg_$(1).stamp
$(1)-report:
	@grep -E "Number of (Slice LUTs|occupied Slices|RAMB16BWERs|RAMB8BWERs|DSP48A1s):" karabas_$(1)_map.mrp
	@grep -E "constraints? (was|were) met|constraints? not met|Timing Score" karabas_$(1).par | sort -u
endef

$(foreach b,$(BOARDS),$(eval $(call board,$(b))))

clean:
	rm -rf xst _ngo xlnx_auto_0_xdb _xmsgs iseconfig

#!/bin/sh
# Fetch the CRISS CP/M upstream files (firmware, docs, schematic) into upstream/.
# They have no licence: kept out of git (.gitignore), checked by sha256.
set -eu
cd "$(dirname "$0")/.."
B=https://criss.radio.ru
mkdir -p upstream/fw upstream/docs
get() { [ -s "upstream/$2" ] || curl -sfL -o "upstream/$2" "$B/$1"; }
for f in criss0.zip criss1.zip criss.fw.2023.11.CLR.zip criss.cpu.4.67.zip criss.cpd.0.24.zip \
         criss.vga.1.24.zip criss.clr.0.5.zip system.zip criss_cpm_22_01.zip; do
	get "criss/fw/$f" "fw/$f"
done
get criss/pdf/CRISS_Manual_RUS.pdf CRISS_Manual_RUS.pdf
get criss/schematic9.png schematic9.png
get criss/CRISS_BOM_LM.pdf docs/CRISS_BOM_LM.pdf
for p in system_programmer_referense sd_images faq pcb319 project_history control_system_platform \
         hardware_support cpm_referense criss_manuals firmware criss_architecture assembling soft \
         modern_cpm_8bit_computer; do
	get "?$p" "docs/$p.html"
done
get "?AzZmzW6VLySSkn6XuYxxZQ%3D%3D" docs/color6.html
# firmware and schematic must match (the html pages change, not checked)
(cd upstream && sha256sum -c --quiet ../tools/upstream.sha256)
# unpack
for z in upstream/fw/*.zip; do
	n=$(basename "$z" .zip); d=upstream/fw/x/$n; mkdir -p "$d"
	if [ "$n" = system ]; then 7z x -y -o"$d" "$z" >/dev/null; else unzip -oq "$z" -d "$d"; fi
done
echo "upstream ok"

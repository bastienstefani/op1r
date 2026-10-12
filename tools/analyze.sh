#!/bin/sh
# Unpack the original R246 image and build everything needed to study it (see docs/firmware-r246.md):
#   tools/analyze.sh /path/to/op1_246.op1 [WORKDIR]          (default WORKDIR: work/, git-ignored)
# Needs a Blackfin objdump (tools/build_bfin_toolchain.sh --binutils-only); set BFIN_OBJDUMP if it is not bfin-elf-objdump in PATH.
set -e
FW=${1:?usage: tools/analyze.sh op1_246.op1 [workdir]}
W=${2:-work}
T=$(dirname "$0")
OD=${BFIN_OBJDUMP:-bfin-elf-objdump}
PY="python3 $T/fwtool.py"

$PY unpack "$FW" "$W/orig"
$PY apply "$W/orig" "$W/patched"
$PY mem "$W/orig/OP1_vdk.ldr" "$W/mem"
$PY mem "$W/patched/OP1_vdk.ldr" "$W/mem_patched"
$PY strings "$W/mem/regions.json" > "$W/strings.txt"

# main program in SDRAM (data + code, 018ab7cc-01aa7078), L1 code SRAM, and the op1r code block at 01e00000
$PY dump "$W/mem/regions.json" 0x018ab7cc 0x01aa7078 "$W/sdram.bin"
$PY dump "$W/mem/regions.json" 0xffa00000 0xffa0bfb4 "$W/l1code.bin"
$PY dump "$W/mem_patched/regions.json" 0x01e00000 0x01e17e88 "$W/op1r_block.bin"
if command -v "$OD" >/dev/null 2>&1; then
    "$OD" -D -b binary -m bfin --adjust-vma=0x018ab7cc "$W/sdram.bin" > "$W/sdram.dis"
    "$OD" -D -b binary -m bfin --adjust-vma=0xffa00000 "$W/l1code.bin" > "$W/l1code.dis"
    "$OD" -D -b binary -m bfin --adjust-vma=0x01e00000 "$W/op1r_block.bin" > "$W/op1r_block.dis"
    $PY xref "$W/sdram.dis" "$W/strings.txt" "$W/funcs.json"
    $PY hooks "$W/orig" --funcs "$W/funcs.json" > "$W/hooks.txt"
else
    echo "no Blackfin objdump ($OD): skipping disassembly (tools/build_bfin_toolchain.sh --binutils-only builds one)"
    $PY hooks "$W/orig" > "$W/hooks.txt"
fi
echo "done: $W"

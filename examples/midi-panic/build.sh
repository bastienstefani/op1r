#!/bin/sh
# Build the MIDI panic example into a firmware image.
#   examples/midi-panic/build.sh op1_246.op1 out.op1 [--op1r]
# Needs bfin-elf-gcc / ld / objcopy in PATH (or BFIN_PREFIX=/path/to/bin/bfin-elf-). Optional: PANIC_KEY=0x35.
set -e
FW=${1:?usage: build.sh op1_246.op1 out.op1 [--op1r]}
OUT=${2:?usage: build.sh op1_246.op1 out.op1 [--op1r]}
OP1R=$3
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../..
X=${BFIN_PREFIX:-bfin-elf-}
B=$(mktemp -d)
trap 'rm -rf "$B"' EXIT
SITE=0x019c2d3e                                   # KeyHandler: CALL key_dispatch(key, pressed)

# previous target of the hooked CALL in the base image (stock dispatcher, or op1r's hook)
python3 "$ROOT/tools/fwtool.py" unpack "$FW" "$B/orig" > /dev/null
LDR=$B/orig/OP1_vdk.ldr
if [ "$OP1R" = "--op1r" ]; then
    python3 "$ROOT/tools/fwtool.py" apply "$B/orig" "$B/patched" > /dev/null
    LDR=$B/patched/OP1_vdk.ldr
fi
PREV=$(python3 "$ROOT/tools/fwtool.py" branch "$LDR" $SITE -q)
echo "hook $SITE: previous target $PREV"

${X}gcc -O2 -ffreestanding -fno-builtin -nostdlib -ffunction-sections -Wall -Wextra \
    ${PANIC_KEY:+-DPANIC_KEY=$PANIC_KEY} -c "$HERE/panic.c" -o "$B/panic.o"
${X}ld -T "$HERE/fw.ld" --defsym=_fw_key_dispatch_prev=$PREV "$B/panic.o" -o "$B/panic.elf"
${X}objcopy -O binary -j .blob "$B/panic.elf" "$B/panic.bin"
HOOK=$(${X}nm "$B/panic.elf" | awk '$3 == "_key_hook" {print "0x" $1}')
BASE=$(${X}objdump -h "$B/panic.elf" | awk '$2 == ".blob" {print "0x" $4}')
${X}objdump -d "$B/panic.elf" > "${OUT%.op1}.dis"
echo "blob $(wc -c < "$B/panic.bin") bytes at $BASE, key_hook at $HOOK (listing: ${OUT%.op1}.dis)"

python3 "$ROOT/tools/fwbuild.py" "$FW" $OP1R -o "$OUT" \
    --blob "$B/panic.bin@$BASE" --call $SITE=$HOOK \
    --text "0x018f738a=$( [ "$OP1R" = "--op1r" ] && echo 'op1r5pnc' || echo 'R246 pnc')"

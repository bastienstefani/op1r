#!/bin/sh
# Build the piano-roll sequencer into a firmware image, on top of op1r.
#   examples/pianoroll/build.sh op1_246.op1 out.op1
# Needs bfin-elf-gcc / ld / objcopy / nm in PATH (or BFIN_PREFIX=/path/to/bin/bfin-elf-).
# NOT TESTED ON HARDWARE. The original op1_246.op1 and the op1r image can always be reflashed.
set -e
FW=${1:?usage: build.sh op1_246.op1 out.op1}
OUT=${2:?usage: build.sh op1_246.op1 out.op1}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../..
X=${BFIN_PREFIX:-bfin-elf-}
FWT="python3 $ROOT/tools/fwtool.py"
B=$(mktemp -d)
trap 'rm -rf "$B"' EXIT
LOAD=0x01ee0000

# the op1r image we build on top of (apply the patch, then read the hook sites from it)
$FWT unpack "$FW" "$B/orig" > /dev/null
$FWT apply "$B/orig" "$B/op1r" > /dev/null
LDR=$B/op1r/OP1_vdk.ldr

# verify every op1r hook target we chain to is where fw.ld expects it
check() { got=$($FWT branch "$LDR" "$1" -q); [ "$got" = "$2" ] || { echo "op1r target at $1 is $got, expected $2 - op1r version mismatch"; exit 1; }; }
check 0x01a42a2e 0x01e00a1c     # idFromName  -> op1r
check 0x01a422ec 0x01e00af8     # nameFromId  -> op1r
check 0x01a3bd58 0x01e00f0c     # build screens
check 0x019f07d2 0x01e0121c     # init screens
check 0x019c2d3e 0x01e01468     # key
check 0x019c2e94 0x01e01a80     # encoder
check 0x01a0d1b0 0x01a46400     # arp instance (op1r leaves it on the stock getter)
# the stock sequencer-clock reset we replace must be untouched (same bytes as R246)
CLK=$(python3 -c "import sys; sys.path.insert(0,'$ROOT/tools'); import fwtool as T; d=open('$LDR','rb').read(); o=T.addr2off(list(T.ldr_blocks(d)),0x01a5e7d0); print(d[o:o+18].hex())")
[ "$CLK" = "09e17c1849e18c010060089348b088b01000" ] || { echo "01a5e7d0 is not the stock clock reset ($CLK)"; exit 1; }

# guard: the stock-address section of fw.ld must not point into op1r's block (it moves with each op1r version)
bad=$(awk '/R246 firmware entry points/{f=1} /hook targets we chain to/{f=0} f && /= 0x01e/' "$HERE/fw.ld")
[ -z "$bad" ] || { echo "fw.ld: op1r-internal address in the stock section (unverified, moves between op1r versions):"; echo "$bad"; exit 1; }

# compile and link at LOAD
"${X}gcc" -O2 -ffreestanding -fno-builtin -nostdlib -ffunction-sections -fdata-sections -Wall -Wextra \
    -c "$HERE/roll.c" -o "$B/roll.o"
LIBGCC=$("${X}gcc" -print-libgcc-file-name)        # __divsi3/__modsi3/__muldi3/__divdi3 etc.
"${X}ld" -T "$HERE/fw.ld" "$B/roll.o" "$LIBGCC" -o "$B/roll.elf"
"${X}objcopy" -O binary -j .blob "$B/roll.elf" "$B/roll.bin"
"${X}objdump" -d "$B/roll.elf" > "${OUT%.op1}.dis"
sym() { "${X}nm" "$B/roll.elf" | awk -v s="_$1" '$3==s {print "0x"$1}'; }
echo "blob $(wc -c < "$B/roll.bin") bytes at $LOAD (listing: ${OUT%.op1}.dis)"

python3 "$ROOT/tools/fwbuild.py" "$FW" --op1r -o "$OUT" \
    --blob "$B/roll.bin@$LOAD" \
    --call 0x01a42a2e=$(sym rp_id_from_name) \
    --call 0x01a42bdc=$(sym rp_id_from_name) \
    --call 0x01a42ce0=$(sym rp_id_from_name) \
    --jump 0x01a422ec=$(sym rp_name_from_id) \
    --call 0x01a0d1b0=$(sym rp_arp_instance) \
    --call 0x01a3bd58=$(sym rp_build_screens) \
    --call 0x019f07d2=$(sym rp_init_screens) \
    --call 0x019c2d3e=$(sym rp_key) \
    --call 0x019c2e94=$(sym rp_encoder) \
    --jump 0x01a5e7d0=$(sym rp_clock_reset) \
    --file "content/display/roll.svg=$HERE/roll.svg" \
    --sql "content/op1_factory.db=INSERT INTO seq_types (id, type) VALUES (9, 'roll')" \
    --text "0x018f738a=op1rroll"

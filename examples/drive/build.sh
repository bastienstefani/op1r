#!/bin/sh
# Build op1r 10 + the piano roll + the drive FX into one firmware image.
#   examples/drive/build.sh op1_246.op1 out.op1
# Needs bfin-elf-gcc / ld / objcopy / nm in PATH (or BFIN_PREFIX=/path/to/bin/bfin-elf-).
# NOT TESTED ON HARDWARE. The original op1_246.op1 and the op1r image can always be reflashed.
set -e
FW=${1:?usage: build.sh op1_246.op1 out.op1}
OUT=${2:?usage: build.sh op1_246.op1 out.op1}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../..
ROLL=$ROOT/examples/pianoroll
X=${BFIN_PREFIX:-bfin-elf-}
FWT="python3 $ROOT/tools/fwtool.py"
B=$(mktemp -d)
trap 'rm -rf "$B"' EXIT
LOAD=0x01e80000

# the op1r image we build on top of
$FWT unpack "$FW" "$B/orig" > /dev/null
$FWT apply "$B/orig" "$B/op1r" > /dev/null
LDR=$B/op1r/OP1_vdk.ldr
py() { python3 -c "import sys; sys.path.insert(0, '$ROOT/tools'); import fwtool as T, hashlib, struct
d = open('$LDR', 'rb').read(); bl = list(T.ldr_blocks(d))
$1"; }

# drive calls into op1r's internals (its FX dispatchers, screen chooser, caches): pin op1r's whole code block
OP1R=$(py "b = [b for b in bl if b['addr'] == 0x01e00000][0]; print(hashlib.sha256(d[b['data_off']:b['data_off'] + b['cnt']]).hexdigest())")
[ "$OP1R" = "9272df7ef2165d343bd8aaf68815536c1e2d59620704e0b2e463cfe457f34bff" ] || {
    echo "op1r's code block is not op1r 10 ($OP1R): re-derive the op1r addresses in drive.ld and ../pianoroll/fw.ld"; exit 1; }

# every site we redirect still holds what op1r 10 put there
check() { got=$($FWT branch "$LDR" "$1" -q); [ "$got" = "$2" ] || { echo "branch at $1 is $got, expected $2"; exit 1; }; }
word() { got=$(py "print('0x%08x' % struct.unpack_from('<I', d, T.addr2off(bl, $1))[0])"); [ "$got" = "$2" ] || { echo "word at $1 is $got, expected $2"; exit 1; }; }
check 0x01a42a2e 0x01e00964     # seq idFromName   -> op1r         (roll)
check 0x01a422ec 0x01e009f8     # seq nameFromId   -> op1r         (roll)
check 0x01a0d1b0 0x01a46400     # arp instance     (stock)         (roll)
check 0x01a3bd58 0x01e00dbc     # build screens    -> op1r         (drive -> roll -> op1r)
check 0x019f07d2 0x01e0106c     # init screens     -> op1r         (drive -> roll -> op1r)
check 0x019c2d3e 0x01e012a8     # key              -> op1r         (drive -> roll -> op1r)
check 0x019c2e94 0x01e017e0     # encoder          -> op1r         (drive -> roll -> op1r)
check 0x019aad8e 0x01e00be8     # fx name compare  -> op1r         (drive)
word  0x01910ca8 0x01e008d0     # filter mono process              (drive)
word  0x01910cb0 0x01e00734     # filter mono set_position         (drive)
word  0x01910cc8 0x01e00778     # filter stereo process            (drive)
word  0x01910cd0 0x01e00760     # filter stereo set_position       (drive)
word  0x018f7ff8 0x01e0d76c     # ScreenId fx case 5               (drive)
CLK=$(py "o = T.addr2off(bl, 0x01a5e7d0); print(d[o:o + 18].hex())")
[ "$CLK" = "09e17c1849e18c010060089348b088b01000" ] || { echo "01a5e7d0 is not the stock clock reset ($CLK)"; exit 1; }

# guard: the stock-address sections of both scripts must not point into op1r's block
bad=$(awk '/R246 firmware entry points/{f=1} /hook targets we chain to/{f=0} f && /= 0x01e/' "$ROLL/fw.ld";
      awk '/R246 firmware/{f=1} /op1r 10/{f=0} f && /= 0x01e/' "$HERE/drive.ld")
[ -z "$bad" ] || { echo "op1r-internal address in a stock section:"; echo "$bad"; exit 1; }

# compile and link at LOAD
CF="-O2 -ffreestanding -fno-builtin -nostdlib -ffunction-sections -fdata-sections -Wall -Wextra"
"${X}gcc" $CF -c "$ROLL/roll.c" -o "$B/roll.o"
"${X}gcc" $CF -c "$HERE/drive.c" -o "$B/drive.o"
LIBGCC=$("${X}gcc" -print-libgcc-file-name)
"${X}ld" -L "$ROLL" -T "$HERE/drive.ld" "$B/roll.o" "$B/drive.o" "$LIBGCC" -o "$B/blob.elf"
"${X}objcopy" -O binary -j .blob "$B/blob.elf" "$B/blob.bin"
"${X}objdump" -d "$B/blob.elf" > "${OUT%.op1}.dis"
sym() { "${X}nm" "$B/blob.elf" | awk -v s="_$1" '$3==s {print "0x"$1}'; }
echo "blob $(wc -c < "$B/blob.bin") bytes at $LOAD (listing: ${OUT%.op1}.dis)"

python3 "$ROOT/tools/fwbuild.py" "$FW" --op1r -o "$OUT" \
    --blob "$B/blob.bin@$LOAD" \
    --call 0x01a42a2e=$(sym rp_id_from_name) \
    --call 0x01a42bdc=$(sym rp_id_from_name) \
    --call 0x01a42ce0=$(sym rp_id_from_name) \
    --jump 0x01a422ec=$(sym rp_name_from_id) \
    --call 0x01a0d1b0=$(sym rp_arp_instance) \
    --jump 0x01a5e7d0=$(sym rp_clock_reset) \
    --call 0x01a3bd58=$(sym dv_build_screens) \
    --call 0x019f07d2=$(sym dv_init_screens) \
    --call 0x019c2d3e=$(sym dv_key) \
    --call 0x019c2e94=$(sym dv_encoder) \
    --jump 0x019aad8e=$(sym dv_fx_name_eq) \
    --word 0x01910ca8=$(sym dv_fx_mono) \
    --word 0x01910cb0=$(sym dv_fx_mono_pos) \
    --word 0x01910cc8=$(sym dv_fx_stereo) \
    --word 0x01910cd0=$(sym dv_fx_stereo_pos) \
    --word 0x018f7ff8=$(sym dv_scr_case) \
    --file "content/display/roll.svg=$ROLL/roll.svg" \
    --file "content/display/drive.svg=$HERE/drive.svg" \
    --sql "content/op1_factory.db=INSERT INTO seq_types (id, type) VALUES (8, 'roll')" \
    --sql "content/op1_factory.db=INSERT INTO fx_types VALUES (14, 'drive', '[4403, 945, 11264, 32700, 21252, 1820, 6553, 26214]')" \
    --text "0x018f738a=op1r+drv"

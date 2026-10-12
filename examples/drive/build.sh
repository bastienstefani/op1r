#!/bin/sh
# Build op1r 12 + the piano roll + the drive FX into one firmware image.
#   examples/drive/build.sh op1_246.op1 out.op1 [base.op1]
# With base.op1, build on that image instead of op1r: someone else's firmware built on op1r 12 (only the bases
# listed below, whose key / encoder hooks were checked to chain to op1r's).
# Needs bfin-elf-gcc / ld / objcopy / nm in PATH (or BFIN_PREFIX=/path/to/bin/bfin-elf-).
# NOT TESTED ON HARDWARE. The original op1_246.op1 and the op1r image can always be reflashed.
set -e
FW=${1:?usage: build.sh op1_246.op1 out.op1 [base.op1]}
OUT=${2:?usage: build.sh op1_246.op1 out.op1 [base.op1]}
BASE=${3:-}
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$HERE/../..
ROLL=$ROOT/examples/pianoroll
X=${BFIN_PREFIX:-bfin-elf-}
FWT="python3 $ROOT/tools/fwtool.py"
B=$(mktemp -d)
trap 'rm -rf "$B"' EXIT
LOAD=0x01ee0000

# the image we build on top of: op1r 12 (applied here), or a known base built on op1r 12
KEY_NEXT=0x01e01468; ENC_NEXT=0x01e01a80        # op1r's key / encoder hooks
if [ -z "$BASE" ]; then
    $FWT unpack "$FW" "$B/orig" > /dev/null
    $FWT apply "$B/orig" "$B/op1r" > /dev/null
    LDR=$B/op1r/OP1_vdk.ldr
    FROM=--op1r; DEFS=; LABEL=op1r+drv
else
    BSHA=$(python3 -c "import hashlib, sys; print(hashlib.sha256(open(sys.argv[1], 'rb').read()).hexdigest())" "$BASE")
    case "$BSHA" in
    b7adf5c0fdd523a726e81880e683eda48061a261944dfa1a7a52c57ebfc41066)
        # op1_249.op1 (maikopruett.com, "undo-f70dabfe-op1r-fc1ae37"): op1r 12 + tape undo, scale quantiser,
        # snapshot names. Its code + bss use 01e40000-01eb7464 and 02800000-02980000; its key / encoder hooks
        # run its modules, then JUMP.L to op1r's (at 01e4064a / 01e406ae).
        KEY_NEXT=0x01e405f4; ENC_NEXT=0x01e40658; KEY_TAIL=0x01e4064a; ENC_TAIL=0x01e406ae; LABEL=249+drv ;;
    *)  echo "unknown base image (sha256 $BSHA): check its hooks and add it to build.sh"; exit 1 ;;
    esac
    $FWT unpack "$BASE" "$B/base" > /dev/null
    LDR=$B/base/OP1_vdk.ldr
    FROM="--base $BASE"; DEFS="--defsym _base_key=$KEY_NEXT --defsym _base_encoder=$ENC_NEXT"
fi
py() { python3 -c "import sys; sys.path.insert(0, '$ROOT/tools'); import fwtool as T, hashlib, struct
d = open('$LDR', 'rb').read(); bl = list(T.ldr_blocks(d))
$1"; }

# drive calls into op1r's internals (its FX dispatchers, screen chooser, caches): pin op1r's whole code block
OP1R=$(py "b = [b for b in bl if b['addr'] == 0x01e00000][0]; print(hashlib.sha256(d[b['data_off']:b['data_off'] + b['cnt']]).hexdigest())")
[ "$OP1R" = "b5bc21684c3ef8ba36114aa13c1513e1b4f6879c89df7fe147cb4206923d3634" ] || {
    echo "op1r's code block is not op1r 12 ($OP1R): re-derive the op1r addresses in drive.ld and ../pianoroll/fw.ld"; exit 1; }

# every site we redirect still holds what op1r 12 put there
check() { got=$($FWT branch "$LDR" "$1" -q); [ "$got" = "$2" ] || { echo "branch at $1 is $got, expected $2"; exit 1; }; }
word() { got=$(py "print('0x%08x' % struct.unpack_from('<I', d, T.addr2off(bl, $1))[0])"); [ "$got" = "$2" ] || { echo "word at $1 is $got, expected $2"; exit 1; }; }
check 0x01a42a2e 0x01e00a1c     # seq idFromName   -> op1r         (roll)
check 0x01a422ec 0x01e00af8     # seq nameFromId   -> op1r         (roll)
check 0x01a0d1b0 0x01a46400     # arp instance     (stock)         (roll)
check 0x01a3bd58 0x01e00f0c     # build screens    -> op1r         (drive -> roll -> op1r)
check 0x019f07d2 0x01e0121c     # init screens     -> op1r         (drive -> roll -> op1r)
check 0x019c2d3e $KEY_NEXT     # key              -> op1r / base  (drive -> roll -> [base ->] op1r)
check 0x019c2e94 $ENC_NEXT     # encoder          -> op1r / base  (drive -> roll -> [base ->] op1r)
if [ -n "$BASE" ]; then          # the base's hooks end in op1r's
    check $KEY_TAIL 0x01e01468
    check $ENC_TAIL 0x01e01a80
fi
check 0x019aad8e 0x01e00d38     # fx name compare  -> op1r         (drive)
word  0x01910ca8 0x01e00988     # filter mono process              (drive)
word  0x01910cb0 0x01e007ec     # filter mono set_position         (drive)
word  0x01910cc8 0x01e00830     # filter stereo process            (drive)
word  0x01910cd0 0x01e00818     # filter stereo set_position       (drive)
word  0x018f7ff8 0x01e116e8     # ScreenId fx case 5               (drive)
CLK=$(py "o = T.addr2off(bl, 0x01a5e7d0); print(d[o:o + 18].hex())")
[ "$CLK" = "09e17c1849e18c010060089348b088b01000" ] || { echo "01a5e7d0 is not the stock clock reset ($CLK)"; exit 1; }

# guard: the stock-address sections of both scripts must not point into op1r's block
bad=$(awk '/R246 firmware entry points/{f=1} /hook targets we chain to/{f=0} f && /= 0x01e/' "$ROLL/fw.ld";
      awk '/R246 firmware/{f=1} /^\/\* op1r /{f=0} f && /= 0x01e/' "$HERE/drive.ld")
[ -z "$bad" ] || { echo "op1r-internal address in a stock section:"; echo "$bad"; exit 1; }

# compile and link at LOAD
CF="-O2 -ffreestanding -fno-builtin -nostdlib -ffunction-sections -fdata-sections -Wall -Wextra"
"${X}gcc" $CF -c "$ROLL/roll.c" -o "$B/roll.o"
"${X}gcc" $CF -c "$HERE/drive.c" -o "$B/drive.o"
LIBGCC=$("${X}gcc" -print-libgcc-file-name)
"${X}ld" $DEFS -L "$ROLL" -T "$HERE/drive.ld" "$B/roll.o" "$B/drive.o" "$LIBGCC" -o "$B/blob.elf"
"${X}objcopy" -O binary -j .blob "$B/blob.elf" "$B/blob.bin"
"${X}objdump" -d "$B/blob.elf" > "${OUT%.op1}.dis"
sym() { "${X}nm" "$B/blob.elf" | awk -v s="_$1" '$3==s {print "0x"$1}'; }
echo "blob $(wc -c < "$B/blob.bin") bytes at $LOAD (listing: ${OUT%.op1}.dis)"

python3 "$ROOT/tools/fwbuild.py" "$FW" $FROM -o "$OUT" \
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
    --sql "content/op1_factory.db=INSERT INTO seq_types (id, type) VALUES (9, 'roll')" \
    --sql "content/op1_factory.db=INSERT INTO fx_types VALUES (14, 'drive', '[4403, 945, 11264, 32700, 21252, 1820, 6553, 26214]')" \
    --text "0x018f738a=$LABEL"

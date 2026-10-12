#!/bin/sh
# Host tests of the examples (no OP-1 needed): the same C code the firmware runs, compiled for this PC.
#   examples/run_tests.sh [outdir]     # outdir (default: a temporary directory) also gets the screen previews
# Needs a C compiler (cc); the screen previews also need python3 + Pillow (skipped if missing).
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$(mktemp -d)}
mkdir -p "$OUT"
CC=${CC:-cc}
UB="-fsanitize=undefined,signed-integer-overflow -fno-sanitize-recover"

echo "== piano roll: beat-aligned start (beat.h)"
$CC -O2 -Wall -Wextra -Werror -o "$OUT/test_beat" "$HERE/pianoroll/test_beat.c"
"$OUT/test_beat"

echo "== drive: DSP, every parameter combination under UBSan (dsp.h)"
$CC -O2 -Wall -Wextra $UB -I"$HERE/drive" -o "$OUT/test_host" "$HERE/drive/test_host.c" -lm
(cd "$OUT" && ./test_host | head -1)

echo "== drive: hooks against a mock firmware (drive.c)"
$CC -DDV_HOST -O1 -Wall -Wextra $UB -o "$OUT/test_hooks" "$HERE/drive/test_hooks.c"
"$OUT/test_hooks"

echo "== drive: screen previews (screen.h)"
$CC -O2 -Wall -Wextra -Werror -o "$OUT/test_ui" "$HERE/drive/test_ui.c"
if python3 -c "import PIL" 2>/dev/null; then
    "$OUT/test_ui" r0 14000 r1 100 r2 20000 r3 32767 k 22000 h 3000 > "$OUT/page1.txt"
    "$OUT/test_ui" r0 20000 r1 15200 r2 9000 r3 26000 r5 20000 r6 20000 r7 20000 k 30000 p 1 > "$OUT/page2.txt"
    python3 "$HERE/drive/render_ui.py" "$OUT/page1.txt" "$OUT/drive_page1.png"
    python3 "$HERE/drive/render_ui.py" "$OUT/page2.txt" "$OUT/drive_page2.png"
    echo "previews: $OUT/drive_page1.png $OUT/drive_page2.png"
else
    echo "(Pillow not installed: previews skipped)"
fi
echo "all host tests passed"

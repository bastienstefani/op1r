#!/bin/sh
# Build GNU binutils for the Blackfin (bfin-elf): objdump to disassemble, as/ld to assemble and link.
#   tools/build_bfin_objdump.sh [PREFIX]        (default PREFIX: work/bfin) -> PREFIX/bin/bfin-elf-objdump
# Needs a C compiler, make, curl. Takes a few minutes. Docs are skipped (no makeinfo needed).
set -e
PREFIX=$(mkdir -p "${1:-work/bfin}" && cd "${1:-work/bfin}" && pwd)
V=2.42
B=$PREFIX/src
mkdir -p "$B" && cd "$B"
[ -f binutils-$V.tar.xz ] || curl -sSLO https://ftp.gnu.org/gnu/binutils/binutils-$V.tar.xz
[ -d binutils-$V ] || tar xf binutils-$V.tar.xz
mkdir -p build && cd build
../binutils-$V/configure --target=bfin-elf --prefix="$PREFIX" --disable-nls --disable-werror \
    --disable-gdb --disable-sim --disable-gprof --disable-gprofng --disable-gold > configure.log
make -j"$(nproc 2>/dev/null || echo 2)" MAKEINFO=true > make.log
make MAKEINFO=true install > install.log
echo "export BFIN_OBJDUMP=$PREFIX/bin/bfin-elf-objdump"

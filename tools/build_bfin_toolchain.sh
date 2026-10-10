#!/bin/sh
# Build a Blackfin (bfin-elf) cross toolchain: binutils 2.42 (objdump, as, ld, objcopy) and GCC 14.2 (C only,
# freestanding, with libgcc).
#   tools/build_bfin_toolchain.sh [--binutils-only] [PREFIX]      (default PREFIX: work/bfin)
# -> PREFIX/bin/bfin-elf-{objdump,gcc,ld,objcopy,...}
# Needs a host C/C++ compiler, make, curl, xz. binutils: a few minutes; GCC: ~30 min on 4 cores.
set -e
ONLY=
[ "$1" = "--binutils-only" ] && { ONLY=1; shift; }
mkdir -p "${1:-work/bfin}"
PREFIX=$(cd "${1:-work/bfin}" && pwd)
J=$(nproc 2>/dev/null || echo 2)
SRC=$PREFIX/src
mkdir -p "$SRC" && cd "$SRC"

BU=binutils-2.42
[ -f $BU.tar.xz ] || curl -sSLO https://ftp.gnu.org/gnu/binutils/$BU.tar.xz
[ -d $BU ] || tar xf $BU.tar.xz
mkdir -p build-binutils && cd build-binutils
../$BU/configure --target=bfin-elf --prefix="$PREFIX" --disable-nls --disable-werror \
    --disable-gdb --disable-sim --disable-gprof --disable-gprofng --disable-gold > configure.log
make -j"$J" MAKEINFO=true > make.log            # MAKEINFO=true: skip the docs (no texinfo needed)
make MAKEINFO=true install > install.log
cd "$SRC"
echo "binutils: $PREFIX/bin/bfin-elf-objdump"
[ -n "$ONLY" ] && { echo "export BFIN_OBJDUMP=$PREFIX/bin/bfin-elf-objdump"; exit 0; }

GCC=gcc-14.2.0
[ -f $GCC.tar.xz ] || curl -sSLO https://ftp.gnu.org/gnu/gcc/$GCC/$GCC.tar.xz
[ -d $GCC ] || { tar xf $GCC.tar.xz && (cd $GCC && ./contrib/download_prerequisites > /dev/null); }
mkdir -p build-gcc && cd build-gcc
PATH=$PREFIX/bin:$PATH ../$GCC/configure --target=bfin-elf --prefix="$PREFIX" --enable-languages=c \
    --without-headers --with-newlib --disable-nls --disable-libssp --disable-libquadmath --disable-threads \
    --disable-shared --disable-multilib --disable-werror > configure.log
PATH=$PREFIX/bin:$PATH make -j"$J" all-gcc all-target-libgcc > make.log
PATH=$PREFIX/bin:$PATH make install-gcc install-target-libgcc > install.log
echo "gcc: $PREFIX/bin/bfin-elf-gcc"
echo "export PATH=$PREFIX/bin:\$PATH BFIN_OBJDUMP=$PREFIX/bin/bfin-elf-objdump"

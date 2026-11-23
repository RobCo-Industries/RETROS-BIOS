#!/usr/bin/env bash
# Builds `make sdimage` into a temporary build directory and reads the card
# back with mtools: RETROS.IMG, config.txt from boot/config.txt,
# ROBCO/BOOT.CFG from boot/BOOT.CFG and the test stage as ROBCO/MFBOOT.SYS.
# Then STAGE2=<file> must put that file there instead.
set -euo pipefail
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD BOARD STAGE2 ROBCO_CORE WINDOW DOC_ROOT

root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

fail() {
    echo "sdimage_test: $*" >&2
    exit 1
}

for t in mcopy mformat; do
    command -v "$t" > /dev/null || fail "$t not found: install mtools"
done
export MTOOLS_SKIP_CHECK=1

build=$tmp/build
# The FAT partition starts at LBA 2048 (tools/mksdimage.sh).
part="@@$((2048 * 512))"

get() {
    mcopy -n -i "$build/sd.img$part" "::$1" "$2" 2> "$tmp/mcopy.err" ||
        fail "$1 is not on the card: $(cat "$tmp/mcopy.err")"
}

cd "$root"
make -s sdimage BUILD="$build" > "$tmp/make.out" 2>&1 || fail "make sdimage failed: $(tail -20 "$tmp/make.out")"
get RETROS.IMG "$tmp/RETROS.IMG"
cmp -s "$tmp/RETROS.IMG" "$build/retros.img" || fail "RETROS.IMG differs from the build"
get config.txt "$tmp/config.txt"
cmp -s "$tmp/config.txt" boot/config.txt || fail "config.txt differs from boot/config.txt"
get ROBCO/BOOT.CFG "$tmp/BOOT.CFG"
cmp -s "$tmp/BOOT.CFG" boot/BOOT.CFG || fail "BOOT.CFG differs from boot/BOOT.CFG"
get ROBCO/MFBOOT.SYS "$tmp/MFBOOT.SYS"
cmp -s "$tmp/MFBOOT.SYS" "$build/stage2/stage2.img" || fail "MFBOOT.SYS is not the test stage"

printf 'not a boot image\n' > "$tmp/other.img"
make -s sdimage BUILD="$build" STAGE2="$tmp/other.img" > "$tmp/make2.out" 2>&1 ||
    fail "make sdimage STAGE2= failed: $(tail -20 "$tmp/make2.out")"
get ROBCO/MFBOOT.SYS "$tmp/MFBOOT2.SYS"
cmp -s "$tmp/MFBOOT2.SYS" "$tmp/other.img" || fail "STAGE2= did not replace MFBOOT.SYS"

echo "sdimage_test: passed"

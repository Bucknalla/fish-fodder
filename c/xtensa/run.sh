#!/bin/sh
# Run the C renderer on an emulated Xtensa CPU and check it draws exactly
# what the JavaScript draws. The ESP32-S3 has no double-precision FPU, so its
# doubles run in software (GCC's Xtensa libgcc routines); so do this lx106
# core's, which makes this the closest check to an ESP32 short of one.
#
#   apt install gcc-xtensa-lx106 picolibc-xtensa-lx106-elf qemu-system-misc
#   c/xtensa/run.sh [COUNT]      COUNT generated names (default 80) plus 3
#                                fixed ones and 4 frames; ~8 s per fish
set -e
here=$(cd "$(dirname "$0")" && pwd)
c=$here/..
out=$c/build/xtensa
mkdir -p "$out/libgcc"
CC=xtensa-lx106-elf-gcc

# Ubuntu's lx106 libgcc leaves out what the ESP8266 has in ROM, doubles
# included, so assemble those from GCC's own sources.
GCC=https://raw.githubusercontent.com/gcc-mirror/gcc/releases/gcc-13.2.0/libgcc/config/xtensa
for f in lib1funcs.S ieee754-df.S ieee754-sf.S xtensa-config-builtin.h; do
  [ -s "$out/libgcc/$f" ] || curl -sSfo "$out/libgcc/$f" "$GCC/$f"
done
if [ ! -f "$out/libextra.a" ]; then
  for fn in _mulsi3 _divsi3 _modsi3 _udivsi3 _umodsi3 _umulsidi3 \
    _negsf2 _addsubsf3 _mulsf3 _divsf3 _cmpsf2 _fixsfsi _fixunssfsi _floatsisf _floatunsisf \
    _negdf2 _addsubdf3 _muldf3 _divdf3 _cmpdf2 _fixdfsi _fixunsdfsi _floatsidf _floatunsidf \
    _truncdfsf2 _extendsfdf2; do
    $CC -c -O2 -DL$fn -I"$out/libgcc" -o "$out/libgcc/$fn.o" "$out/libgcc/lib1funcs.S"
  done
  xtensa-lx106-elf-ar rcs "$out/libextra.a" "$out"/libgcc/*.o
fi

node "$here/compare.mjs" names "$out" "${1:-80}"
$CC -O2 -ffp-contract=off -nostartfiles --specs=/usr/lib/xtensa-lx106-elf/picolibc.specs \
  -T "$here/sim.ld" -Wl,--no-warn-rwx-segments -I"$c" -I"$out" -o "$out/check.elf" \
  "$here/sim.c" "$here/check.c" "$c/fishdraw.c" "$c/fishfodder.c" "$c/fdlibm.c" \
  -lm -lc "$out/libextra.a" -lgcc
qemu-system-xtensa -M sim -cpu lx106 -m 64M -nographic -monitor none -semihosting \
  -kernel "$out/check.elf" > "$out/out.txt"
node "$here/compare.mjs" check "$out"

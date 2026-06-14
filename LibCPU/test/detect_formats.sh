#!/bin/sh
# Format-detection sweep for SymbolReader: craft each format's magic bytes and assert that
# `lcx klib symbols` names the format. $1 = path to the lcx executable.
set -e
lcx="$1"
d=$(mktemp -d)
trap 'rm -rf "$d"' EXIT

printf '\177ELF\002\001\000\000\000\000\000\000\000\000\000\000' > "$d/elf"
printf 'Joy!peffpwpc\000\000\000\001'                            > "$d/cfmppc"
printf 'Joy!peffm68k\000\000\000\001'                            > "$d/cfm68k"
printf '\000\000\003\363\000\000\000\000'                        > "$d/hunk"
printf 'NetWare Loadable Module\032'                             > "$d/nlm"
printf 'L\001\000\000'                                           > "$d/wincoff"   # i386 COFF machine magic
# bigobj: Sig1=0, Sig2=0xFFFF, then 8 bytes, then the bigobj GUID at offset 12.
printf '\000\000\377\377\002\000\000\000\000\000\000\000\307\241\272\321\356\272\251\113\257\040\372\366\152\244\334\270' > "$d/bigobj"
printf '\001\337\000\000'                                        > "$d/xcoff"   # 0x01DF big-endian
# OpenVMS EIHD: size=0x60@+0, hdrblkcnt=1@+4, majorid=3@+8, minorid=0@+12, imgtype=1@+16.
printf '\140\000\000\000\001\000\000\000\003\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000' > "$d/vms"
# CP/M-VAX: 0x601A branch magic stored little-endian (1A 60), distinct from big-endian GEMDOS.
{ printf '\032\140'; dd if=/dev/zero bs=1 count=26 2>/dev/null; } > "$d/cpmvax"
# MZ + PE: 'MZ', e_lfanew=0x40 at offset 0x3C, 'PE\0\0' at 0x40.
{ printf 'MZ'; dd if=/dev/zero bs=1 count=58 2>/dev/null; \
  printf '\100\000\000\000'; printf 'PE\000\000'; } > "$d/pe"
printf 'VZ\000\000'                                              > "$d/uefite"   # UEFI TE
printf 'MP\000\000'                                              > "$d/pharlap"  # Phar-Lap
# X68000 .X: 'HU' + reserved + loadmode, then a full 0x40-byte header.
{ printf 'HU\000\000'; dd if=/dev/zero bs=1 count=60 2>/dev/null; } > "$d/x68000"
{ dd if=/dev/zero bs=1 count=16 2>/dev/null; printf '\021\000\000\357'; } > "$d/aif"  # AIF: 0xEF000011 @0x10
printf '\002\305\342\304'                                        > "$d/os360"    # X'02' + EBCDIC ESD
printf '\003\360\000\000'                                        > "$d/goff"     # PTV X'03' + HDR
printf '\340\000\000\000'                                        > "$d/ieee695"  # MB record 0xE0
printf 'S00600004844521B\n'                                      > "$d/srec"     # Motorola S-record
printf ':10010000214601360121470136007EFE09D219\n'              > "$d/ihex"     # Intel HEX
printf '/00100010ABCD\n'                                         > "$d/tekhex"   # Tektronix hex
printf '@0000\nDEAD\n'                                           > "$d/vhex"     # Verilog memh

ok=1
check() {
    got=$("$lcx" klib symbols "$d/$1" 2>&1 | tail -1)
    case "$got" in
        *"$2"*) ;;
        *) echo "MISMATCH $1: want '$2' got '$got'"; ok=0 ;;
    esac
}
check elf    "elf:"
check cfmppc "cfm-ppc:"
check cfm68k "cfm-68k:"
check hunk   "amiga-hunk:"
check nlm    "nlm:"
check wincoff "wincoff:"
check bigobj  "bigobj-coff:"
check xcoff  "xcoff:"
check pe     "pe/coff:"
check vms    "vms:"
check cpmvax "cpm-vax:"
check uefite "uefi-te:"
check pharlap "phar-lap:"
check x68000 "x68000:"
check aif    "arm-aif:"
check os360  "os360-obj:"
check goff   "goff:"
check ieee695 "ieee-695:"
check srec   "srec:"
check ihex   "intel-hex:"
check tekhex "tektronix-hex:"
check vhex   "verilog-hex:"

test "$ok" -eq 1

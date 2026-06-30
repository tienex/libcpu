#!/bin/sh
# Header-accurate cross-COMPILE of libnix for OS/2 against the REAL OS/2 libc (kLIBC) headers.
# The OS/2 GCC (bitwiseworks, with the LX object backend) runs only on OS/2, but kLIBC's HEADERS are
# open source -- so this compiles every OS/2-profile source (CORE+BSD) with a generic x86 GCC,
# -nostdinc + the real kLIBC headers + OS/2 calling-convention keywords neutralised + the standard
# POSIX feature macros. That exercises the port's header/symbol assumptions against OS/2's actual
# libc -- where the portability bugs live -- exactly as the Haiku gate does with Haiku's headers.
# (A runnable OS/2 binary additionally needs the bitwiseworks GCC + OS/2 itself for the LX link.)
#
# Needs a 32-bit x86 GCC (i686-elf-gcc, from `brew install i686-elf-gcc`) and the kLIBC headers; the
# headers are auto-cloned to $KLIBC_DIR if absent. Skips cleanly (exit 0) when the x86 GCC is missing.
set -u

SRC="$(cd "$(dirname "$0")" && pwd)"
WORK="${1:-/tmp/libnix-os2}"
: "${KLIBC_DIR:=/Volumes/FuryCode/haiku-xtools/klibc-os2}"
PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"; export PATH

XG="$(command -v i686-elf-gcc || true)"
if [ -z "$XG" ]; then echo "SKIP: i686-elf-gcc (32-bit x86 GCC) not found"; exit 0; fi

K="$KLIBC_DIR/src/emx/include"
if [ ! -f "$K/stdio.h" ]; then
	command -v git >/dev/null 2>&1 || { echo "SKIP: git not found to fetch kLIBC headers"; exit 0; }
	echo "fetching kLIBC (OS/2 libc) headers..."
	git clone --depth 1 https://github.com/bitwiseworks/libc.git "$KLIBC_DIR" >/dev/null 2>&1 \
		|| { echo "SKIP: could not clone kLIBC headers"; exit 0; }
fi
[ -f "$K/stdio.h" ] || { echo "SKIP: kLIBC headers not present at $K"; exit 0; }

mkdir -p "$WORK"
# Configure once with the host cmake to generate the config + xec-base.h, then correct the autoconf
# function/header probes a link-less x86 host cannot resolve to OS-2/kLIBC reality.
cmake -S "$SRC" -B "$WORK/cmake" -DNIX_HOST_PROFILE=os2 >/dev/null 2>&1 \
	|| { echo "FAIL: cmake configure (os2 profile)"; exit 1; }
CFG="$WORK/cfg"; mkdir -p "$CFG"
cp "$WORK/cmake/nix/nix-config.h" "$CFG/"
# kLIBC lacks these (header probe); turn them off. It HAS sockets/uio/select/shm/statvfs/wait.
for off in SYS_REBOOT_H POLL_H PTHREAD_H SCHED_H BITSTRING_H SYS_FILIO_H SYS_MOUNT_H NETINET_IN_SYSTM_H SYS_EVENT_H SYS_STATFS_H SYS_VFS_H SYS_MKDEV_H SYS_MNTTAB_H UCRED_H FEATURES_H; do
	sed -i '' "s|#define HAVE_$off 1|/* #undef HAVE_$off */|" "$CFG/nix-config.h" 2>/dev/null || \
	sed -i "s|#define HAVE_$off 1|/* #undef HAVE_$off */|" "$CFG/nix-config.h" 2>/dev/null
done
for off in STATFS FSTATFS GETFSSTAT GETMNTENT GETEXTMNTENT RESETMNTTAB SETLOGIN ISSETUGID GETPEEREID \
           GETPEERUCRED CHFLAGS FCHFLAGS FUTIMES FUTIMESAT GETREUID GETREGID GETRESUID GETRESGID \
           GETFSUID GETFSGID ADJFREQ ADJTIME GETHOSTID SETHOSTID ACCT REVOKE NICE KQUEUE PTHREAD_YIELD; do
	sed -i '' "s|#define HAVE_$off 1|/* #undef HAVE_$off */|" "$CFG/nix-config.h" 2>/dev/null || \
	sed -i "s|#define HAVE_$off 1|/* #undef HAVE_$off */|" "$CFG/nix-config.h" 2>/dev/null
done

BI="$($XG -print-file-name=include)"
DEFS="-D__OS2__ -D__KLIBC__ -D__EMX__ -D_System= -D_Optlink= -D_Far16= -D_Pascal= -D_Cdecl= -D_Seg16= -D_far16= \
-D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE"
INCS="-nostdinc -isystem $K -isystem $BI"
LOCAL="-I$SRC/nix -I$CFG -I$SRC/../../LibCPU/include"

FILES="nix-common nix-env nix-init nix-fd nix-io nix-file nix-dir nix-time nix-hostinfo nix-cred nix-mem \
nix-rt-time nix-rt-process nix-process nix-signal nix-socket nix-s5-msg nix-s5-sem nix-s5-shm nix-xcpt \
nix-structs nix-bsd-cred nix-bsd-dir nix-bsd-file nix-bsd-fs nix-bsd-io nix-bsd-mem nix-bsd-nfs \
nix-bsd-obsolete nix-bsd-process nix-bsd-signal nix-bsd-system nix-bsd-time"
ok=0; fail=0; failed=""
for f in $FILES; do
	if "$XG" -fsyntax-only $DEFS $INCS $LOCAL "$SRC/nix/$f.c" 2>"$WORK/$f.err"; then
		ok=$((ok + 1)); else fail=$((fail + 1)); failed="$failed $f"; fi
done
total=$((ok + fail))
if [ "$fail" -eq 0 ]; then
	echo "RESULT: PASS -- libnix compiles for OS/2 ($ok/$total sources, real kLIBC headers, i686 gcc)"
	exit 0
fi
echo "RESULT: FAIL -- $fail/$total OS/2 source(s) failed:$failed"
for f in $failed; do echo "--- $f ---"; grep -E "error:|fatal" "$WORK/$f.err" | head -3; done
exit 1

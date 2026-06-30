#!/bin/sh
# Native cross-COMPILE of libnix for Haiku with the real Haiku toolchain (x86_64-unknown-haiku-gcc)
# and the real Haiku system headers. Proves the Haiku host backend compiles against Haiku's actual
# libroot/POSIX headers -- far beyond a POSIX stand-in build. (Linking a runnable binary additionally
# needs Haiku's shared libroot from a built Haiku image; this gate covers the compile, which is where
# the header/symbol portability bugs live.)
#
# Requires a Haiku cross toolchain + source tree, located via:
#   HAIKU_CROSS_DIR  = .../haiku/generated/cross-tools-x86_64   (from ./configure --build-cross-tools)
#   HAIKU_SRC        = .../haiku                                 (the Haiku source tree, for headers)
# Skips cleanly (exit 0) when they are absent, like run-libnix-wine.sh does without MinGW/Wine.
set -u

SRC="$(cd "$(dirname "$0")" && pwd)"
: "${HAIKU_CROSS_DIR:=/Volumes/FuryCode/haiku-xtools/haiku/generated/cross-tools-x86_64}"
: "${HAIKU_SRC:=/Volumes/FuryCode/haiku-xtools/haiku}"
XGCC="$HAIKU_CROSS_DIR/bin/x86_64-unknown-haiku-gcc"

if [ ! -x "$XGCC" ] || [ ! -d "$HAIKU_SRC/headers/posix" ]; then
	echo "SKIP: Haiku cross toolchain/source not found (set HAIKU_CROSS_DIR + HAIKU_SRC)"; exit 0
fi

WORK="${1:-/tmp/libnix-haiku}"; rm -rf "$WORK"; mkdir -p "$WORK"

# Generate the config headers with cmake (Haiku toolchain), then correct the autoconf function
# probes that a link-less cross-configure cannot resolve (no target libroot to link): switch off the
# BSD/Solaris/Linux-only calls Haiku's headers do not declare. (A native Haiku configure, which can
# link libroot, resolves these automatically.)
cmake -S "$SRC" -B "$WORK/cmake" \
	-DCMAKE_TOOLCHAIN_FILE="$SRC/CMake/haiku.cmake" \
	-DHAIKU_CROSS_DIR="$HAIKU_CROSS_DIR" -DHAIKU_SRC="$HAIKU_SRC" \
	-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DNIX_HOST_PROFILE=haiku >"$WORK/cfg.log" 2>&1 \
	|| { echo "FAIL: cmake configure"; tail -20 "$WORK/cfg.log"; exit 1; }

CFG="$WORK/cfg-hdrs"; mkdir -p "$CFG"
cp "$WORK/cmake/nix/nix-config.h" "$CFG/"
for f in STATFS FSTATFS GETFSSTAT GETMNTENT GETEXTMNTENT RESETMNTTAB SETLOGIN ISSETUGID \
         GETPEEREID GETPEERUCRED CHFLAGS FCHFLAGS FUTIMES FUTIMESAT GETREUID GETREGID GETRESUID \
         GETRESGID GETFSUID GETFSGID ADJFREQ ADJTIME GETHOSTID SETHOSTID ACCT REVOKE NICE \
         KQUEUE PTHREAD_YIELD; do
	sed -i '' "s|#define HAVE_$f 1|/* #undef HAVE_$f */|" "$CFG/nix-config.h" 2>/dev/null || \
	sed -i "s|#define HAVE_$f 1|/* #undef HAVE_$f */|" "$CFG/nix-config.h" 2>/dev/null
done

INCS="-I$HAIKU_SRC/headers/posix -I$HAIKU_SRC/headers/os -I$HAIKU_SRC/headers/config -I$HAIKU_SRC/headers"
for d in "$HAIKU_SRC"/headers/os/*/; do INCS="$INCS -I$d"; done
LOCAL="-I$SRC/nix -I$CFG -I$SRC/../../LibCPU/include"

# Haiku profile = CORE + BSD family.
FILES="nix-common nix-env nix-init nix-fd nix-io nix-file nix-dir nix-time nix-hostinfo nix-cred \
nix-mem nix-rt-time nix-rt-process nix-process nix-signal nix-socket nix-s5-msg nix-s5-sem nix-s5-shm \
nix-xcpt nix-structs nix-bsd-cred nix-bsd-dir nix-bsd-file nix-bsd-fs nix-bsd-io nix-bsd-mem \
nix-bsd-nfs nix-bsd-obsolete nix-bsd-process nix-bsd-signal nix-bsd-system nix-bsd-time"
ok=0; fail=0; failed=""
for f in $FILES; do
	if "$XGCC" -c -D__HAIKU__ $INCS $LOCAL "$SRC/nix/$f.c" -o "$WORK/$f.o" 2>"$WORK/$f.err"; then
		ok=$((ok + 1)); else fail=$((fail + 1)); failed="$failed $f"; fi
done
total=$((ok + fail))
if [ "$fail" -eq 0 ]; then
	echo "RESULT: PASS -- libnix compiles natively for Haiku ($ok/$total sources, x86_64-unknown-haiku-gcc)"
	exit 0
fi
echo "RESULT: FAIL -- $fail/$total Haiku source(s) failed:$failed"
for f in $failed; do echo "--- $f ---"; grep -E "error:|fatal" "$WORK/$f.err" | head -3; done
exit 1

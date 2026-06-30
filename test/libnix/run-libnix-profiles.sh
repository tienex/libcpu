#!/bin/sh
# Cross-verify the libnix HOST SOURCE SELECTIONS for haiku / os2 / vms by building each host's exact
# source set (via -DNIX_HOST_PROFILE=<host>) on the current BSD-family build host and running the
# host-op conformance test (nixtest). haiku/os2/vms all build the CORE + BSD family; this proves
# that selection compiles, links and passes the conformance test (file ops, mkdir/rmdir, time,
# hostname, credentials, signals, and a loopback TCP self-connect) -- the functional surface a
# "host: working" claim rests on -- even where that host's own toolchain is unavailable here.
#
# The host-specific compat shims (nix-<host>-compat.h) stay host-macro-guarded, so they are inert
# in this proxy; their native effect is exercised only by a host-native ctest. win32 is the one
# host that is fully verified on its own runtime (under Wine -- see run-libnix-wine.sh), because a
# MinGW-w64 cross toolchain is available; haiku/os2/vms have no cross toolchain in this tree.
#
# Usage: run-libnix-profiles.sh [build-dir-prefix]   (default: /tmp/libnix-prof)
set -e

SRC="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${1:-/tmp/libnix-prof}"

# A POSIX/BSD-family build host is required (these profiles build the BSD family). Skip cleanly on
# a host where the family would not compile (e.g. a Windows build host).
case "$(uname -s 2>/dev/null)" in
	Darwin|*BSD|Linux|SunOS) : ;;
	*) echo "SKIP: host-profile cross-verification needs a POSIX/BSD-family build host"; exit 0 ;;
esac

LIBPATHVAR=LD_LIBRARY_PATH
[ "$(uname -s)" = "Darwin" ] && LIBPATHVAR=DYLD_LIBRARY_PATH

rc=0
for prof in haiku os2 vms; do
	B="${PREFIX}-${prof}"
	rm -rf "$B"
	cmake -S "$SRC" -B "$B" -DNIX_HOST_PROFILE="$prof" >/dev/null
	cmake --build "$B" --target nixtest >/dev/null
	out=$(env $LIBPATHVAR="$B/nix:$B/xec-compat" "$B/nixtest" 2>&1 | tail -1)
	if echo "$out" | grep -q "RESULT: PASS"; then
		echo "PASS: libnix host profile '$prof' (CORE+BSD selection) -- nixtest conformance OK"
	else
		echo "FAIL: libnix host profile '$prof' -- $out"
		rc=1
	fi
done
exit $rc

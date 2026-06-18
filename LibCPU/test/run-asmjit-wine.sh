#!/bin/sh
# Cross-compile the AsmJit codegen harness (test/asmjit_win_test.cpp) for Windows
# x86-64 AND x86 with MinGW-w64, then run BOTH under Wine. This proves the AsmJit
# x86/x86-64 backend emits correct code that executes under the real Windows ABIs --
# Microsoft x64 (Win64 / UEFI x64) and cdecl (Win32 / UEFI IA-32) -- which is the
# part the native arm64 / Rosetta-SysV paths cannot reach.
#
# Usage: run-asmjit-wine.sh <repo-root> <work-dir>
# Exit:  0 = both architectures printed "ALL PASS"; non-zero = a failure.
#
# AsmJit's core dispatches to BOTH the arm and x86 instruction backends at runtime
# (it is a cross-assembler), so every arch backend must be linked even when only the
# host one is used. The harness calls CreateAsmjitBackend() directly (no CFBundle).
set -e

ROOT="$1"
WORK="${2:-/tmp}"
INC="-I${ROOT}/include -I${ROOT}/backends/asmjit -I${ROOT}/third_party/asmjit"
AJ="${ROOT}/third_party/asmjit/asmjit"
SRC="${ROOT}/test/asmjit_win_test.cpp ${ROOT}/backends/asmjit/AsmjitBackend.cpp \
     ${AJ}/core/*.cpp ${AJ}/arm/*.cpp ${AJ}/x86/*.cpp ${AJ}/support/*.cpp"
CXXFLAGS="-std=c++20 -O0 -DASMJIT_STATIC ${INC} -static -static-libgcc -static-libstdc++"

# CrossOver's wine needs a bottle; a plain Wine install does not. Prefer an explicit
# $WINE, else use CrossOver (~/bin/wine + CX_BOTTLE) if present, else system wine.
run_wine () {
    exe="$1"
    if [ -n "${WINE}" ]; then
        ${WINE} "${exe}"
    elif [ -x "${HOME}/bin/wine" ]; then
        CX_BOTTLE="${CX_BOTTLE:-default}" "${HOME}/bin/wine" "${exe}"
    else
        wine "${exe}"
    fi
}

status=0
for bits in 64 32; do
    if [ "${bits}" = 64 ]; then CXX=x86_64-w64-mingw32-g++; else CXX=i686-w64-mingw32-g++; fi
    exe="${WORK}/ajtest_win${bits}.exe"
    echo "=== Windows x${bits}: compile (${CXX}) + run under Wine ==="
    # shellcheck disable=SC2086
    ${CXX} ${CXXFLAGS} ${SRC} -o "${exe}"
    if run_wine "${exe}" | tee "${WORK}/ajtest_win${bits}.out" | grep -q "ALL PASS"; then
        echo "x${bits}: PASS"
    else
        echo "x${bits}: FAIL"
        status=1
    fi
done
exit ${status}

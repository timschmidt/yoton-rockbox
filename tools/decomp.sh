#!/bin/sh
# decomp.sh <bin> <base> <out.c> <projdir> [vt_addr nvec extra...]
# Raw ARM Cortex-M binary -> decompiled C via Ghidra headless.
# The Ghidra project is kept in <projdir> (re-usable for later queries).
set -e
H=$(cd "$(dirname "$0")" && pwd)
BIN=$1; BASE=$2; OUT=$3; PROJ=$4; shift 4
mkdir -p "$PROJ"
NAME=$(basename "$BIN" .bin)
"$H"/ghidra_12.2_PUBLIC/support/analyzeHeadless "$PROJ" "$NAME" -import "$BIN" -overwrite \
  -processor ARM:LE:32:Cortex -loader BinaryLoader -loader-baseAddr "$BASE" \
  -scriptPath "$H/gscripts" -preScript SeedCortexM.java "$@" \
  -postScript DecompileAll.java "$(realpath "$OUT")" 2>&1 | tee "$OUT.log" | grep -E "functions:|ERROR|Exception" | head -20

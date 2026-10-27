#!/usr/bin/env bash
# Regenerate firmware/patches/adr-trace.diff:
#   diff -ruN <pristine STM32CubeWL v1.4.0 LoRaWAN_End_Node> <firmware/LoRaWAN_End_Node_ADR>
# restricted to firmware sources (build system / IDE files are not part of the diff).
#
# usage: tools/gen_adr_trace_diff.sh [path/to/STM32CubeWL]   (default: ../third_party/STM32CubeWL)
#
# Apply on a pristine tree:  cd <pristine LoRaWAN_End_Node_ADR> && patch -p2 < adr-trace.diff
# Revert on this tree:       cd firmware/LoRaWAN_End_Node_ADR && patch -R -p2 < ../patches/adr-trace.diff
set -euo pipefail

FW=$(cd "$(dirname "$0")/.." && pwd)
CUBE=${1:-$FW/../third_party/STM32CubeWL}
PRJ=LoRaWAN_End_Node_ADR
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

"$FW/tools/assemble_from_cube.sh" "$CUBE" "$TMP/a/$PRJ" >/dev/null
mkdir -p "$TMP/b"
cp -r "$FW/$PRJ" "$TMP/b/$PRJ"

cd "$TMP"
set +e
diff -ruN \
  -x build -x .vscode -x .settings -x cmake -x CMakeLists.txt -x CMakePresets.json \
  -x 'README*' -x '*.md' -x .gitignore \
  "a/$PRJ" "b/$PRJ" > "$FW/patches/adr-trace.diff"
rc=$?
set -e
[ $rc -le 1 ] || { echo "diff failed ($rc)" >&2; exit $rc; }

echo "wrote $FW/patches/adr-trace.diff"
grep -E '^diff -ruN' "$FW/patches/adr-trace.diff" | awk '{print "  " $NF}'

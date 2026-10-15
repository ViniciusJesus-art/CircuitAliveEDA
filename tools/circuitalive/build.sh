#!/usr/bin/env bash
set -Eeuo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
if [[ ! -f "$CIRCUITALIVE_WORK/build/circuitalive/CMakeCache.txt" ]]; then
    "$CIRCUITALIVE_ROOT/tools/circuitalive/configure.sh"
fi
mkdir -p "$CIRCUITALIVE_ROOT/.circuitalive/logs"
cmake --build "$CIRCUITALIVE_WORK/build/circuitalive" \
    --parallel "${CIRCUITALIVE_JOBS:-3}" "$@" 2>&1 \
    | tee "$CIRCUITALIVE_ROOT/.circuitalive/logs/build.log"

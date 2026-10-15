#!/usr/bin/env bash
set -Eeuo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
mkdir -p "$CIRCUITALIVE_ROOT/.circuitalive/logs"
exec 9>"$CIRCUITALIVE_ROOT/.circuitalive/build.lock"
if ! flock -n 9; then
    printf 'Já existe uma compilação do CircuitAliveEDA em execução.\n' >&2
    exit 1
fi
if [[ ! -f "$CIRCUITALIVE_WORK/build/circuitalive/CMakeCache.txt" ]]; then
    "$CIRCUITALIVE_ROOT/tools/circuitalive/configure.sh"
fi
cmake --build "$CIRCUITALIVE_WORK/build/circuitalive" \
    --parallel "${CIRCUITALIVE_JOBS:-3}" "$@" 2>&1 \
    | tee "$CIRCUITALIVE_ROOT/.circuitalive/logs/build.log"

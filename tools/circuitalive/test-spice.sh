#!/usr/bin/env bash
set -Eeuo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
export CIRCUITALIVE_CONFIG_HOME="$CIRCUITALIVE_ROOT/.circuitalive/test-config"
source "$CIRCUITALIVE_ROOT/tools/circuitalive/runtime-env.sh"
mkdir -p "$CIRCUITALIVE_ROOT/.circuitalive/logs"
ctest --test-dir "$CIRCUITALIVE_WORK/build/circuitalive" \
    --output-on-failure --no-tests=error --timeout 600 -R '^qa_spice$' "$@" 2>&1 \
    | tee "$CIRCUITALIVE_ROOT/.circuitalive/logs/test-spice.log"

#!/usr/bin/env bash
set -Eeuo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
mkdir -p "$CIRCUITALIVE_ROOT/.circuitalive/logs"
ctest --test-dir "$CIRCUITALIVE_WORK/build/circuitalive" \
    --output-on-failure --no-tests=error --timeout 120 -R '^qa_interactive_sim$' "$@" 2>&1 \
    | tee "$CIRCUITALIVE_ROOT/.circuitalive/logs/test-interactive.log"

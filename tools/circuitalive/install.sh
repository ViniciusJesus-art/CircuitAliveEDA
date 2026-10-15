#!/usr/bin/env bash
set -Eeuo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
mkdir -p "$CIRCUITALIVE_ROOT/.circuitalive/logs"
cmake --install "$CIRCUITALIVE_WORK/build/circuitalive" "$@" 2>&1 \
    | tee "$CIRCUITALIVE_ROOT/.circuitalive/logs/install.log"
source "$CIRCUITALIVE_ROOT/tools/circuitalive/runtime-env.sh"

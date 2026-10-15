#!/usr/bin/env bash
set -Eeuo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
mkdir -p "$CIRCUITALIVE_ROOT/.circuitalive/logs"
preset=circuitalive-fedora
if [[ -d "$CIRCUITALIVE_WORK/.circuitalive/deps/usr" ]]; then
    preset=circuitalive-fedora-local
fi
cmake -S "$CIRCUITALIVE_WORK" --preset "$preset" "$@" 2>&1 \
    | tee "$CIRCUITALIVE_ROOT/.circuitalive/logs/configure.log"

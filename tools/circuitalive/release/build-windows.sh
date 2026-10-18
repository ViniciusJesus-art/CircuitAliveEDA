#!/usr/bin/env bash
# Compatibility entrypoint; Windows binaries and dependencies now use MSVC.
set -Eeuo pipefail
exec pwsh -NoProfile -File tools/circuitalive/release/build-windows.ps1 \
    -Version "${1:-10.0.7-alpha.1}" -Phase "${2:-all}"

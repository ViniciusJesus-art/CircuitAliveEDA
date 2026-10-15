#!/usr/bin/env bash
# Source this file from the CircuitAliveEDA helper scripts.
CIRCUITALIVE_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)"
# Some upstream build tools do not quote paths containing spaces. Keep all files
# in the project and give those tools a stable, reproducible path without spaces.
CIRCUITALIVE_PATH_KEY="$(printf '%s' "$CIRCUITALIVE_ROOT" | sha256sum | cut -c1-12)"
CIRCUITALIVE_WORK="/tmp/circuitalive-${UID}-${CIRCUITALIVE_PATH_KEY}"
if [[ -L "$CIRCUITALIVE_WORK" ]]; then
    [[ "$(readlink -- "$CIRCUITALIVE_WORK")" == "$CIRCUITALIVE_ROOT" ]] || {
        printf 'Atalho ocupado: %s\n' "$CIRCUITALIVE_WORK" >&2
        return 1
    }
elif [[ -e "$CIRCUITALIVE_WORK" ]]; then
    printf 'Caminho ocupado: %s\n' "$CIRCUITALIVE_WORK" >&2
    return 1
else
    ln -s -- "$CIRCUITALIVE_ROOT" "$CIRCUITALIVE_WORK"
fi
export CIRCUITALIVE_ROOT CIRCUITALIVE_WORK
export CCACHE_DIR="$CIRCUITALIVE_WORK/.circuitalive/ccache"
export CCACHE_MAXSIZE=5G
export CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_ctime,include_file_mtime
export GH_CONFIG_DIR="$CIRCUITALIVE_ROOT/.circuitalive/github-auth"
if [[ -d "$CIRCUITALIVE_WORK/.circuitalive/deps/usr" ]]; then
    CIRCUITALIVE_DEPS="$CIRCUITALIVE_WORK/.circuitalive/deps/usr"
    export PATH="$CIRCUITALIVE_DEPS/bin:$PATH"
    export LD_LIBRARY_PATH="$CIRCUITALIVE_DEPS/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export PKG_CONFIG_PATH="$CIRCUITALIVE_DEPS/lib64/pkgconfig:$CIRCUITALIVE_DEPS/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    if [[ -d "$CIRCUITALIVE_DEPS/share/swig/4.4.1" ]]; then
        export SWIG_LIB="$CIRCUITALIVE_DEPS/share/swig/4.4.1"
    fi
fi

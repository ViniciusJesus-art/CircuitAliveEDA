#!/usr/bin/env bash
set -Eeuo pipefail
release_version=${1:-10.0.7-alpha.1}
release_phase=${2:-all}
case "$release_phase" in all|configure|test|build|package) ;; *) exit 2 ;; esac
release_stage=$(cygpath -m "$PWD/stage")
if [[ "$release_phase" == all || "$release_phase" == configure ]]; then
cmake -S . -B build/windows -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG' -DCMAKE_C_FLAGS_RELEASE='-O2 -DNDEBUG' \
    -DCMAKE_INSTALL_PREFIX="$release_stage" -DDEFAULT_INSTALL_PATH="$release_stage" \
    -DCMAKE_PREFIX_PATH="$(cygpath -m "$MINGW_PREFIX")" \
    -DwxWidgets_CONFIG_EXECUTABLE="$MINGW_PREFIX/bin/wx-config-3.2" \
    -DOCC_INCLUDE_DIR="$MINGW_PREFIX/include/opencascade" \
    -DPYTHON_EXECUTABLE="$MINGW_PREFIX/bin/python.exe" -DPYTHON_ROOT_DIR="$MINGW_PREFIX" \
    -DBoost_USE_STATIC_LIBS=OFF -DNGSPICE_ROOT_DIR="$MINGW_PREFIX" -DCA_NGSPICE_CIDER=ON -DKICAD_VERSION_EXTRA=CircuitAliveEDA \
    -DCIRCUITALIVE_RELEASE_VERSION="$release_version" \
    -DKICAD_CONFIG_DIR=circuitalive-eda -DKICAD_BUILD_I18N=ON \
    -DKICAD_BUILD_QA_TESTS=ON -DKICAD_SPICE_QA=ON -DKICAD_SCRIPTING_WXPYTHON=ON \
    -DKICAD_USE_SENTRY=OFF -DKICAD_UPDATE_CHECK=OFF -DUSE_CCACHE=ON
cmake --build build/windows --parallel 2 --target qa_interactive_sim
fi
if [[ "$release_phase" == all || "$release_phase" == test ]]; then
python tools/circuitalive/release/prepare-runtime.py build/windows/eeschema/sim "$MINGW_PREFIX/lib/ngspice"
export SPICE_SCRIPTS="$(cygpath -m "$PWD/build/windows/eeschema/sim/ngspice/scripts")"
build/windows/qa/tests/spice/qa_interactive_sim.exe --report_level=short --log_level=error
fi
if [[ "$release_phase" == all || "$release_phase" == build ]]; then
# The complete interactive regression has passed; build the shipping applications.
cmake -S . -B build/windows -DKICAD_BUILD_QA_TESTS=OFF
cmake --build build/windows --parallel 2
fi
if [[ "$release_phase" == all || "$release_phase" == package ]]; then
cmake --install build/windows
python tools/circuitalive/release/package-windows.py stage "$MINGW_PREFIX" "$release_version"
fi

#!/usr/bin/env bash
set -Eeuo pipefail
release_version=${1:-0.1.0-preview.1}
release_stage=$(cygpath -m "$PWD/stage")
cmake -S . -B build/windows -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG' -DCMAKE_C_FLAGS_RELEASE='-O2 -DNDEBUG' \
    -DCMAKE_INSTALL_PREFIX="$release_stage" -DDEFAULT_INSTALL_PATH="$release_stage" \
    -DCMAKE_PREFIX_PATH="$(cygpath -m "$MINGW_PREFIX")" \
    -DwxWidgets_CONFIG_EXECUTABLE="$MINGW_PREFIX/bin/wx-config-3.2" \
    -DOCC_INCLUDE_DIR="$MINGW_PREFIX/include/opencascade" \
    -DPYTHON_EXECUTABLE="$MINGW_PREFIX/bin/python.exe" -DPYTHON_ROOT_DIR="$MINGW_PREFIX" \
    -DNGSPICE_ROOT_DIR="$MINGW_PREFIX" -DKICAD_VERSION_EXTRA=CircuitAliveEDA \
    -DKICAD_CONFIG_DIR=circuitalive-eda -DKICAD_BUILD_I18N=ON \
    -DKICAD_BUILD_QA_TESTS=ON -DKICAD_SPICE_QA=ON -DKICAD_SCRIPTING_WXPYTHON=ON \
    -DKICAD_USE_SENTRY=OFF -DKICAD_UPDATE_CHECK=OFF -DUSE_CCACHE=ON
cmake --build build/windows --parallel 2 --target qa_interactive_sim
python tools/circuitalive/release/prepare-runtime.py build/windows/eeschema/sim "$MINGW_PREFIX/lib/ngspice"
export SPICE_SCRIPTS="$PWD/build/windows/eeschema/sim/ngspice/scripts"
build/windows/qa/tests/spice/qa_interactive_sim.exe --report_level=short --log_level=error
cmake --build build/windows --parallel 2
cmake --install build/windows
python tools/circuitalive/release/package-windows.py stage "$MINGW_PREFIX" "$release_version"

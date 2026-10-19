#!/usr/bin/env bash
# Executed only in the Fedora 44 release container, not on a developer's host.
set -Eeuo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.."
if [[ $EUID -ne 0 || $(uname -m) != x86_64 ]]; then
    printf 'Use the Fedora 44 x86_64 release container as root.\n' >&2
    exit 1
fi
source /etc/os-release
if [[ $ID != fedora || $VERSION_ID != 44 ]]; then
    printf 'This release profile requires Fedora 44.\n' >&2
    exit 1
fi
: "${RELEASE_VERSION:?Missing release version}"
: "${RELEASE_COMMIT:?Missing source commit}"
formats=${RELEASE_FORMATS:-rpm,appimage}
case "$formats" in
    rpm|appimage|rpm,appimage) ;;
    *) printf 'Select rpm, appimage or rpm,appimage for the Fedora build.\n' >&2; exit 1 ;;
esac
if [[ ! $RELEASE_VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+$ ]]; then
    printf 'Invalid alpha release version.\n' >&2
    exit 1
fi
mkdir -p .circuitalive/logs
exec > >(tee .circuitalive/logs/linux-release.log) 2>&1
dnf install -y git ca-certificates curl python3 rpm-build cpio tar gzip zstd \
    binutils findutils which diffutils make desktop-file-utils python3-six python3-typing-extensions
bash tools/circuitalive/dependencies-fedora.sh --system --assumeyes
source tools/circuitalive/env.sh
# The checkout belongs to the hosted runner user; the container builds as root.
git config --global --add safe.directory "$PWD"
git config --global --add safe.directory "$CIRCUITALIVE_WORK"
if [[ $(git rev-parse HEAD) != "$RELEASE_COMMIT" ]]; then
    printf 'Checkout does not match the release commit.\n' >&2
    exit 1
fi
# The bundled private code-model headers currently match ngspice 47 on Linux.
if [[ $(pkg-config --modversion ngspice) != 47 ]]; then
    printf 'Revalidate the Linux code-model ABI before using another ngspice version.\n' >&2
    exit 1
fi
export CCACHE_BASEDIR="$PWD" CCACHE_MAXSIZE=2G
jobs=${CIRCUITALIVE_JOBS:-2}
prefix="$PWD/stage/linux"
cmake --preset circuitalive-fedora \
    -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG' \
    '-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG' \
    "-DCMAKE_INSTALL_PREFIX=$prefix" "-DCIRCUITALIVE_RELEASE_VERSION=$RELEASE_VERSION"
cmake --build build/circuitalive --parallel "$jobs" --target qa_interactive_sim
python3 tools/circuitalive/release/prepare-runtime.py build/circuitalive/eeschema/sim /usr/lib64/ngspice
export SPICE_SCRIPTS="$PWD/build/circuitalive/eeschema/sim/ngspice/scripts"
ctest --test-dir build/circuitalive --output-on-failure --no-tests=error -R '^qa_interactive_sim$'
cmake --build build/circuitalive --parallel "$jobs"
cmake --install build/circuitalive
python3 tools/circuitalive/release/prepare-runtime.py "$prefix/bin" /usr/lib64/ngspice
python3 tools/circuitalive/prepare-local-libraries.py --download
package_args=(
    --prefix "$prefix" --libraries "$PWD/.circuitalive/libraries/usr/share/kicad" \
    --version "$RELEASE_VERSION" --commit "$RELEASE_COMMIT" --output "$PWD/dist" --skip-tar \
)
if [[ $formats == appimage ]]; then package_args+=(--skip-rpm); fi
if [[ $formats == *appimage* ]]; then
    python3 tools/circuitalive/release/prepare-appimage-tools.py --directory .circuitalive/appimage-tools
    package_args+=(--appimagetool .circuitalive/appimage-tools/appimagetool-x86_64.AppImage
                   --appimage-runtime .circuitalive/appimage-tools/runtime-x86_64)
fi
python3 tools/circuitalive/release/package-linux.py "${package_args[@]}"
ccache --show-stats

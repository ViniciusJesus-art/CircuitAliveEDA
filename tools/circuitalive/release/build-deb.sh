#!/usr/bin/env bash
# Native Ubuntu build: Fedora binaries are never repackaged as Debian packages.
set -Eeuo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.."
source /etc/os-release
if [[ $ID != ubuntu || $VERSION_ID != 24.04 || $(uname -m) != x86_64 || ${GITHUB_ACTIONS:-} != true ]]; then
    printf 'This profile runs on an Ubuntu 24.04 GitHub Actions runner.\n' >&2
    exit 1
fi
: "${RELEASE_VERSION:?Missing release version}"
: "${RELEASE_COMMIT:?Missing source commit}"
if [[ ! $RELEASE_VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+$ || $(git rev-parse HEAD) != "$RELEASE_COMMIT" ]]; then
    printf 'Invalid release version or source checkout.\n' >&2
    exit 1
fi
mkdir -p .circuitalive/logs
exec > >(tee .circuitalive/logs/deb-release.log) 2>&1
sudo apt-get update
sudo env DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake ninja-build git ccache pkg-config gettext \
    libwxgtk3.2-dev libwxgtk-webview3.2-dev libboost-all-dev libcairo2-dev libpixman-1-dev \
    libglew-dev libglm-dev freeglut3-dev libglu1-mesa-dev libgtk-3-dev \
    libcurl4-openssl-dev libgit2-dev libsecret-1-dev libspnav-dev \
    libfreetype-dev libharfbuzz-dev libfontconfig1-dev libocct-data-exchange-dev \
    libocct-ocaf-dev libocct-visualization-dev libprotobuf-dev protobuf-compiler \
    unixodbc-dev python3-dev python3-wxgtk4.0 swig zlib1g-dev libzstd-dev \
    libpoppler-dev libpoppler-glib-dev libwayland-dev dpkg-dev \
    autoconf automake libtool bison flex libfftw3-dev libsuitesparse-dev

# Pin both dependencies whose Ubuntu versions are older than the APIs/ABI used here.
ngroot="$PWD/.circuitalive/deb-deps"
python3 - <<'PY'
import hashlib, json, tarfile, urllib.request
from pathlib import Path
import shutil
lock = json.loads(Path('tools/circuitalive/release/linux-toolchain.json').read_text())
for name in ('ubuntu_ngspice', 'ubuntu_nng'):
    entry = lock[name]
    archive = Path('.circuitalive') / (name + '.tar.gz')
    with urllib.request.urlopen(entry['url'], timeout=120) as source, archive.open('wb') as dest:
        shutil.copyfileobj(source, dest)
    with archive.open('rb') as stream:
        if hashlib.file_digest(stream, 'sha256').hexdigest() != entry['sha256']:
            raise SystemExit(f'Source checksum mismatch: {name}')
    with tarfile.open(archive) as source:
        source.extractall('.circuitalive', filter='data')
PY
jobs=${CIRCUITALIVE_JOBS:-2}
cmake -S .circuitalive/nng-1.9.0 -B .circuitalive/nng-build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$ngroot" -DCMAKE_INSTALL_LIBDIR=lib \
    -DBUILD_SHARED_LIBS=ON -DNNG_TESTS=OFF -DNNG_TOOLS=OFF -DNNG_ENABLE_TLS=OFF
cmake --build .circuitalive/nng-build --parallel "$jobs"
cmake --install .circuitalive/nng-build
(
    cd .circuitalive/ngspice-ngspice-47
    ./autogen.sh
    ./configure --prefix="$ngroot" --with-ngshared --enable-xspice --enable-cider \
        --without-x --with-readline=no CFLAGS='-O2'
    make -j "$jobs"
    make install
)
export PATH="$ngroot/bin:$PATH"
export LD_LIBRARY_PATH="$ngroot/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PKG_CONFIG_PATH="$ngroot/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export CCACHE_DIR="$PWD/.circuitalive/ccache-deb" CCACHE_BASEDIR="$PWD" CCACHE_MAXSIZE=2G DPKG_DEB_THREADS_MAX=2
cmake -S . -B build/deb -G Ninja \
    -DCMAKE_BUILD_TYPE=Release '-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG' \
    '-DCMAKE_C_FLAGS_RELEASE=-O2 -DNDEBUG' -DCMAKE_INSTALL_PREFIX=/opt/circuitaliveeda \
    -DCMAKE_INSTALL_LIBDIR=lib -DPYTHON_SITE_PACKAGE_PATH=lib/python3/dist-packages \
    -DPYTHON_DEST=/opt/circuitaliveeda/lib/python3/dist-packages \
    -DPYTHON_FULL_DEST=/opt/circuitaliveeda/lib/python3/dist-packages \
    "-DCMAKE_PREFIX_PATH=$ngroot" "-DNGSPICE_LIBRARY=$ngroot/lib/libngspice.so" \
    "-DNNG_LIBRARY=$ngroot/lib/libnng.so" "-DNNG_INCLUDE_DIR=$ngroot/include" \
    "-DNGSPICE_INCLUDE_DIR=$ngroot/include" "-DCIRCUITALIVE_RELEASE_VERSION=$RELEASE_VERSION" \
    -DKICAD_VERSION_EXTRA=CircuitAliveEDA -DKICAD_CONFIG_DIR=circuitalive-eda \
    -DKICAD_BUILD_QA_TESTS=ON -DKICAD_SPICE_QA=ON -DKICAD_BUILD_I18N=ON \
    -DKICAD_SCRIPTING_WXPYTHON=ON -DKICAD_IPC_API=ON -DKICAD_USE_SENTRY=OFF \
    -DKICAD_UPDATE_CHECK=OFF -DKICAD_INSTALL_DEMOS=ON -DUSE_CCACHE=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_JOB_POOLS=link=1 -DCMAKE_JOB_POOL_LINK=link
cmake --build build/deb --parallel "$jobs" --target qa_interactive_sim
python3 tools/circuitalive/release/prepare-runtime.py build/deb/eeschema/sim "$ngroot/lib/ngspice"
export SPICE_SCRIPTS="$PWD/build/deb/eeschema/sim/ngspice/scripts"
ctest --test-dir build/deb --output-on-failure --no-tests=error -R '^qa_interactive_sim$'
cmake --build build/deb --parallel "$jobs"
DESTDIR="$PWD/stage/deb" cmake --install build/deb
python3 tools/circuitalive/release/prepare-source-libraries.py --directory .circuitalive/deb-libraries
python3 tools/circuitalive/release/package-deb.py --stage stage/deb \
    --libraries .circuitalive/deb-libraries --ngspice "$ngroot" \
    --version "$RELEASE_VERSION" --commit "$RELEASE_COMMIT" --output dist
ccache --show-stats

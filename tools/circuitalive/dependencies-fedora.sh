#!/usr/bin/env bash
set -Eeuo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
packages=(
    gcc-c++ cmake ninja-build git pkgconf-pkg-config ccache gettext
    wxGTK-devel wxBase-devel boost-devel cairo-devel pixman-devel
    glew-devel glm-devel freeglut-devel mesa-libGLU-devel gtk3-devel
    libcurl-devel libgit2-devel libsecret-devel libspnav-devel
    freetype-devel harfbuzz-devel fontconfig-devel opencascade-devel
    protobuf-devel protobuf-compiler nng-devel unixODBC-devel
    python3-devel python3-wxpython4 swig zlib-ng-compat-devel libzstd-devel
    poppler-devel poppler-glib-devel wayland-devel
    libngspice libngspice-devel ngspice ngspice-codemodel shared-mime-info
)
case "${1:---system}" in
    --system)
        exec sudo dnf install "${packages[@]}"
        ;;
    --local)
        # Build headers and tools are extracted locally; component libraries
        # have their own preparation command and never require system KiCad.
        mkdir -p "$CIRCUITALIVE_ROOT/.circuitalive/rpms"
        dnf --repo=fedora --repo=updates download --arch=x86_64 \
            --destdir="$CIRCUITALIVE_ROOT/.circuitalive/rpms" \
            wxGTK-devel wxBase-devel boost-devel libcurl-devel opencascade-devel \
            protobuf-devel protobuf-compiler nng-devel-1.9.0-5.fc44 \
            libngspice-devel-47-1.fc44 ngspice-47-1.fc44 ngspice-codemodel-47-1.fc44 \
            libgit2-devel-1.9.7-1.fc44 swig ccache libspnav-devel unixODBC-devel \
            libsecret-devel-0.21.7-10.fc44 poppler-devel poppler-glib-devel \
            wayland-devel gettext gettext-libs \
            gnutls-devel nettle-devel libtasn1-devel p11-kit-devel libidn2-devel gmp-devel \
            xorg-x11-server-Xvfb gh
        exec python3 "$CIRCUITALIVE_ROOT/tools/circuitalive/prepare-local-deps.py"
        ;;
    *)
        printf 'Uso: %s [--system|--local]\n' "$0" >&2
        exit 2
        ;;
esac

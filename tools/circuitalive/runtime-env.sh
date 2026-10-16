#!/usr/bin/env bash
# Source after env.sh to configure the locally built KiCad applications.
export KICAD_CONFIG_HOME="${CIRCUITALIVE_CONFIG_HOME:-$CIRCUITALIVE_ROOT/.circuitalive/config}"
export KICAD_CACHE_HOME="$CIRCUITALIVE_ROOT/.circuitalive/cache"
export KICAD_DOCUMENTS_HOME="$CIRCUITALIVE_ROOT/.circuitalive/documents"
export KICAD10_SYMBOL_DIR="${CIRCUITALIVE_LIBRARY_DIR:-/usr/share/kicad}/symbols"
export KICAD10_FOOTPRINT_DIR="${CIRCUITALIVE_LIBRARY_DIR:-/usr/share/kicad}/footprints"
export KICAD10_3DMODEL_DIR="${CIRCUITALIVE_LIBRARY_DIR:-/usr/share/kicad}/3dmodels"
export KICAD10_TEMPLATE_DIR="${CIRCUITALIVE_LIBRARY_DIR:-/usr/share/kicad}/template"
# Keep model development live in this checkout, including when using the local installation.
export CIRCUITALIVE_BLOCK_DIR="${CIRCUITALIVE_BLOCK_DIR:-$CIRCUITALIVE_ROOT/simulation/blocks}"
export CIRCUITALIVE_MODEL_DIR="${CIRCUITALIVE_MODEL_DIR:-$CIRCUITALIVE_ROOT/simulation/models}"
mkdir -p "$KICAD_CONFIG_HOME/10.0" "$KICAD_CACHE_HOME" "$KICAD_DOCUMENTS_HOME"
for table in sym-lib-table fp-lib-table; do
    if [[ ! -e "$KICAD_CONFIG_HOME/10.0/$table" && -f "$KICAD10_TEMPLATE_DIR/$table" ]]; then
        cp -- "$KICAD10_TEMPLATE_DIR/$table" "$KICAD_CONFIG_HOME/10.0/$table"
    fi
done

# KiCad's code model search includes a directory next to each executable.
# Fedora installs these XSPICE modules in lib64, outside that search list.
if [[ -f /usr/lib64/ngspice/spice2poly.cm ]]; then
    for directory in \
        "$CIRCUITALIVE_WORK/build/circuitalive/eeschema" \
        "$CIRCUITALIVE_WORK/build/circuitalive/qa/tests/spice" \
        "$CIRCUITALIVE_WORK/.circuitalive/install/bin"; do
        if [[ -d "$directory" && ! -e "$directory/ngspice" && ! -L "$directory/ngspice" ]]; then
            ln -s /usr/lib64/ngspice "$directory/ngspice"
        fi
    done
fi

#!/usr/bin/env bash
# Source after env.sh to configure the locally built KiCad applications.
export KICAD_CONFIG_HOME="${CIRCUITALIVE_CONFIG_HOME:-$CIRCUITALIVE_ROOT/.circuitalive/config}"
export KICAD_CACHE_HOME="$CIRCUITALIVE_ROOT/.circuitalive/cache"
export KICAD_DOCUMENTS_HOME="$CIRCUITALIVE_ROOT/.circuitalive/documents"
# Component data belongs to this installation, independently of Fedora's KiCad.
export CIRCUITALIVE_LIBRARY_DIR="${CIRCUITALIVE_LIBRARY_DIR:-$CIRCUITALIVE_ROOT/.circuitalive/libraries/usr/share/kicad}"
export KICAD10_SYMBOL_DIR="$CIRCUITALIVE_LIBRARY_DIR/symbols"
export KICAD10_FOOTPRINT_DIR="$CIRCUITALIVE_LIBRARY_DIR/footprints"
export KICAD10_3DMODEL_DIR="$CIRCUITALIVE_LIBRARY_DIR/3dmodels"
export KICAD10_TEMPLATE_DIR="$CIRCUITALIVE_LIBRARY_DIR/template"
# Keep model development live in this checkout, including when using the local installation.
export CIRCUITALIVE_BLOCK_DIR="${CIRCUITALIVE_BLOCK_DIR:-$CIRCUITALIVE_ROOT/simulation/blocks}"
export CIRCUITALIVE_MODEL_DIR="${CIRCUITALIVE_MODEL_DIR:-$CIRCUITALIVE_ROOT/simulation/models}"
mkdir -p "$KICAD_CONFIG_HOME/10.0" "$KICAD_CACHE_HOME" "$KICAD_DOCUMENTS_HOME"
for table in sym-lib-table fp-lib-table; do
    if [[ ! -e "$KICAD_CONFIG_HOME/10.0/$table" && -f "$KICAD10_TEMPLATE_DIR/$table" ]]; then
        cp -- "$KICAD10_TEMPLATE_DIR/$table" "$KICAD_CONFIG_HOME/10.0/$table"
    fi
done

# Both the editor and the isolated worker need the matching XSPICE code models.
# Prefer the local dependency set, and repair obsolete system-directory links.
CIRCUITALIVE_CODEMODELS=
for candidate in "$CIRCUITALIVE_WORK/.circuitalive/deps/usr/lib64/ngspice" /usr/lib64/ngspice; do
    if [[ -f "$candidate/spice2poly.cm" ]]; then
        CIRCUITALIVE_CODEMODELS="$candidate"
        break
    fi
done
if [[ -n "$CIRCUITALIVE_CODEMODELS" ]]; then
    for directory in \
        "$CIRCUITALIVE_WORK/build/circuitalive/eeschema" \
        "$CIRCUITALIVE_WORK/build/circuitalive/eeschema/sim" \
        "$CIRCUITALIVE_WORK/build/circuitalive/qa/tests/spice" \
        "$CIRCUITALIVE_WORK/.circuitalive/install/bin"; do
        if [[ -d "$directory" && ! -e "$directory/ngspice" ]]; then
            if [[ -L "$directory/ngspice" ]]; then
                rm -- "$directory/ngspice"
            fi
            ln -s "$CIRCUITALIVE_CODEMODELS" "$directory/ngspice"
        fi
    done
fi

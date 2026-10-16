#!/usr/bin/env bash
set -Eeuo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
circuitalive_applications="${XDG_DATA_HOME:-$HOME/.local/share}/applications"
mkdir -p "$circuitalive_applications"
python3 - "$CIRCUITALIVE_ROOT" "$circuitalive_applications" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
destination = Path(sys.argv[2]) / 'org.circuitaliveeda.CircuitAliveEDA.desktop'
launcher = str(root / 'circuitalive')
# Desktop Exec has its own quoting and percent expansion; it is not a shell command.
quoted = launcher.replace('\\', '\\\\').replace('"', '\\"').replace('`', '\\`').replace('$', '\\$').replace('%', '%%')
icon = root / '.circuitalive/install/share/icons/hicolor/128x128/apps/circuitaliveeda.png'
if not icon.is_file():
    raise SystemExit('Install CircuitAliveEDA before installing the desktop entry.')
destination.write_text(f'''[Desktop Entry]
Version=1.0
Type=Application
Name=CircuitAliveEDA
GenericName=Electronics Design and Interactive Simulation
Comment=Electronic schematics, interactive simulation and PCB design
Exec="{quoted}" %f
TryExec={launcher}
Icon={icon}
Path={root}
Terminal=false
Categories=Science;Electronics;
Keywords=EDA;PCB;Schematic;Simulation;CircuitAliveEDA;
StartupWMClass=CircuitAliveEDA
''')
print(destination)
PY
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$circuitalive_applications"
fi

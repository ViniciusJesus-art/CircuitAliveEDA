#!/usr/bin/env python3
"""GUI regression: unsaved label edits must sync on every PCB focus.

Run after sourcing tools/circuitalive/env.sh and runtime-env.sh. Requires
Xvfb, xdotool, ImageMagick and tesseract; uses only a temporary project.
Set SMOKE_INSTALLED=1 to check the local installation instead of the build.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[3]
work = Path(tempfile.mkdtemp(prefix='circuitalive-focus-sync-'))
print('Smoke artifacts:', work, flush=True)
config = work / 'config' / '10.0'
shutil.copytree(root / '.circuitalive/config/10.0', config)
common = json.loads((config / 'kicad_common.json').read_text())
common['system']['language'] = 'English'
(config / 'kicad_common.json').write_text(json.dumps(common))
project = work / 'window_smoke.kicad_pro'
project.write_text('{}')
def expression_at(text, start):
    depth, quoted, escaped = 0, False, False
    for index in range(start, len(text)):
        char = text[index]
        if quoted:
            if escaped: escaped = False
            elif char == "\\": escaped = True
            elif char == '"': quoted = False
        elif char == '"': quoted = True
        elif char == '(': depth += 1
        elif char == ')':
            depth -= 1
            if depth == 0: return text[start:index + 1]
    raise ValueError('Unbalanced fixture expression')

# Use the embedded capacitor symbol from an existing test, without library downloads.
fixture = (root / 'qa/data/eeschema/spice_netlists/fliege_filter/fliege_filter.kicad_sch').read_text()
capacitor = expression_at(fixture, fixture.index('(symbol "Device:C"'))
(work / 'window_smoke.kicad_sch').write_text(f'''(kicad_sch
  (version 20250114) (generator "eeschema")
  (uuid "10000000-0000-4000-8000-000000000001") (paper "A4")
  (lib_symbols {capacitor})
  (symbol (lib_id "Device:C") (at 50 50 0) (unit 1)
    (in_bom yes) (on_board yes) (dnp no)
    (uuid "20000000-0000-4000-8000-000000000001")
    (property "Reference" "C1" (at 55 49 0) (effects (font (size 1.27 1.27))))
    (property "Value" "100nF" (at 55 51 0) (effects (font (size 1.27 1.27))))
    (property "Footprint" "Test:Cap" (at 50 50 0) (effects (font (size 1.27 1.27))) (hide yes))
    (instances (project "window_smoke"
      (path "/10000000-0000-4000-8000-000000000001" (reference "C1") (unit 1)))))
  (global_label "VCC_A" (shape input) (at 50 46.19 0)
    (uuid "30000000-0000-4000-8000-000000000001")
    (effects (font (size 1.27 1.27)) (justify left)))
  (global_label "GND" (shape input) (at 50 53.81 0)
    (uuid "30000000-0000-4000-8000-000000000002")
    (effects (font (size 1.27 1.27)) (justify left)))
  (sheet_instances (path "/" (page "1"))) (embedded_fonts no))''')
(work / 'window_smoke.kicad_pcb').write_text('''(kicad_pcb
  (version 20260206) (generator "pcbnew")
  (general (thickness 1.6)) (paper "A4")
  (layers (0 "F.Cu" signal) (2 "B.Cu" signal)
    (5 "F.SilkS" user) (7 "B.SilkS" user)
    (1 "F.Mask" user) (3 "B.Mask" user)
    (19 "Cmts.User" user) (25 "Edge.Cuts" user))
  (setup (pad_to_mask_clearance 0)) (net 0 "")
  (footprint "Test:Cap" (layer "F.Cu") (at 50 50)
    (uuid "40000000-0000-4000-8000-000000000001")
    (property "Reference" "C1" (at 0 -2 0) (layer "F.SilkS")
      (effects (font (size 1 1) (thickness 0.15))))
    (property "Value" "100nF" (at 0 2 0) (layer "F.Fab")
      (effects (font (size 1 1) (thickness 0.15))))
    (pad "1" smd rect (at 0 0) (size 2 2) (layers "F.Cu" "F.Paste" "F.Mask"))
    (pad "2" smd rect (at 5 0) (size 2 2) (layers "F.Cu" "F.Paste" "F.Mask"))))''')
env = os.environ.copy()
env.update(KICAD_CONFIG_HOME=str(work / 'config'), KICAD_CACHE_HOME=str(work / 'cache'),
           KICAD_DOCUMENTS_HOME=str(work / 'documents'), KICAD_RUN_FROM_BUILD_DIR='1',
           KICAD_SOFTWARE_RENDERING='1', GDK_BACKEND='x11', LIBGL_ALWAYS_SOFTWARE='1')
if os.environ.get('SMOKE_INSTALLED') == '1':
    env.pop('KICAD_RUN_FROM_BUILD_DIR', None)
    env['LD_LIBRARY_PATH'] = str(root / '.circuitalive/install/lib64') + ':' + env.get('LD_LIBRARY_PATH', '')
env.pop('WAYLAND_DISPLAY', None)
read_fd, write_fd = os.pipe()
xlog = (work / 'xvfb.log').open('w')
xserver = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-ac', '-screen', '0',
                            '1600x1000x24', '-nolisten', 'tcp'],
                           env=env, pass_fds=(write_fd,), stdout=xlog, stderr=xlog)
os.close(write_fd)
display = os.read(read_fd, 64).decode().strip()
os.close(read_fd)
assert display, (work / 'xvfb.log').read_text()
env['DISPLAY'] = ':' + display
env.pop('XAUTHORITY', None)
app = None

def xdo(*args, check=True):
    result = subprocess.run(['xdotool', *map(str, args)], env=env, capture_output=True, text=True, timeout=10)
    if check and result.returncode:
        raise RuntimeError(result.stderr)
    return result.stdout.strip()

def windows():
    ids = xdo('search', '--onlyvisible', '--name', '.', check=False).splitlines()
    return {wid: xdo('getwindowname', wid, check=False) for wid in ids}

def find(title):
    return next((wid for wid, name in windows().items() if title in name), None)

def wait_window(title, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        wid = find(title)
        if wid:
            return wid
        time.sleep(.2)
    raise AssertionError(f'Missing {title}: {windows()}')

def focus(wid):
    xdo('windowraise', wid)
    xdo('windowfocus', '--sync', wid)
    time.sleep(.7)

def keys(wid, key):
    focus(wid)
    xdo('key', '--clearmodifiers', key)

def screenshot(wid, name):
    path = work / (name + '.png')
    subprocess.run(['import', '-window', wid, str(path)], env=env, check=True)
    return path

def label_position():
    import csv, io, re
    path = screenshot(schematic, 'schematic')
    result = subprocess.run(['tesseract', str(path), 'stdout', '--psm', '11', 'tsv'],
                            env=env, capture_output=True, text=True, check=True)
    for word in csv.DictReader(io.StringIO(result.stdout), delimiter='\t'):
        if re.sub('[^A-Z]', '', word['text'].upper()).startswith('VCC'):
            return int(word['left']) + int(word['width']) // 2, int(word['top']) + int(word['height']) // 2
    raise AssertionError('Could not locate VCC label: ' + result.stdout)

def rename_label(name, pending_tool=False):
    focus(schematic)
    xdo('key', 'Escape', 'Escape', 'ctrl+Home')
    time.sleep(.5)
    x, y = label_position()
    xdo('mousemove', '--window', schematic, x, y)
    xdo('click', '1')
    time.sleep(.2)
    xdo('key', 'e')
    dialog = wait_window('Label Properties')
    xdo('key', '--clearmodifiers', 'ctrl+a')
    xdo('type', '--clearmodifiers', name)
    xdo('key', 'Return')
    time.sleep(.5)
    assert not find('Label Properties'), windows()
    if pending_tool:
        xdo('mousemove', '--window', schematic, '750', '550')
        xdo('key', 'w')
    # Deliberately do not save the schematic: sync must use the in-memory edits.

def check_pad(expected, region='canvas'):
    import re
    focus(pcb)
    point = {'canvas': ('750', '500'), 'appearance': ('1400', '400'),
             'toolbar': ('20', '45'), 'status': ('700', '875')}[region]
    xdo('mousemove', '--window', pcb, *point)
    xdo('click', '1')
    time.sleep(2)
    xdo('key', '--clearmodifiers', 'ctrl+s')
    time.sleep(.5)
    text = (work / 'window_smoke.kicad_pcb').read_text()
    pad = expression_at(text, text.index('(pad "1"'))
    net = re.search(r'\(net(?: \d+)? "([^"\n]*)"\)', pad).group(1)
    if expected is None:
        assert net.startswith('unconnected-'), net
    else:
        assert net == expected, (expected, net)
    assert '(at 50 50)' in text, 'Capacitor moved during the regression'
    print('PASS: pad 1 =', net, 'after focus via', region, flush=True)

try:
    app_log = (work / 'app.log').open('w')
    binary = '.circuitalive/install/bin/kicad' if os.environ.get('SMOKE_INSTALLED') == '1' else 'build/circuitalive/kicad/kicad'
    app = subprocess.Popen([str(root / binary), '-n', '--software-rendering', str(project)],
                           env=env, stdout=app_log, stderr=app_log)
    manager = wait_window('window_smoke')
    time.sleep(2)
    keys(manager, 'ctrl+p')
    pcb = wait_window('PCB Editor')
    keys(manager, 'ctrl+e')
    schematic = wait_window('Schematic Editor')
    for wid in [pcb, schematic]:
        xdo('windowmove', wid, '0', '0')
        xdo('windowsize', wid, '1500', '900')
    time.sleep(2)
    check_pad('VCC_A')
    rename_label('VCC_B', pending_tool=True)
    check_pad('VCC_B', 'appearance')
    keys(pcb, 'ctrl+shift+l')
    rename_label('VCC_C')
    check_pad('VCC_C', 'status')
    focus(schematic)
    xdo('key', 'Escape', 'Escape', 'ctrl+Home')
    time.sleep(.5)
    x, y = label_position()
    xdo('mousemove', '--window', schematic, x, y)
    xdo('key', 'Delete')
    check_pad(None)
    keys(schematic, 'ctrl+z')
    check_pad('VCC_C', 'toolbar')
    keys(schematic, 'ctrl+y')
    check_pad(None, 'appearance')
    keys(schematic, 'ctrl+z')
    check_pad('VCC_C')
    rename_label('VCC_D', pending_tool=True)
    check_pad('VCC_D', 'status')
    # A no-op return must not dirty the PCB after saving the last real update.
    focus(schematic)
    focus(pcb)
    time.sleep(2)
    assert '*window_smoke' not in xdo('getwindowname', pcb), windows()
    print('PASS: unchanged return leaves PCB clean', flush=True)
except Exception:
    print('Visible windows at failure:', windows(), 'app exit:', app.poll() if app else None, flush=True)
    subprocess.run(['import', '-window', 'root', str(work / 'failure.png')], env=env)
    raise
finally:
    if app is not None and app.poll() is None:
        app.terminate()
        try:
            app.wait(timeout=5)
        except subprocess.TimeoutExpired:
            app.kill()
    xserver.terminate()
    xserver.wait(timeout=5)

"""Create a self-contained per-user NSIS installer from a native MSYS2 build."""
from pathlib import Path
import hashlib,json,os,re,shutil,subprocess,sys
stage,prefix=map(lambda p:Path(p).resolve(),sys.argv[1:3]);version=sys.argv[3]
if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[a-zA-Z0-9.]+)?',version):raise SystemExit('Invalid release version')
root=Path(__file__).resolve().parents[3];dist=root/'dist';dist.mkdir(exist_ok=True)
def copy_dependency(source,destination):
 if not Path(destination).exists():shutil.copy2(source,destination)
 return str(destination)
for dll in (prefix/'bin').glob('*.dll'):copy_dependency(dll,stage/'bin'/dll.name)
for binary in (prefix/'bin').glob('python*.exe'):shutil.copy2(binary,stage/'bin'/binary.name)
for library in (prefix/'lib').glob('python3.*'):
 if library.is_dir():shutil.copytree(library,stage/'lib'/library.name,dirs_exist_ok=True,copy_function=copy_dependency)
shutil.copytree(prefix/'share/licenses',stage/'share/licenses/dependencies',dirs_exist_ok=True)
for license in ('LICENSE.README','LICENSE.GPLv3','LICENSE.GPLv2','AUTHORS.txt'):
 if (root/license).exists():shutil.copy2(root/license,stage/license)
icon_script=stage/'launcher-icon.rc';icon_object=stage/'launcher-icon.o'
icon_script.write_text('1 ICON "'+(root/'resources/bitmaps_png/icons/icon_kicad.ico').as_posix()+'"\n')
subprocess.run(['windres',str(icon_script),str(icon_object)],check=True)
subprocess.run(['g++','-std=c++20','-O2','-municode','-mwindows',str(root/'tools/circuitalive/release/windows-launcher.cpp'),str(icon_object),'-o',str(stage/'bin/CircuitAliveEDA.exe')],check=True)
icon_script.unlink();icon_object.unlink()
models=stage/'bin/ngspice';models.mkdir(exist_ok=True)
for file in (prefix/'lib/ngspice').glob('*.cm'):shutil.copy2(file,models/file.name)
(stage/'PACKAGE-INFO.json').write_text(json.dumps({'product':'CircuitAliveEDA','version':version,'commit':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'platform':'windows-x86_64'},indent=2))
(stage/'DEPENDENCIES.txt').write_bytes(subprocess.check_output(['pacman','-Q']))
if os.environ.get('GITHUB_ACTIONS')=='true':
 shutil.rmtree(root/'build/windows')
installer=dist/f'CircuitAliveEDA-{version}-windows-x86_64-setup.exe'
script=dist/'installer.nsi'
extra=''
for folder in ('symbols','footprints','3dmodels','template'):
 path=prefix/'share/kicad'/folder
 if path.exists():extra+=f'SetOutPath "$INSTDIR\\share\\kicad\\{folder}"\nFile /r "{path}\\*"\n'
script.write_text(f'''Unicode True
!include "MUI2.nsh"
Name "CircuitAliveEDA {version}"
OutFile "{installer}"
InstallDir "$LOCALAPPDATA\\Programs\\CircuitAliveEDA"
RequestExecutionLevel user
SetCompressor /SOLID lzma
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "{root/'LICENSE.GPLv3'}"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "PortugueseBR"
Section "CircuitAliveEDA"
SetShellVarContext current
SetOutPath "$INSTDIR"
File /r /x ".git" /x ".history" /x "*-backups" "{stage}\\*"
{extra}
SetOutPath "$INSTDIR\\bin"
CreateDirectory "$SMPROGRAMS\\CircuitAliveEDA"
CreateShortcut "$SMPROGRAMS\\CircuitAliveEDA\\CircuitAliveEDA.lnk" "$INSTDIR\\bin\\CircuitAliveEDA.exe"
CreateShortcut "$DESKTOP\\CircuitAliveEDA.lnk" "$INSTDIR\\bin\\CircuitAliveEDA.exe"
WriteUninstaller "$INSTDIR\\Uninstall.exe"
WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CircuitAliveEDA" "DisplayName" "CircuitAliveEDA"
WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CircuitAliveEDA" "DisplayVersion" "{version}"
WriteRegStr HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CircuitAliveEDA" "UninstallString" '$"$INSTDIR\\Uninstall.exe$"'
SectionEnd
Section "Uninstall"
Delete "$DESKTOP\\CircuitAliveEDA.lnk"
RMDir /r "$SMPROGRAMS\\CircuitAliveEDA"
RMDir /r "$INSTDIR"
DeleteRegKey HKCU "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\CircuitAliveEDA"
SectionEnd
''')
subprocess.run(['makensis','-V2',str(script)],check=True)
script.unlink()
(installer.with_suffix(installer.suffix+'.sha256')).write_text(hashlib.file_digest(installer.open('rb'),'sha256').hexdigest()+'  '+installer.name+'\n')
# Exercise the actual silent installer and the installed executable, not only the build tree.
smoke=root/'installer smoke'
subprocess.run([str(installer),'/S',f'/D={smoke}'],check=True,timeout=1200)
subprocess.run([str(smoke/'bin/kicad-cli.exe'),'version'],check=True,timeout=60)
python_env=dict(os.environ,PYTHONHOME=str(smoke),
                PYTHONPATH=str(smoke/'lib'/f'python{sys.version_info.major}.{sys.version_info.minor}'/'site-packages'))
subprocess.run([str(smoke/'bin/python.exe'),'-c','import wx, pcbnew; print("Installed Python/wx:", wx.version())'],
               check=True,timeout=60,env=python_env)
subprocess.run([sys.executable,str(root/'tools/circuitalive/release/prepare-runtime.py'),
               str(smoke/'bin'),str(smoke/'bin/ngspice')],check=True)
subprocess.run([sys.executable,str(root/'tools/circuitalive/release/smoke-worker.py'),
               str(smoke/'bin/circuitalive-sim-worker.exe'),'--scripts',str(smoke/'bin/ngspice/scripts')],
               check=True,timeout=150)

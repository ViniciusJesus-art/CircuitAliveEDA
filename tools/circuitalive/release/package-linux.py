#!/usr/bin/env python3
"""Bundle the Fedora 44 x86_64 build, component libraries, Python and runtime dependencies."""
import argparse,hashlib,json,os,re,shutil,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--prefix',type=Path,required=True);p.add_argument('--libraries',type=Path,required=True)
p.add_argument('--version',default='10.0.7-alpha.1');p.add_argument('--commit',required=True)
p.add_argument('--output',type=Path,required=True);p.add_argument('--reuse',action='store_true');args=p.parse_args()
if not re.fullmatch(r'10\.0\.7-alpha\.[0-9]+',args.version):raise SystemExit('Invalid alpha version')
root=Path(__file__).resolve().parents[3];out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
work=out/'work';app=work/'CircuitAliveEDA';app.mkdir(parents=True,exist_ok=True)
ignore=shutil.ignore_patterns('.git','.history','*-backups','*.kicad_prl','~*.lck','__pycache__','*.pyc','*.pyo')
python_version=f'python{sys.version_info.major}.{sys.version_info.minor}'
if args.reuse:
    copied=set(json.loads((app/'PACKAGE-INFO.json').read_text())['bundled_shared_libraries'])
    for binary in (args.prefix/'bin').iterdir():
        if binary.is_file():shutil.copy2(binary,app/'bin'/binary.name)
else:
    shutil.copytree(args.prefix,app,dirs_exist_ok=True,symlinks=False,ignore=ignore)
    for folder in ('symbols','footprints','3dmodels','template'):
     source=args.libraries/folder
     if source.exists():shutil.copytree(source,app/'share/kicad'/folder,dirs_exist_ok=True,symlinks=False,ignore=ignore)
    python_version=f'python{sys.version_info.major}.{sys.version_info.minor}'
    for directory in (Path('/usr/lib')/python_version,Path('/usr/lib64')/python_version):
     if directory.exists():shutil.copytree(directory,app/'lib'/python_version,dirs_exist_ok=True,ignore=shutil.ignore_patterns('site-packages','test','__pycache__','*.pyc'))
    site=app/'lib'/python_version/'site-packages';site.mkdir(parents=True,exist_ok=True)
    wx=root/'.circuitalive/deps/usr/lib64'/python_version/'site-packages/wx'
    if wx.exists():shutil.copytree(wx,site/'wx',dirs_exist_ok=True,ignore=ignore)
    for module in ('six.py','typing_extensions.py'):
     for directory in (Path('/usr/lib')/python_version/'site-packages',Path('/usr/lib64')/python_version/'site-packages'):
      if (directory/module).exists():shutil.copy2(directory/module,site/module);break
    local_licenses=root/'.circuitalive/deps/usr/share/licenses'
    if local_licenses.exists():shutil.copytree(local_licenses,app/'share/licenses/local-dependencies',dirs_exist_ok=True)
    for license in ('LICENSE.README','LICENSE.GPLv2','LICENSE.GPLv3','AUTHORS.txt'):
     if (root/license).exists():shutil.copy2(root/license,app/license)
    # Resolve complete ELF dependency trees while the checkout's matching wx/ngspice runtime is selected.
    exclude=('ld-linux','libc.so','libm.so','libdl.so','libpthread.so','librt.so','libresolv.so','libGL.','libGLX','libEGL.','libOpenGL.','libGLdispatch.','libgbm.','libdrm','libLLVM')
    packages=set();copied=set();env=dict(os.environ)
    env['LD_LIBRARY_PATH']=str(args.prefix/'lib64')+':'+env.get('LD_LIBRARY_PATH','')
    elf=[]
    for file in app.rglob('*'):
     if file.is_file():
      with file.open('rb') as stream:
       if stream.read(4)==b'\x7fELF':elf.append(file)
    for file in elf:
     result=subprocess.run(['ldd',str(file)],text=True,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,env=env)
     for line in result.stdout.splitlines():
      if '=> not found' in line:raise SystemExit(f'Missing dependency for {file}: {line}')
      match=re.match(r'\s*(\S+) => (/\S+) \(',line)
      if not match:continue
      name,path=match.groups()
      if name.startswith(exclude) or name in copied:continue
      copied.add(name);dest=app/'lib64'/name;dest.parent.mkdir(exist_ok=True)
      if not dest.exists():shutil.copy2(path,dest)
      owner=subprocess.run(['rpm','-qf','--qf','%{NAME}\n',path],text=True,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL)
      if owner.returncode==0:packages.update(owner.stdout.splitlines())
    for package in sorted(packages):
     for base in ('licenses','doc'):
      directory=Path('/usr/share')/base/package
      if directory.exists():shutil.copytree(directory,app/'share'/base/package,dirs_exist_ok=True,ignore=ignore)
    (app/'DEPENDENCIES.txt').write_text(subprocess.check_output(['rpm','-q','--qf','%{NAME} %{VERSION}-%{RELEASE} source=%{SOURCERPM}\n',*sorted(packages)],text=True))
(app/'PACKAGE-INFO.json').write_text(json.dumps({'product':'CircuitAliveEDA','version':args.version,'commit':args.commit,'platform':'linux-x86_64','minimum_distribution':'Fedora 44 (glibc 2.43)','bundled_shared_libraries':sorted(copied)},indent=2))
launcher=app/'CircuitAliveEDA'
launcher.write_text('''#!/usr/bin/env bash
set -Eeuo pipefail
bundle_file="$(readlink -f -- "${BASH_SOURCE[0]}")"
bundle_root="$(cd -- "$(dirname -- "$bundle_file")" && pwd -P)"
export LD_LIBRARY_PATH="$bundle_root/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONHOME="$bundle_root"
export PYTHONPATH="$bundle_root/lib/'''+python_version+'''/site-packages${PYTHONPATH:+:$PYTHONPATH}"
export KICAD10_SYMBOL_DIR="$bundle_root/share/kicad/symbols"
export KICAD10_FOOTPRINT_DIR="$bundle_root/share/kicad/footprints"
export KICAD10_3DMODEL_DIR="$bundle_root/share/kicad/3dmodels"
export KICAD10_TEMPLATE_DIR="$bundle_root/share/kicad/template"
export CIRCUITALIVE_MODEL_DIR="$bundle_root/share/circuitalive/models"
export CIRCUITALIVE_BLOCK_DIR="$bundle_root/share/circuitalive/blocks"
export SPICE_SCRIPTS="${XDG_CACHE_HOME:-$HOME/.cache}/circuitaliveeda/ngspice"
mkdir -p "$SPICE_SCRIPTS"
{
 printf '* CircuitAliveEDA bundled runtime\\nunset osdi_enabled\\nset num_threads=2\\n'
 for model in "$bundle_root"/bin/ngspice/*.cm; do printf 'codemodel "%s"\\n' "$model"; done
} > "$SPICE_SCRIPTS/spinit"
app=kicad
case "${1:-}" in
 cli) app=kicad-cli;shift ;;
 eeschema|pcbnew|gerbview|bitmap2component|pcb_calculator|pl_editor|cvpcb) app="$1";shift ;;
esac
exec "$bundle_root/bin/$app" "$@"
''');launcher.chmod(0o755)
install=app/'install.sh'
install.write_text('''#!/usr/bin/env bash
set -Eeuo pipefail
source_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
destination="${1:-$HOME/.local/opt/circuitaliveeda-'''+args.version+'''}"
mkdir -p "$destination" "$HOME/.local/bin" "$HOME/.local/share/applications"
cp -a "$source_root/." "$destination/"
ln -sfn "$destination/CircuitAliveEDA" "$HOME/.local/bin/circuitalive-eda"
cat > "$HOME/.local/share/applications/circuitaliveeda.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=CircuitAliveEDA
Exec="$destination/CircuitAliveEDA" %F
Icon=$destination/share/icons/hicolor/128x128/apps/circuitaliveeda.png
Categories=Development;Electronics;
Terminal=false
EOF
printf 'CircuitAliveEDA installed in %s\\n' "$destination"
''');install.chmod(0o755)
# Smoke-test relocated compiled binaries with the same launcher shipped to users.
smoke_env=dict(os.environ)
for name in ('LD_LIBRARY_PATH','PYTHONPATH','PYTHONHOME','KICAD_RUN_FROM_BUILD_DIR'):smoke_env.pop(name,None)
smoke_env.update(XDG_CACHE_HOME=str(work/'smoke/cache'),XDG_DATA_HOME=str(work/'smoke/data'),KICAD_CONFIG_HOME=str(work/'smoke/config'),KICAD_CACHE_HOME=str(work/'smoke/cache'),KICAD_DOCUMENTS_HOME=str(work/'smoke/documents'),PATH='/usr/bin:/bin')
subprocess.run([str(launcher),'cli','version'],check=True,timeout=60,env=smoke_env)
rpmdir=work/'rpmbuild'
for directory in ('BUILD','BUILDROOT','RPMS','SOURCES','SPECS','SRPMS'):(rpmdir/directory).mkdir(parents=True,exist_ok=True)
spec=rpmdir/'SPECS/circuitaliveeda.spec';numeric=args.version.split('-')[0]
spec.write_text(f'''%global debug_package %{{nil}}
%global __os_install_post %{{nil}}
%global _binary_payload w9T2.zstdio
Name: circuitaliveeda
Version: {numeric}
Release: 0.alpha.1.fc44
Summary: CircuitAliveEDA with interactive circuit simulation
License: GPL-3.0-or-later AND BSD-3-Clause AND LGPL-2.1-or-later
URL: https://github.com/ViniciusJesus-art/CircuitAliveEDA
BuildArch: x86_64
AutoReqProv: no
Requires: glibc >= 2.43, gtk3, fontconfig, libglvnd-glx, libglvnd-egl, libglvnd-opengl, shared-mime-info
%description
CircuitAliveEDA {args.version}, based on version 10.0.7, with bundled models,
component libraries, translations and simulation runtime. Alpha release.
%prep
%build
%install
mkdir -p %{{buildroot}}/opt/circuitaliveeda %{{buildroot}}/usr/bin %{{buildroot}}/usr/share/applications %{{buildroot}}/usr/share/icons/hicolor/128x128/apps
cp -a "{app}/." %{{buildroot}}/opt/circuitaliveeda/
ln -s /opt/circuitaliveeda/CircuitAliveEDA %{{buildroot}}/usr/bin/circuitalive-eda
cat > %{{buildroot}}/usr/share/applications/circuitaliveeda.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=CircuitAliveEDA
Exec=/opt/circuitaliveeda/CircuitAliveEDA %F
Icon=circuitaliveeda
Categories=Development;Electronics;
Terminal=false
EOF
cp "{app}/share/icons/hicolor/128x128/apps/circuitaliveeda.png" %{{buildroot}}/usr/share/icons/hicolor/128x128/apps/
%files
/opt/circuitaliveeda
/usr/bin/circuitalive-eda
/usr/share/applications/circuitaliveeda.desktop
/usr/share/icons/hicolor/128x128/apps/circuitaliveeda.png
''')
temporary=work/'tmp';temporary.mkdir(exist_ok=True)
subprocess.run(['rpmbuild','-bb','--define',f'_topdir {rpmdir}','--define',f'_tmppath {temporary}',str(spec)],check=True)
for file in (rpmdir/'RPMS').rglob('*.rpm'):shutil.copy2(file,out/f'CircuitAliveEDA-{args.version}-fedora44-x86_64.rpm')
subprocess.run(['tar','--zstd','-cf',str(out/f'CircuitAliveEDA-{args.version}-linux-x86_64.tar.zst'),'-C',str(work),app.name],check=True)
for file in out.iterdir():
 if file.is_file() and not file.name.endswith('.sha256'):
  file.with_suffix(file.suffix+'.sha256').write_text(hashlib.file_digest(file.open('rb'),'sha256').hexdigest()+'  '+file.name+'\n')
print('Release packages:',out)

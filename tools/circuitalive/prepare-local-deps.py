#!/usr/bin/env python3
"""Extract Fedora development RPMs locally, reusing installed runtime libraries."""
from pathlib import Path
import hashlib
import os
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
prefix = root / '.circuitalive/deps'


def repair_runtime_links():
    """Reuse ABI symlinks rather than pinning a system library's patch filename."""
    for link in prefix.rglob('*'):
        if not link.is_symlink() or link.exists():
            continue
        target = link.readlink()
        if target.is_absolute():
            system = Path('/') / link.relative_to(prefix)
            if system.exists():
                link.unlink()
                link.symlink_to(system)
            continue
        system = Path('/') / link.relative_to(prefix).parent / target
        if system.exists():
            dest = link.parent / target
            if not dest.exists():
                dest.symlink_to(system)


if sys.argv[1:] == ['--repair-runtime-links']:
    repair_runtime_links()
    raise SystemExit(0)

key = hashlib.sha256(str(root).encode()).hexdigest()[:12]
alias = f'/tmp/circuitalive-{os.getuid()}-{key}'
names = {
    'wxGTK-devel', 'wxBase-devel', 'boost-devel', 'libcurl-devel',
    'opencascade-devel', 'protobuf-devel', 'protobuf-compiler', 'nng-devel',
    'libngspice-devel', 'ngspice', 'libgit2-devel', 'swig', 'ccache',
    'xorg-x11-server-Xvfb', 'gh', 'libspnav-devel', 'unixODBC-devel',
    'libsecret-devel', 'poppler-devel', 'poppler-glib-devel', 'wayland-devel',
    'gnutls-devel', 'nettle-devel', 'libtasn1-devel', 'p11-kit-devel',
    'libidn2-devel', 'gmp-devel',
}
prefix.mkdir(parents=True, exist_ok=True)
selected = []
for rpm in sorted((root / '.circuitalive/rpms').glob('*.rpm')):
    name = subprocess.check_output(
        ['rpm', '-qp', '--queryformat', '%{NAME}', str(rpm)],
        text=True, stderr=subprocess.DEVNULL,
    )
    if name not in names:
        continue
    if name == 'libsecret-devel' and rpm.name != 'libsecret-devel-0.21.7-10.fc44.x86_64.rpm':
        continue
    print(f'Extraindo {rpm.name}', flush=True)
    selected.append(rpm)
    producer = subprocess.Popen(['rpm2cpio', str(rpm)], stdout=subprocess.PIPE)
    consumer = subprocess.run(
        ['cpio', '-idmu', '--quiet', '--no-absolute-filenames'],
        cwd=prefix, stdin=producer.stdout,
    )
    producer.stdout.close()
    if producer.wait() or consumer.returncode:
        raise SystemExit(f'Falha ao extrair {rpm.name}')

# Development symlinks refer to runtime libraries already installed on this host.
repair_runtime_links()

# Fedora wx-config hardcodes /usr even when given --prefix.
wxconfig = prefix / 'usr/lib64/wx/config/gtk3-unicode-3.2'
if wxconfig.exists():
    content = wxconfig.read_text()
    for variable, suffix in [
        ('wxconfdir', 'lib64/wx/config'), ('includedir', 'include'), ('libdir', 'lib64'),
    ]:
        content = re.sub(
            r'^' + variable + r'=.*$',
            variable + f'="{alias}/.circuitalive/deps/usr/{suffix}"',
            content, flags=re.M,
        )
    wxconfig.write_text(content)
    binary = prefix / 'usr/bin/wx-config'
    if not binary.is_symlink() and not binary.exists():
        binary.symlink_to('../lib64/wx/config/gtk3-unicode-3.2')

for pc in prefix.rglob('*.pc'):
    content = pc.read_text()
    # Make repeated invocations safe for files from previously extracted RPMs.
    content = content.replace(f'{alias}/.circuitalive/deps/usr', '/usr')
    pc.write_text(content.replace('/usr', f'{alias}/.circuitalive/deps/usr'))

manifest = root / '.circuitalive/dependencies.sha256'
manifest.write_text(''.join(
    f'{hashlib.sha256(p.read_bytes()).hexdigest()}  rpms/{p.name}\n'
    for p in selected
))
print(f'Dependências locais: {prefix}')
print(f'Pacotes e checksums: {manifest}')

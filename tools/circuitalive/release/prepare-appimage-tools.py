#!/usr/bin/env python3
"""Download the AppImage tools pinned by the release profile and verify their SHA-256."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import urllib.request


def prepare(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    lock = json.loads(Path(__file__).with_name('linux-toolchain.json').read_text())
    for name, entry in lock['appimage'].items():
        target = directory / name
        if target.is_file():
            with target.open('rb') as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() == entry['sha256']:
                    target.chmod(0o755)
                    continue
        partial = target.with_suffix(target.suffix + '.part')
        try:
            with urllib.request.urlopen(entry['url'], timeout=120) as response, partial.open('wb') as stream:
                shutil.copyfileobj(response, stream)
            with partial.open('rb') as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != entry['sha256']:
                    raise RuntimeError(f'AppImage tool checksum mismatch: {name}')
            partial.chmod(0o755)
            partial.replace(target)
        finally:
            partial.unlink(missing_ok=True)
    return directory


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    print(prepare(parser.parse_args().directory))

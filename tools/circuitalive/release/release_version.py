"""KiCad base version followed by the independent CircuitAlive version."""
from dataclasses import dataclass
import re


VERSION_PATTERN = r'([0-9]+\.[0-9]+\.[0-9]+)-(alpha\.)?([0-9]+(?:\.[0-9]+){0,3})'
VERSION_HELP = ('Expected KiCad-CircuitAlive version, e.g. 10.0.7-alpha.4.1 or '
                '10.0.7-1.2.3; CircuitAlive accepts 1–4 numbers from 0 to 65535')


@dataclass(frozen=True)
class ReleaseVersion:
    base: str
    circuitalive: str
    alpha: bool

    @property
    def windows(self):
        # Windows has four numeric WORDs. The full two-part version is retained
        # separately in the resource strings, installer UI and About dialog.
        parts = self.circuitalive.split('.')
        return '.'.join(str(int(p)) for p in parts + ['0'] * (4 - len(parts)))

    @property
    def debian(self):
        return self.base + ('~alpha.' if self.alpha else '+') + self.circuitalive + '-1'

    @property
    def rpm_release(self):
        return ('0.alpha.' if self.alpha else '1.') + self.circuitalive + '.fc44'


def parse_version(version):
    match = re.fullmatch(VERSION_PATTERN, version)
    if not match or any(int(p) > 65535 for p in match[3].split('.')):
        raise ValueError(VERSION_HELP)
    return ReleaseVersion(match[1], match[3], bool(match[2]))

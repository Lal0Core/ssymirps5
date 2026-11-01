#!/usr/bin/env python3
"""List the names a PS5 shared core imports from PS5_RetroArch's title.

The title binds each import by name from a table generated from the cores it
shipped with; a name missing from that table makes the console refuse the core.
This core is meant to import only the C runtime, never libc++ (C++ names).

With --title EBOOT, also check every import against the strings in a title's
eboot.bin (the generated table compares names with string literals).
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path


def imports(core):
    out = subprocess.check_output(['readelf', '--dyn-syms', '-W', str(core)], text=True)
    names = {}
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 8 and f[6] == 'UND' and f[7] != '':
            names[re.sub(r'@.*', '', f[7])] = f[4] == 'WEAK'
    return names


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('core', type=Path)
    ap.add_argument('--title', type=Path, help='eboot.bin of the installed PS5_RetroArch title')
    ap.add_argument('--list', action='store_true', help='print every import')
    args = ap.parse_args()
    names = imports(args.core)
    cxx = sorted(n for n in names if n.startswith('_Z'))
    strong = sorted(n for n, weak in names.items() if not weak)
    print(f'imports: {len(names)} ({len(strong)} strong, {len(names) - len(strong)} weak), C++ names: {len(cxx)}')
    if args.list:
        for n in sorted(names):
            print(('  weak ' if names[n] else '       ') + n)
    if cxx:
        print('error: C++ imports (the title may not bind them):', ', '.join(cxx[:20]), file=sys.stderr)
        return 1
    if args.title:
        blob = args.title.read_bytes()
        missing = [n for n in strong if b'\0' + n.encode() + b'\0' not in blob]
        if missing:
            print(f'error: {len(missing)} imports not found in {args.title.name}: {", ".join(missing)}',
                  file=sys.stderr)
            return 1
        print(f'all {len(strong)} strong imports are present in {args.title.name}')
    return 0


if __name__ == '__main__':
    sys.exit(main())

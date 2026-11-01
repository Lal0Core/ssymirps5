#!/usr/bin/env python3
"""The release's source zip: everything needed to build the app, each folder under its own name in the zip.

  make_source_zip.py <out.zip> <folder>=<name in the zip> [<folder>=<name> ...]

The GPL-3.0 asks for the complete source of what is handed out: the app (ssymirps5/), Ymir's core with its PS5 patch
(ymir-ps5/), PS5_Vulkan (RADV's PS5 port and its build) and the build scripts. Build output, downloaded dependencies
(.deps), caches and the test references of PS5_Vulkan (golden/, probes/) are left out.
"""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZipInfo
import os
import stat
import sys

EXCLUDED_DIRS = {'build', 'build-core', '.git', '.deps', 'empty-libs', 'removed', '__pycache__', '.pytest_cache',
                 'golden', 'probes'}
EXCLUDED_SUFFIXES = {'.o', '.pyc', '.part'}
# the instructions for the AI assistants used while writing the app: not part of its source
EXCLUDED_FILES = {'AGENTS.md', 'CLAUDE.md'}

out = Path(sys.argv[1]).resolve()
pairs = []
for arg in sys.argv[2:]:
    src, _, name = arg.partition('=')
    assert name and '/' not in name and '\\' not in name and name not in {'.', '..'}, arg
    pairs.append((Path(src).resolve(), name))
assert pairs, __doc__

out.parent.mkdir(parents=True, exist_ok=True)
tmp = out.with_suffix(out.suffix + '.part')
count = 0
try:
    with ZipFile(tmp, 'w', ZIP_DEFLATED, compresslevel=6) as z:
        for root, prefix in pairs:
            if root.is_file():
                z.write(root, prefix)
                count += 1
                continue
            for base, dirs, names in os.walk(root, followlinks=False):
                dirs[:] = sorted(d for d in dirs if d not in EXCLUDED_DIRS)
                for name in sorted(names):
                    p = Path(base) / name
                    if p.suffix in EXCLUDED_SUFFIXES or name in EXCLUDED_FILES or p.resolve() in {out, tmp}:
                        continue
                    target = prefix + '/' + p.relative_to(root).as_posix()
                    if p.is_symlink():
                        info = ZipInfo(target)
                        info.create_system = 3
                        info.external_attr = (stat.S_IFLNK | 0o777) << 16
                        z.writestr(info, os.readlink(p))
                    else:
                        z.write(p, target)
                    count += 1
    tmp.replace(out)
except BaseException:
    if tmp.exists():
        tmp.unlink()
    raise
print('%s: %d files, %d bytes' % (out, count, out.stat().st_size))

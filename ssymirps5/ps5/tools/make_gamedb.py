#!/usr/bin/env python3
"""Builds frontend/data/nointro.tsv, the game-name table built into the app, from libretro-database's
Sega Saturn Redump DAT (metadat/redump; names only: a disc is matched by its file name).

  tools/make_gamedb.py <libretro-database checkout> frontend/data/nointro.tsv

Each line is "system<TAB>CRC32<TAB>No-Intro name" ("-" for the CRC of a disc game, matched by name only).
NES entries come twice in the DAT, with the iNES header (.nes) and without (.unh); both CRCs are kept.
"""
import re
import sys

SYSTEMS = [
    ("ss", "redump/Sega - Saturn.dat"),
]

GAME = re.compile(r'^game \(\s*$')
NAME = re.compile(r'^\s*name "(.*)"\s*$')
ROM = re.compile(r'rom \( name "(.*?)" size (\d+) crc ([0-9A-Fa-f]{8})')


def parse(path):
    version = ""
    games = []  # (name, [crc...])
    name = None
    crcs = []
    in_game = False
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r'^\s*version "(.*)"', line)
            if m and not version:
                version = m.group(1)
            if GAME.match(line):
                name, crcs, in_game = None, [], True
                continue
            m = NAME.match(line)
            if m and name is None and in_game:
                name = m.group(1)
                continue
            m = ROM.search(line)
            if m and name is not None:
                crcs.append(m.group(3).upper())
                continue
            if line.strip() == ")" and name is not None:
                games.append((name, crcs))
                name, in_game = None, False
    return version, games


def main():
    root, out = sys.argv[1], sys.argv[2]
    lines = []
    header = ["# SSYmirPS5: Sega Saturn game names, from libretro-database (Redump; matched by name)"]
    for sysid, rel in SYSTEMS:
        version, games = parse(f"{root}/metadat/{rel}")
        seen = set()
        n = 0
        for name, crcs in games:
            if sysid == "ss":
                key = ("-", name)
                if key not in seen:
                    seen.add(key)
                    lines.append(f"{sysid}\t-\t{name}")
                    n += 1
                continue
            for crc in crcs[:1] if sysid != "nes" else crcs:
                if (crc, name) in seen:
                    continue
                seen.add((crc, name))
                lines.append(f"{sysid}\t{crc}\t{name}")
                n += 1
        header.append(f"#   {sysid}: {rel.split('/', 1)[1]} ({version}), {n} entries")
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(header + lines) + "\n")
    print(f"{out}: {len(lines)} entries")


if __name__ == "__main__":
    main()

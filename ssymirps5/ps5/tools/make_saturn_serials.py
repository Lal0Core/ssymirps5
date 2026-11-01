#!/usr/bin/env python3
"""SSYmirPS5: frontend/data/saturn-serials.tsv, the product number of every known Sega Saturn disc and its game.

  make_saturn_serials.py <GameDB-Saturn checkout> <out.tsv>

From niemasd/GameDB-Saturn (GPL-3.0; its sources: Redump, GameFAQs, Sega Retro): one line per disc header product
number (the folder names, as the discs write them at $20), "serial<TAB>title<TAB>region", the title being the
release name when there is one ("Virtua Fighter 2 (Japan) (Rev B)"), else the title. The case view (fe_caseview.cpp)
names a disc's game by it, so its back cover is looked up by the right game, whatever the file is called.
"""
import os
import sys

src, out = sys.argv[1], sys.argv[2]
games = os.path.join(src, "games")
rows = []
for serial in sorted(os.listdir(games)):
    d = os.path.join(games, serial)
    if not os.path.isdir(d):
        continue

    def read(name):
        p = os.path.join(d, name)
        if not os.path.isfile(p):
            return ""
        return open(p, encoding="utf-8", errors="replace").read().strip().splitlines()[0].strip() if open(p, encoding="utf-8", errors="replace").read().strip() else ""

    title = read("release_name.txt") or read("title.txt")
    region = read("region.txt")
    if not title:
        continue
    clean = lambda s: s.replace("\t", " ").replace("\r", " ").replace("\n", " ")
    rows.append("%s\t%s\t%s" % (clean(serial), clean(title), clean(region)))
with open(out, "w", encoding="utf-8") as f:
    f.write("# SSYmirPS5: Sega Saturn product numbers -> game (tools/make_saturn_serials.py, from niemasd/GameDB-Saturn, GPL-3.0)\n")
    f.write("# serial<TAB>title<TAB>region\n")
    f.write("\n".join(rows) + "\n")
print("%d serials" % len(rows))

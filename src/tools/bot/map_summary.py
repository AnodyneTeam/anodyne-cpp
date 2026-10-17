#!/usr/bin/env python3
"""usage: map_summary.py MAP [type-regex]   List the entity presets of a map by 160x160 screen (from Content/Entities.xml)."""
import re, sys, collections
import xml.etree.ElementTree as ET
X = '/home/snake/dev/AnodyneSharp/AnodyneSharp/AnodyneSharp.Shared/Content/Entities.xml'
m = sys.argv[1].upper()
flt = re.compile(sys.argv[2]) if len(sys.argv) > 2 else None
root = ET.parse(X).getroot()
for mp in root.findall('map'):
    if mp.get('name') != m:
        continue
    scr = collections.defaultdict(list)
    for e in mp:
        x, y = int(float(e.get('x', 0))), int(float(e.get('y', 0)))
        if flt and not flt.search(e.tag):
            continue
        extra = ' '.join(f'{k}={v}' for k, v in e.attrib.items() if k not in ('x', 'y', 'guid', 'alive', 'p'))
        scr[(x // 160, y // 160)].append(f'{e.tag}@{x},{y} {extra}'.strip())
    for k in sorted(scr, key=lambda k: (k[1], k[0])):
        print(f'screen {k[0]},{k[1]}:')
        for l in scr[k]:
            print('   ', l)

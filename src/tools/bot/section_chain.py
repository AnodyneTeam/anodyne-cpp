#!/usr/bin/env python3
"""Prints the bot tokens of the Temple sections from index N onward (so a run can start from section N's saved snapshot)
usage: section_chain.py <N> [Temple.cs]"""
import re, sys
n = int(sys.argv[1])
src = open(sys.argv[2] if len(sys.argv) > 2 else '/home/snake/dev/AnodyneSharp/AnodyneSharp/AnodyneSharp.Shared/Recording/Temple.cs').read()
consts = dict(re.findall(r'const string (\w+) = "([^"]*)";', src))
bodies = []
for m in re.finditer(r'Section\("([^"]+)",\s*\(\)\s*=>\s*Exec\(([^;]*?)\)\);', src):
    expr = m.group(2)
    toks = ''
    for part in re.split(r'\s*\+\s*', expr):
        part = part.strip()
        if part.startswith('"'):
            toks += part.strip('"')
        elif part in consts:
            toks += consts[part]
    bodies.append((m.group(1), toks))
import os
snap = os.environ.get('SNAPS') == '1'
for i, (name, toks) in enumerate(bodies):
    if i >= n:
        if snap:
            print('`s%02d_%s' % (i, name.replace(' ', '_')), end=' ')
        print(toks, end=' ')
print()

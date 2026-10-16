#!/usr/bin/env python3
"""Print a class's exported decompile (work/*/<Class>.txt) without comments, thunks, destructors or local
declarations. usage: cls.py Class"""
import glob, re, sys
name = sys.argv[1]
f = glob.glob(f'work/*/{name}.txt')[0]
text = open(f).read()
parts = re.split(r'^//==== (0x[0-9a-f]+)  ', text, flags=re.M)
for i in range(1, len(parts), 2):
    addr, body = parts[i], parts[i + 1]
    lines = body.split('\n')
    title = lines[0]
    if 'thunk' in title or 'covariant' in title or '~' in title:
        continue
    code = [l for l in lines[1:] if not l.startswith('//') and 'WARNING' not in l]
    out = []
    for l in code:
        out.append(l)
        if l == '}':
            break
    s = '\n'.join(out).strip()
    s = re.sub(r'\n  (?:undefined\d*|uint|int|long|ulong|char|byte|bool|float|double|code|void|cRZString|string|undefined)\s*\**\s*[A-Za-z_0-9, \*\[\]]+;', '', s)
    print(f'// {addr} {title}\n{s}')

#!/usr/bin/env python3
"""Print an exported class dump without thunks / comments: scripts/nothunk.py <dir> <Class> [from] [to]"""
import re, sys
t = open(f"work/{sys.argv[1]}/{sys.argv[2]}.txt").read()
out = []
for p in re.split(r'(?m)^//==== ', t)[1:]:
    head = p.split('\n', 1)[0]
    if 'non-virtual thunk' in head: continue
    body = ''.join(l[:150] + '\n' for l in p.split('\n')[1:] if not l.startswith('//') and l.strip())
    out.append('//== ' + head[:110] + '\n' + body)
lines = ''.join(out).split('\n')
a = int(sys.argv[3]) if len(sys.argv) > 3 else 0
b = int(sys.argv[4]) if len(sys.argv) > 4 else len(lines)
print('\n'.join(lines[a:b])); print(f'[{len(lines)} lines]', file=sys.stderr)

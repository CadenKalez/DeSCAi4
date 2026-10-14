#!/usr/bin/env python3
"""usage: show_oracle.py dst src flag(0/1) method   -- print the decompiled original of one instantiation"""
import json, re, sys, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
m = json.load(open(ROOT/'work/blit/manifest.json'))
src = (ROOT/'work/blit/oracle.c').read_text()
d, s, f, meth = int(sys.argv[1]), int(sys.argv[2]), sys.argv[3] == '1', sys.argv[4]
for e in m:
    if e['dst'] == d and e['src'] == s and e['flag'] == f and e['method'] == meth:
        i = src.index('oracle_' + e['addr'] + '(')
        j = src.index('\n#undef', i)
        body = re.sub(r'\n\s*\n+', '\n', src[i:j])
        body = body.replace('(*(unsigned int *)((unsigned char *)&in_stack_00000058 + 4))', 'COLOR')
        body = re.sub(r'\(\*\(unsigned char \*\)\(\(unsigned char \*\)&in_stack_00000058 \+ 7\)\)', 'COLOR_A', body)
        body = re.sub(r'in_stack_00000008', 'DSTP', body); body = re.sub(r'in_stack_00000010', 'DSTPITCH', body)
        body = re.sub(r'in_stack_00000028', 'SRCP', body); body = re.sub(r'in_stack_00000030', 'SRCPITCH', body)
        body = re.sub(r'in_stack_00000060', 'KEY', body)
        print('//', e['addr'], d, s, f, meth); print(body)

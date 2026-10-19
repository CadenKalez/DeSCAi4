#!/usr/bin/env python3
"""Collapse libc++ std::string boilerplate in Ghidra decompile text so the real logic is readable.
usage: clean_dec.py in.txt > out.txt"""
import re, sys
t = open(sys.argv[1], errors="replace").read()
# string built from a C string: strlen ... memcpy ... NUL
t = re.sub(r"[ \t]*sVar\d+ = _strlen\((\w+)\);.*?\*\(undefined1 \*\)\(\(long\)\w+ \+ sVar\d+\) = 0;\n",
           lambda m: f"  /* SSO string := {m.group(1)} */\n", t, flags=re.S)
# zero-initialisation of the pieces of a stack string
t = re.sub(r"^[ \t]*(?:uStack|cStack|local|auStack)_\w+(?:\[\d+\])? = (?:\(\w+\))?(?:0|'\\0');\n", "", t, flags=re.M)
# std::string destructor idiom
t = re.sub(r"^[ \t]*if \(\(?\(?(?:byte\))?\(?\w+(?:\._\d_\d)?\)? & 1\)? != 0\) \{\n[ \t]*(?:::)?operator_delete\([^\n]*\);\n[ \t]*\}\n", "", t, flags=re.M)
t = re.sub(r"^[ \t]*if \(\(?\w+ & 1\)? != 0\) \{\n[ \t]*::operator_delete\([^\n]*\);\n[ \t]*\}\n", "", t, flags=re.M)
t = re.sub(r"^[ \t]*___stack_chk_fail\(\);\n", "", t, flags=re.M)
t = re.sub(r"\n{3,}", "\n\n", t)
sys.stdout.write(t)

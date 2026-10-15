#!/usr/bin/env python3
"""Read bytes from the (x86_64) game binary by virtual address: macho_read.py <hexaddr> <length> [u32|u16|u8|hex]"""
import struct, sys, pathlib
BIN = pathlib.Path.home()/"Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents/MacOS/Sim City 4 Deluxe Editionsub"

def segments(data):
    magic, = struct.unpack_from("<I", data, 0)
    assert magic == 0xfeedfacf, hex(magic)
    ncmds, = struct.unpack_from("<I", data, 16)
    off = 32
    segs = []
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd == 0x19:
            name = data[off+8:off+24].rstrip(b"\0").decode()
            vmaddr, vmsize, fileoff, filesize = struct.unpack_from("<QQQQ", data, off+24)
            segs.append((name, vmaddr, vmsize, fileoff, filesize))
        off += size
    return segs

def read(addr, length, data=None, segs=None):
    data = data or BIN.read_bytes()
    segs = segs or segments(data)
    for name, vm, vmsize, fo, fs in segs:
        if vm <= addr < vm + vmsize:
            o = fo + (addr - vm)
            return data[o:o+length]
    raise SystemExit(f"address {addr:#x} not mapped")

if __name__ == "__main__":
    addr = int(sys.argv[1], 16); n = int(sys.argv[2]); fmt = sys.argv[3] if len(sys.argv) > 3 else "hex"
    b = read(addr, n)
    if fmt == "u32": print([hex(x) for x in struct.unpack(f"<{len(b)//4}I", b)])
    elif fmt == "u16": print([hex(x) for x in struct.unpack(f"<{len(b)//2}H", b)])
    else: print(b.hex())

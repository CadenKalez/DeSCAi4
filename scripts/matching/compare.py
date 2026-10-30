"""Matching-decomp comparison: the shipped Mach-O x86_64 game vs our x86_64 object files.

Each function is disassembled with capstone into a list of normalized instruction strings:
  * a call / jump / rip-relative operand that resolves to a symbol on its side becomes that symbol's normalized name
    (functions, globals, vtables, imported stubs / GOT entries); a string literal becomes its contents; a literal-pool
    constant becomes its bytes;
  * a branch inside the function becomes its offset from the function start;
  * everything else (opcode, registers, order, stack offsets, immediates) is kept verbatim.
score() = difflib.SequenceMatcher ratio over the two lists; 1.0 = identical after relocation.
Names are demangled and the port-only namespaces (sc4::, gz::, native:: ...) are stripped, so ours meet the original's.
"""
import bisect, difflib, json, re, struct, subprocess
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_IMM, CS_OP_MEM
from capstone.x86 import X86_REG_RIP

PORT_NS = ("sc4", "native", "gz", "gzcom", "sgl", "s3d", "gd", "exphost", "lua5", "SC4Audio", "sc4audio", "render", "html")
_ns_re = re.compile(r"(?<![A-Za-z0-9_:])(?:%s)::" % "|".join(PORT_NS))


def norm_name(demangled):
    s = _ns_re.sub("", demangled)
    s = s.replace("(anonymous namespace)::", "")
    return re.sub(r"\s+", " ", s)


def demangle_all(names):
    if not names:
        return {}
    p = subprocess.run(["c++filt"], input="\n".join(names), capture_output=True, text=True)
    out = p.stdout.split("\n")
    return {n: (out[i] if i < len(out) else n) for i, n in enumerate(names)}


def demangle_one(name):
    d = demangle_all([name]).get(name, name)
    if d == name and name.startswith("_"):
        d = demangle_all([name[1:]]).get(name[1:], name[1:])
    return norm_name(d)


def parse_macho(data):
    magic, cputype, cpusub, filetype, ncmds, sizeofcmds, flags, _ = struct.unpack_from("<IiiIIIII", data, 0)
    assert magic == 0xfeedfacf, hex(magic)
    off = 32
    sections, symtab, dysymtab = [], None, None
    for _ in range(ncmds):
        cmd, size = struct.unpack_from("<II", data, off)
        if cmd == 0x19:   # LC_SEGMENT_64
            nsects = struct.unpack_from("<I", data, off + 64)[0]
            so = off + 72
            for _i in range(nsects):
                sect = data[so:so + 80]
                addr, ssize, soff, align, reloff, nreloc, sflags, r1, r2, r3 = struct.unpack_from("<QQIIIIIIII", sect, 32)
                sections.append(dict(sect=sect[0:16].rstrip(b"\0").decode(), seg=sect[16:32].rstrip(b"\0").decode(), addr=addr,
                                     size=ssize, offset=soff, reloff=reloff, nreloc=nreloc, flags=sflags, reserved1=r1,
                                     reserved2=r2, index=len(sections) + 1))
                so += 80
        elif cmd == 0x2:
            symtab = struct.unpack_from("<IIII", data, off + 8)
        elif cmd == 0xb:
            dysymtab = struct.unpack_from("<18I", data, off + 8)
        off += size
    syms = []
    if symtab:
        symoff, nsyms, stroff, strsize = symtab
        for i in range(nsyms):
            n_strx, n_type, n_sect, n_desc, n_value = struct.unpack_from("<IBBhQ", data, symoff + i * 16)
            end = data.index(b"\0", stroff + n_strx)
            syms.append(dict(name=data[stroff + n_strx:end].decode("utf-8", "replace"), type=n_type, sect=n_sect, value=n_value))
    return sections, syms, dysymtab


class Image:
    """Address -> meaning for one Mach-O image."""

    def __init__(self, data, sections, syms):
        self.data, self.sections = data, sections
        self.by_index = {s["index"]: s for s in sections}
        named = [s for s in syms if (s["type"] & 0x0e) == 0x0e and not (s["type"] & 0xe0) and s["sect"] > 0]
        dem = demangle_all([s["name"] for s in named])
        self.addr_syms = sorted(((s["value"], s["name"], s["sect"]) for s in named), key=lambda t: t[0])
        self.addr_keys = [a for a, _, _ in self.addr_syms]
        self.dem = {}
        for s in named:
            d = dem.get(s["name"], s["name"])
            if d == s["name"] and s["name"].startswith("_"):
                d = demangle_all([s["name"][1:]]).get(s["name"][1:], s["name"][1:])
            self.dem[s["name"]] = norm_name(d)

    def section_of(self, addr):
        for s in self.sections:
            if s["addr"] <= addr < s["addr"] + s["size"]:
                return s
        return None

    def file_bytes(self, sect, addr, n):
        o = sect["offset"] + (addr - sect["addr"])
        return self.data[o:o + n]

    def describe(self, addr):
        s = self.section_of(addr)
        if s is None:
            return f"abs:{addr:#x}"
        sn, stype = s["sect"], s["flags"] & 0xff
        if sn == "__cstring" or stype == 0x2:
            try:
                o = s["offset"] + addr - s["addr"]
                return "str:" + json.dumps(self.data[o:self.data.index(b"\0", o)].decode("utf-8", "replace"))
            except ValueError:
                return "str:?"
        if sn in ("__literal4", "__literal8", "__literal16") or stype in (0x3, 0x4, 0xe):
            n = {"__literal4": 4, "__literal8": 8, "__literal16": 16}.get(sn, {0x3: 4, 0x4: 8, 0xe: 16}.get(stype, 8))
            return "lit:" + self.file_bytes(s, addr, n).hex()
        i = bisect.bisect_right(self.addr_keys, addr) - 1
        if i >= 0:
            a, name, sect = self.addr_syms[i]
            if self.by_index.get(sect) is s:
                d = self.dem.get(name, name)
                return d if a == addr else f"{d}+{addr - a}"
        return f"{sn}+{addr - s['addr']}"


_cs = Cs(CS_ARCH_X86, CS_MODE_64)
_cs.detail = True


def trim_padding(insns):
    while insns and (insns[-1].mnemonic in ("nop", "int3") or insns[-1].mnemonic.startswith("nop")):
        insns.pop()
    return insns


def normalize(insns, start, end, resolve):
    out = []
    for ins in insns:
        text, mn = ins.op_str, ins.mnemonic
        if ins.group(1) or ins.group(2) or mn.startswith("j") or mn == "call":
            target = None
            for op in ins.operands:
                if op.type == CS_OP_IMM:
                    target = op.imm
            if target is not None:
                text = f"L{target - start}" if start <= target < end else (resolve(ins, target) or f"?{target:#x}")
        for op in ins.operands:
            if op.type == CS_OP_MEM and op.mem.base == X86_REG_RIP:
                sym = resolve(ins, ins.address + ins.size + op.mem.disp)
                text = re.sub(r"\[rip [+-] 0x[0-9a-f]+\]", f"[{sym or '?'}]", text)
        out.append(f"{mn} {text}".strip())
    return out


class Original:
    """The shipped executable: functions by start address (LC_FUNCTION_STARTS), symbols, imports."""

    def __init__(self, path):
        self.data = open(path, "rb").read()
        sections, syms, dysym = parse_macho(self.data)
        self.img = Image(self.data, sections, syms)
        self.text = next(s for s in sections if s["sect"] == "__text")
        self.imports = {}
        if dysym:
            indirectsymoff, nindirect = dysym[12], dysym[13]
            ind = [struct.unpack_from("<I", self.data, indirectsymoff + 4 * i)[0] for i in range(nindirect)]
            for s in sections:
                stype = s["flags"] & 0xff
                if stype in (0x6, 0x7, 0x8):
                    stride = s["reserved2"] if stype == 0x8 else 8
                    for i in range(s["size"] // stride):
                        k = ind[s["reserved1"] + i]
                        if k & 0xc0000000:
                            continue
                        self.imports[s["addr"] + i * stride] = ("got:" if stype != 0x8 else "") + demangle_one(syms[k]["name"])
        starts = subprocess.run(["objdump", "--macho", "--function-starts", path], capture_output=True, text=True).stdout.split()
        self.starts = sorted(int(x, 16) for x in starts if re.fullmatch(r"[0-9a-fA-F]{9,16}", x))
        text_end = self.text["addr"] + self.text["size"]
        self.size_of = {a: (self.starts[i + 1] if i + 1 < len(self.starts) else text_end) - a for i, a in enumerate(self.starts)}
        self.by_name = {}
        for a, name, sect in self.img.addr_syms:
            if a in self.size_of:
                self.by_name.setdefault(self.img.dem.get(name, name), a)

    def resolve(self, ins, target):
        return self.imports.get(target) or self.img.describe(target)

    def function(self, start):
        size = self.size_of[start]
        insns = trim_padding(list(_cs.disasm(self.img.file_bytes(self.text, start, size), start)))
        return normalize(insns, start, start + size, self.resolve), sum(i.size for i in insns)


class ObjectFile:
    def __init__(self, path):
        self.data = open(path, "rb").read()
        self.sections, self.syms, _ = parse_macho(self.data)
        self.img = Image(self.data, self.sections, self.syms)
        self.text = next((s for s in self.sections if s["sect"] == "__text" and s["seg"] == "__TEXT"), None)
        self.relocs = {}
        if self.text:
            for i in range(self.text["nreloc"]):
                r_address, info = struct.unpack_from("<iI", self.data, self.text["reloff"] + 8 * i)
                self.relocs.setdefault(r_address, []).append(((info >> 27) & 1, info & 0xffffff, (info >> 28) & 0xf))

    def functions(self):
        if not self.text:
            return []
        idx = self.text["index"]
        defs = sorted({(s["value"], s["name"]) for s in self.syms if (s["type"] & 0x0e) == 0x0e and s["sect"] == idx
                       and not (s["type"] & 0xe0) and not s["name"].startswith(("ltmp", "l_", "L"))})
        out = []
        for i, (a, name) in enumerate(defs):
            end = defs[i + 1][0] if i + 1 < len(defs) else self.text["addr"] + self.text["size"]
            out.append((self.img.dem.get(name, name), a, end - a))
        return out

    def resolve(self, ins, target):
        off0 = ins.address - self.text["addr"]
        for o in range(off0, off0 + ins.size):
            for extern, symnum, rtype in self.relocs.get(o, ()):
                if extern:
                    name = self.syms[symnum]["name"]
                    nm = self.img.dem.get(name) or demangle_one(name)
                    return ("got:" + nm) if rtype in (3, 4) else nm
                return self.img.describe(target)
        return self.img.describe(target) if target is not None else None

    def function(self, start, size):
        insns = trim_padding(list(_cs.disasm(self.img.file_bytes(self.text, start, size), start)))
        return normalize(insns, start, start + size, self.resolve), sum(i.size for i in insns)


def score(a, b):
    return 1.0 if a == b else difflib.SequenceMatcher(None, a, b, autojunk=False).ratio()

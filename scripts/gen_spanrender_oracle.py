#!/usr/bin/env python3
"""Decompiled cSpanRender* methods -> work/blit/span_oracle.c + span_harness.cpp (compares SpanRender.h)."""
import re, json, pathlib, subprocess
ROOT = pathlib.Path(__file__).resolve().parent.parent
txt = subprocess.run(["python3", str(ROOT/"scripts/dec_addrs.py"), "--grep",
                      r"nGZDrawSoftRender::cSpanRender(8|565|1555|4444|8888)::Render(Solid|Gradient|Alpha|AlphaGradient)\("],
                     capture_output=True, text=True, check=True).stdout
blocks = re.split(r"(?m)^//==== ", txt)[1:]
out = ["#include <stdint.h>\n#include <string.h>\ntypedef unsigned char undefined1,byte,uchar;typedef unsigned short undefined2,ushort;"
       "typedef unsigned int undefined4,uint;typedef unsigned long undefined8,ulong;\n"
       "#define _memset memset\nstatic void memset16(void*p,unsigned short v,unsigned long n){unsigned short*q=p;while(n--)*q++=v;}\n"
       "static void memset32(void*p,unsigned v,unsigned long n){unsigned*q=p;while(n--)*q++=v;}\n"]
cases = []
for b in blocks:
    head, _, code = b.partition("\n")
    m = re.match(r"(0x[0-9a-f]+) nGZDrawSoftRender::cSpanRender(\w+)::(\w+)\(", head)
    addr, fmt, meth = m.groups()
    if fmt == "8" and meth in ("RenderAlpha", "RenderAlphaGradient"):
        continue
    hm = re.search(r"(?m)^\{", code)
    header, body = code[:hm.start()], code[hm.start():]
    header = re.sub(r"(?m)^//.*$", "", header)
    params = re.search(r"\(([^()]*)\)\s*$", header.strip()).group(1)
    params = re.sub(r"\bcGZBufferSurfaceDesc \*", "unsigned char *", params)
    params = re.sub(r"\bcGradientInfo \*", "unsigned char *", params)
    params = re.sub(r"\bcSpanRender\w+ \*", "void *", params)
    body = re.sub(r"(?m)^//.*$", "", body)
    out.append(f"void oracle_{addr[2:]}({params}) {body}")
    cases.append((addr[2:], fmt, meth))
(ROOT/"work/blit/span_oracle.c").write_text("\n".join(out))
h = ['#include <cstdio>', '#include <cstdlib>', '#include <cstring>', '#include <vector>', '#include "gz/SpanRender.h"',
     'using namespace nGZDrawSoftRender;', 'extern "C" {']
for a, f, m in cases:
    sig = {"RenderSolid": "void*, const unsigned*, unsigned, unsigned char*, unsigned",
           "RenderGradient": "void*, const unsigned*, unsigned, unsigned char*, unsigned char*",
           "RenderAlpha": "void*, const unsigned*, unsigned, unsigned char*, unsigned, unsigned",
           "RenderAlphaGradient": "void*, const unsigned*, unsigned, unsigned char*, unsigned char*"}[m]
    h.append(f"void oracle_{a}({sig});")
h.append('}')
h.append(r'''
static unsigned rs = 777;
static unsigned Rnd() { rs = rs * 1664525u + 1013904223u; return rs >> 8; }
template <class R>
static int Run(const char* name, const char* addr, const char* method, int bytes, void (*oracle)(), int kind) { return 0; }
int main() {
    int fail = 0, total = 0;
    const int W = 48, H = 20;
    auto test = [&](const char* fmt, int bytes, cSpanRender& r, const char* addr, const char* m, auto oracleCall) {
        for (int t = 0; t < 200; ++t) {
            std::vector<unsigned char> a(W * H * 4), b;
            for (auto& x : a) x = Rnd();
            b = a;
            std::vector<unsigned> spans;
            for (int i = 0; i < 5; ++i) { unsigned y = Rnd() % H, x0 = Rnd() % 20, x1 = x0 + Rnd() % 24; spans.push_back(y << 16 | x0); spans.push_back(y << 16 | x1); }
            unsigned char g[0x30];
            for (auto& x : g) x = Rnd();
            // moderate gradient magnitudes keep the arithmetic away from pathological wrap-around
            std::int32_t gi[12];
            for (int i = 0; i < 12; ++i) gi[i] = (Rnd() % 0x2000000) - 0x1000000;
            std::memcpy(g, gi, 0x30);
            unsigned colour = Rnd() * 65537u, alpha = Rnd() * 257u;
            cGZBufferSurfaceDesc dA{a.data(), W * bytes, W, H, {}}, dB{b.data(), W * bytes, W, H, {}};
            unsigned char descA[0x20], descB[0x20];
            std::memcpy(descA, &dA, 0x20); std::memcpy(descB, &dB, 0x20);
            oracleCall(descA, spans, g, colour, alpha, &r, dB);
            ++total;
            if (a != b) { ++fail; std::printf("DIFF %s %s %s\n", fmt, m, addr); break; }
        }
    };
    (void)test;
''')
for a, f, m in cases:
    cls = {"8": "cSpanRender8", "565": "cSpanRender565", "1555": "cSpanRender1555", "4444": "cSpanRender4444", "8888": "cSpanRender8888"}[f]
    bytes = {"8": 1, "565": 2, "1555": 2, "4444": 2, "8888": 4}[f]
    if m == "RenderSolid":
        call = f"o(nullptr, spans.data(), 5, descA, colour); r.RenderSolid(spans.data(), 5, dB, colour);"
    elif m == "RenderGradient":
        call = f"o(nullptr, spans.data(), 5, descA, g); r.RenderGradient(spans.data(), 5, dB, *reinterpret_cast<cGradientInfo*>(g));"
    elif m == "RenderAlpha":
        call = f"o(nullptr, spans.data(), 5, descA, colour, alpha); r.RenderAlpha(spans.data(), 5, dB, colour, alpha);"
    else:
        call = f"o(nullptr, spans.data(), 5, descA, g); r.RenderAlphaGradient(spans.data(), 5, dB, *reinterpret_cast<cGradientInfo*>(g));"
    h.append(f'''    {{
        auto o = oracle_{a};
        auto fn = [&](unsigned char* descA, std::vector<unsigned>& spans, unsigned char* g, unsigned colour, unsigned alpha, cSpanRender* rp, cGZBufferSurfaceDesc& dB) {{
            cSpanRender& r = *rp; {call}
        }};
        {cls} inst;
        test("{f}", {bytes}, inst, "{a}", "{m}", fn);
    }}''')
h.append('    std::printf("span renderers: %d cases, %d differ\\n", total, fail);\n    return fail != 0;\n}')
(ROOT/"work/blit/span_harness.cpp").write_text("\n".join(h))
print(len(cases), "cases")

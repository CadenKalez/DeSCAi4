#!/usr/bin/env python3
"""Generate work/blit/harness.cpp: runs every recovered cBlitter instantiation against its decompiled
oracle (work/blit/oracle.c, see gen_blitter_oracle.py) on random surfaces and reports differences."""
import json, pathlib
ROOT = pathlib.Path(__file__).resolve().parent.parent
man = json.load(open(ROOT/"work/blit/manifest.json"))
FORMATS = ["0u,0u,0u,0u,1,0", "0u,16711680u,65280u,255u,4,8", "0u,31744u,992u,31u,2,5", "0u,3840u,240u,15u,2,4",
           "0u,63488u,2016u,31u,2,5", "255u,0u,0u,0u,1,0", "32768u,31744u,992u,31u,2,5",
           "4278190080u,16711680u,65280u,255u,4,8", "61440u,3840u,240u,15u,2,4"]
out = ['#include <cstdio>', '#include <cstdlib>', '#include <cstring>', '#include <vector>', '#include <map>', '#include <string>',
       '#include "gz/SoftBlit.h"', 'using namespace nGZDrawSoftRender;']
for i, f in enumerate(FORMATS):
    out.append(f"using F{i} = RGBFormat<{f}>;")
out.append('extern "C" {')
for e in man:
    out.append(f'unsigned long oracle_{e["addr"]}(unsigned long, unsigned long, unsigned char*);')
out.append('}')
out.append('''
typedef void (*SpanFn)(const std::uint32_t*, std::uint32_t, cGZBufferSurfaceDesc, cGZBufferSurfaceDesc, cBltInfo);
typedef void (*RectFn)(std::uint32_t, std::uint32_t, cGZBufferSurfaceDesc, cGZBufferSurfaceDesc, cBltInfo);
struct Case { const char* addr; int dst, src; bool flag; const char* method; SpanFn span; RectFn rect;
              unsigned long (*oracle)(unsigned long, unsigned long, unsigned char*); };
static const Case kCases[] = {''')
for e in man:
    d, s, fl, m = e["dst"], e["src"], "true" if e["flag"] else "false", e["method"]
    cls = f"cBlitter<F{d}, F{s}, {fl}>"
    impl = None
    fn = f"&{cls}::{m}"
    span = fn if m != "BltRect" else "nullptr"
    rect = fn if m == "BltRect" else "nullptr"
    out.append(f'  {{"{e["addr"]}", {d}, {s}, {fl}, "{m}", {span}, {rect}, &oracle_{e["addr"]}}},')
out.append('};')
out.append(r'''
static unsigned rngState = 12345;
static unsigned Rnd() { rngState = rngState * 1664525u + 1013904223u; return rngState >> 8; }
static const int kBytes[9] = {1, 4, 2, 2, 2, 1, 2, 4, 2};
static const unsigned kColourMask[9] = {0, 0xffffff, 0x7fff, 0xfff, 0xffff, 0, 0x7fff, 0xffffff, 0xfff};
int main(int argc, char** argv) {
    const int W = 40, H = 40, kTrials = 120;
    const bool verbose = argc > 1;
    std::map<std::string, int> bad, total;
    int unimplemented = 0, failures = 0, passes = 0, noEffect = 0;
    for (const Case& c : kCases) {
        const std::string method = c.method;
        if (!c.span && !c.rect) { ++unimplemented; continue; }
        bool failed = false; bool everChanged = false;
        for (int trial = 0; trial < kTrials && !failed; ++trial) {
            const int sb = kBytes[c.src], db = kBytes[c.dst];
            const int sp = W * sb + 8, dp = W * db + 8;
            std::vector<unsigned char> src(sp * H), dst(dp * H);
            for (auto& b : src) b = Rnd();
            for (auto& b : dst) b = Rnd();
            std::vector<unsigned char> dstA = dst, dstB = dst;
            cGZBufferSurfaceDesc sd{src.data(), sp, (unsigned)W, (unsigned)H, {0, 0, 0, 0}};
            cBltInfo info{};
            info.startX = (trial % 3 == 0) ? Rnd() % W : (Rnd() % 8) << 16;
            info.startY = (trial % 3 == 0) ? Rnd() % H : (Rnd() % 8) << 16;
            info.stepX = 0x8000 + Rnd() % 0x8000;
            info.stepY = 0x8000 + Rnd() % 0x8000;
            info.srcFormatId = Rnd();
            info.colour = Rnd() * 257u ^ Rnd();
            info.key = Rnd() & kColourMask[c.src];
            if (trial % 2 == 0) {   // make the key hit
                unsigned v = 0;
                std::memcpy(&v, &src[(Rnd() % H) * sp + (Rnd() % W) * sb], sb);
                info.key = v & kColourMask[c.src];
            }
            if (c.src == 0 || c.src == 5) info.key = Rnd() & 1;      // solid source: flag
            const bool tiled = method.find("Tiled") != std::string::npos, stretch = method.find("Stretch") != std::string::npos;
            if (tiled) { info.startX = Rnd() % W; info.startY = Rnd() % H; }
            if (!tiled && !stretch) { info.startX = info.startY = info.stepX = info.stepY = 0; }
            std::vector<std::uint32_t> spans;
            const int nspans = 6;
            for (int i = 0; i < nspans; ++i) {
                unsigned y = Rnd() % 24, x0 = Rnd() % 20, x1 = x0 + Rnd() % 18;
                spans.push_back(y << 16 | x0);
                spans.push_back(y << 16 | x1);
            }
            auto makeStack = [&](unsigned char* stk, std::vector<unsigned char>& d) {
                std::memset(stk, 0, 0x80);
                cGZBufferSurfaceDesc dd{d.data(), dp, (unsigned)W, (unsigned)H, {0, 0, 0, 0}};
                std::memcpy(stk + 0x08, &dd, 0x20);
                std::memcpy(stk + 0x28, &sd, 0x20);
                std::memcpy(stk + 0x48, &info, sizeof info);
            };
            unsigned char stkA[0x80], stkB[0x80];
            makeStack(stkA, dstA);
            makeStack(stkB, dstB);
            cGZBufferSurfaceDesc dd{dstB.data(), dp, (unsigned)W, (unsigned)H, {0, 0, 0, 0}};
            if (c.rect) {
                const unsigned w = 1 + Rnd() % 30, h = 1 + Rnd() % 20;
                c.oracle(w, h, stkA);
                c.rect(w, h, dd, sd, info);
            } else {
                c.oracle((unsigned long)spans.data(), nspans, stkA);
                c.span(spans.data(), nspans, dd, sd, info);
            }
            if (dstA != dstB) failed = true;
            if (dstA != dst) everChanged = true;
        }
        std::string key = method + " dst" + std::to_string(c.dst) + " src" + std::to_string(c.src) + (c.flag ? " key" : "");
        if (!everChanged) { ++noEffect; if (verbose) std::printf("NOEFFECT %s\n", c.addr); }
        ++total[method];
        if (failed) { ++failures; ++bad[method]; if (verbose) std::printf("DIFF %s %s\n", c.addr, key.c_str()); }
        else ++passes;
    }
    std::printf("pass %d  fail %d  unimplemented %d  never-modified %d\n", passes, failures, unimplemented, noEffect);
    for (auto& kv : bad) std::printf("  %-26s %d / %d differ\n", kv.first.c_str(), kv.second, total[kv.first]);
    return failures != 0;
}
''')
(ROOT/"work/blit/harness.cpp").write_text("\n".join(out))
print("ok", len(man))

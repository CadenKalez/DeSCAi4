// Compares nSGLDX7::cTextureStageData with the Ghidra decompilation (work/texstage/oracle.c): both are run
// against a recording device and must emit identical texture-stage call sequences.
#include "render/TextureStageData.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
extern "C" {
void oracle_10060444c(void* self, int stage, void* dev, void* texMgr);
void oracle_10060898c(void* self);
unsigned long oracle_1006089f2(void* self, int stage, void* dev, void* texMgr);
void oracle_100608c50(void* self, int stage, void* dev);
void oracle_100608eb4(void* self, int stage, void* dev);
void oracle_10060902c(void* self, int stage, void* dev);
}
struct Call { int kind, a, b, c; bool operator==(const Call&) const = default; };
static std::vector<Call> g_log;
static uint32_t g_usable = 1;
extern "C" {
static void MockSetTexture(void*, int stage) { g_log.push_back({0, stage, 0, 0}); }
static void MockSetState(void*, int stage, int state, int value) { g_log.push_back({1, stage, state, value}); }
// stand-in for cTextureManager::TextureHandle: returns a record whose +0x30 word is non-null for usable ids
long mock_TextureHandle(void*, unsigned id) {
    static unsigned char blobs[2][0x40];
    if (id == 0xffffffffu || !(g_usable & (1u << (id & 31)))) return 0;
    long surface = 1;
    std::memcpy(&blobs[0][0x30], &surface, 8);
    return long(&blobs[0][0]);
}
}
struct Host : nSGLDX7::ITextureStageHost {
    bool IsTextureUsable(uint32_t id) override { return id != 0xffffffffu && (g_usable & (1u << (id & 31))); }
    void BindTexture(int stage, uint32_t) override { g_log.push_back({0, stage, 0, 0}); }
    void SetTextureStageState(int stage, int state, int value) override { g_log.push_back({1, stage, state, value}); }
};
static uint32_t R32() { return uint32_t(rand()) * 2654435761u ^ uint32_t(rand()); }

int main() {
    void* vtbl[0x200 / 8] = {};
    vtbl[0x118 / 8] = (void*)MockSetTexture;
    vtbl[0x128 / 8] = (void*)MockSetState;
    struct { void** v; } dev{vtbl};
    int bad = 0;
    // constructor defaults
    {
        alignas(8) unsigned char raw[0x80] = {};
        oracle_10060898c(raw);
        nSGLDX7::cTextureStageData d;
        auto u32 = [&](size_t o) { uint32_t v; std::memcpy(&v, raw + o, 4); return v; };
        const bool ok = raw[0] == d.dirty && raw[1] == d.textureEnabled && raw[2] == d.textureHasAlpha && u32(4) == d.textureId &&
                        u32(0xc) == d.magFilter && u32(0x10) == d.minFilter && u32(0x14) == d.wrapS && u32(0x18) == d.wrapT &&
                        u32(0x1c) == d.envMode && u32(0x20) == d.appliedEnvMode && u32(0x24) == d.rgbCombine && u32(0x28) == d.alphaCombine &&
                        u32(0x2c) == d.rgbSource[0] && u32(0x30) == d.rgbSource[1] && u32(0x34) == d.rgbSource[2] &&
                        u32(0x38) == d.alphaSource[0] && u32(0x3c) == d.alphaSource[1] && u32(0x40) == d.alphaSource[2] &&
                        u32(0x44) == d.rgbOperand[0] && u32(0x48) == d.rgbOperand[1] && u32(0x4c) == d.rgbOperand[2] &&
                        u32(0x50) == d.alphaOperand[0] && u32(0x54) == d.alphaOperand[1] && u32(0x58) == d.alphaOperand[2] &&
                        u32(0x5c) == d.rgbScale && u32(0x60) == d.alphaScale;
        if (!ok) { std::printf("FAIL ctor defaults\n"); ++bad; }
    }
    for (int iter = 0; iter < 20000; ++iter) {
        alignas(8) unsigned char raw[0x80] = {};
        oracle_10060898c(raw);
        nSGLDX7::cTextureStageData d;
        auto put = [&](size_t o, uint32_t v) { std::memcpy(raw + o, &v, 4); };
        const bool small = iter % 2 == 0;  // mix in-range values with wild ones
        auto pick = [&](uint32_t mod) { return small ? R32() % mod : R32() % (mod * 2 + 3); };
        raw[1] = d.textureEnabled = R32() % 4 != 0;
        raw[2] = d.textureHasAlpha = R32() % 2;
        put(4, d.textureId = R32() % 6);
        put(0xc, d.magFilter = pick(2));
        put(0x10, d.minFilter = pick(8));
        put(0x14, d.wrapS = pick(4));
        put(0x18, d.wrapT = pick(4));
        put(0x1c, d.envMode = pick(6));
        put(0x20, d.appliedEnvMode = R32() % 3 == 0 ? d.envMode : R32() % 7);
        put(0x24, d.rgbCombine = pick(6));
        put(0x28, d.alphaCombine = pick(6));
        for (int k = 0; k < 3; ++k) {
            put(0x2c + 4 * k, d.rgbSource[k] = R32() % 4);
            put(0x38 + 4 * k, d.alphaSource[k] = R32() % 4);
            put(0x44 + 4 * k, d.rgbOperand[k] = 2 + R32() % 4);
            put(0x50 + 4 * k, d.alphaOperand[k] = 2 + R32() % 4);
        }
        put(0x5c, d.rgbScale = pick(4));
        put(0x60, d.alphaScale = pick(4));
        g_usable = R32() % 4 == 0 ? 0 : 0x3f;
        Host host;
        for (int which = 0; which < 5; ++which) {
            std::vector<Call> mine, ref;
            unsigned char copy[0x80];
            std::memcpy(copy, raw, sizeof raw);
            nSGLDX7::cTextureStageData e = d;
            g_log.clear();
            const int stage = int(R32() % 4);
            bool rb = true, rm = true;
            switch (which) {
                case 0: e.ApplyState(stage, host); mine = g_log; g_log.clear(); oracle_10060444c(copy, stage, &dev, nullptr); break;
                case 1: rm = e.ApplyTextureState(stage, host); mine = g_log; g_log.clear(); rb = oracle_1006089f2(copy, stage, &dev, nullptr) & 1; break;
                case 2: e.ApplyEnvModeState(stage, host); mine = g_log; g_log.clear(); oracle_100608c50(copy, stage, &dev); break;
                case 3: e.ApplyColorCombinerState(stage, host); mine = g_log; g_log.clear(); oracle_100608eb4(copy, stage, &dev); break;
                default: e.ApplyAlphaCombinerState(stage, host); mine = g_log; g_log.clear(); oracle_10060902c(copy, stage, &dev); break;
            }
            ref = g_log;
            uint32_t applied; std::memcpy(&applied, copy + 0x20, 4);
            if (mine != ref || rm != rb || applied != e.appliedEnvMode || copy[0] != e.dirty) {
                if (++bad < 6) {
                    std::printf("FAIL which=%d iter=%d (%zu vs %zu calls)\n", which, iter, mine.size(), ref.size());
                    for (size_t i = 0; i < mine.size() || i < ref.size(); ++i) {
                        if (i < mine.size()) std::printf("  mine %d %d %d %d", mine[i].kind, mine[i].a, mine[i].b, mine[i].c);
                        if (i < ref.size()) std::printf("   ref %d %d %d %d", ref[i].kind, ref[i].a, ref[i].b, ref[i].c);
                        std::printf("\n");
                    }
                }
            }
        }
    }
    if (bad) return 1;
    std::printf("texturestage oracle ok\n");
}

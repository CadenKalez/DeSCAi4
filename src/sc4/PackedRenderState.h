// cSC4PackedRenderState (0x100771d00) and the render-state diffing of SC4DrawContext::SetRenderState (0x100773906).
// A render state is a {value, mask} pair of 32-bit words; `mask` has a 0 bit for every field the creator passed as -1
// ("leave as is"). SC4DrawContext keeps the effective word (+0xd8) and sends the driver only the fields that changed.
//
//  bit  0 colour write        bit 1 depth write         bit 2 depth test        bit 3 alpha test      bit 4 blend
//  bit  5 texture stage 0     bit 6 cull face           bits 7-9 depth func     bits 10-12 alpha func
//  bits 13-16 blend src       bits 17-20 blend dst      bits 21-24 alpha ref (n/15)
//  bits 25-27 TexEnv, stage 0 bit 28 texture stage 1    bits 29-31 TexEnv, stage 1
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include <string>

#include "render/gd/GDriverBase.h"

struct cSC4PackedRenderState {
    std::uint32_t value = 0;
    std::uint32_t mask = 0xffffffff;

    // @ 0x100771d00. Parameter order as in the binary; -1 (or -1.0f for the alpha reference) means "don't care".
    // `textureStages`: 0 = none, 1 = stage 0, >= 2 = stages 0 and 1.
    cSC4PackedRenderState& PackRenderState(bool colorWrite, bool depthWrite, bool depthTest, bool alphaTest, bool blend,
                                           int textureStages, bool cull, int depthFunc, int blendSrc, int blendDst,
                                           int alphaFunc, float alphaRef, int texEnv0, int texEnv1);
};

// Field accessors for a (cached) state word.
namespace renderstate {
constexpr std::uint32_t kColorWrite = 1u << 0, kDepthWrite = 1u << 1, kDepthTest = 1u << 2, kAlphaTest = 1u << 3,
                        kBlend = 1u << 4, kTexture0 = 1u << 5, kCull = 1u << 6, kTexture1 = 1u << 28;
constexpr unsigned DepthFunc(std::uint32_t w) { return w >> 7 & 7; }
constexpr unsigned AlphaFunc(std::uint32_t w) { return w >> 10 & 7; }
constexpr unsigned BlendSrc(std::uint32_t w) { return w >> 13 & 0xf; }
constexpr unsigned BlendDst(std::uint32_t w) { return w >> 17 & 0xf; }
constexpr unsigned AlphaRef15(std::uint32_t w) { return w >> 21 & 0xf; }
constexpr unsigned TexEnv0(std::uint32_t w) { return w >> 25 & 7; }
constexpr unsigned TexEnv1(std::uint32_t w) { return w >> 29; }
}  // namespace renderstate

// What the device reports (SC4DrawContext reads these three bytes from the render-properties block).
struct SC4DeviceFlags {
    bool forceNoAlphaTest = false;      // +0x238
    bool forceNoTranslucency = false;   // +0x1f8
    bool secondTextureStage = true;     // +0x2f8
};

// The state-diff half of SC4DrawContext::SetRenderState(cSC4PackedRenderState const&): applies the changed fields to the
// driver and updates the cached word. (The tail of the original also resets texture wrap/filter modes and clears the
// texture transforms; that belongs with the rest of SC4DrawContext.)
class SC4RenderStateCache {
public:
    SC4RenderStateCache(gd::cIGZGDriver* driver, SC4DeviceFlags flags) : driver_(driver), flags_(flags) {}
    void SetRenderState(const cSC4PackedRenderState& state);
    std::uint32_t Word() const { return word_; }
    void ForceWord(std::uint32_t word) { word_ = word; }   // SetDefaultRenderStateUnilaterally stores ~default first
    const SC4DeviceFlags& Flags() const { return flags_; }

private:
    gd::cIGZGDriver* driver_;
    SC4DeviceFlags flags_;
    std::uint32_t word_ = 0;
};

// Debug check (RENDER_ARCH.md 8.2): compares every field of the state word the draw context believes the driver is in against
// the driver's own mirror of what it was told. Returns "" when they agree, else the name of the first field that differs.
// Fields the word never set (still 0) are compared too: the harness starts both sides from the same defaults.
std::string FirstRenderStateMismatch(const gd::StateMirror& driver, std::uint32_t word, const SC4DeviceFlags& flags);
#define SC4_ASSERT_RENDER_STATE(driver, word, flags)                                                          \
    do {                                                                                                      \
        const std::string mismatch_ = FirstRenderStateMismatch((driver), (word), (flags));                    \
        if (!mismatch_.empty()) { std::fprintf(stderr, "render state mismatch: %s\n", mismatch_.c_str()); std::abort(); } \
    } while (0)

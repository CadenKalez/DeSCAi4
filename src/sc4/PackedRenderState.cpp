#include "sc4/PackedRenderState.h"

#include <bit>

#pragma STDC FP_CONTRACT OFF

cSC4PackedRenderState& cSC4PackedRenderState::PackRenderState(bool colorWrite, bool depthWrite, bool depthTest, bool alphaTest,
                                                              bool blend, int textureStages, bool cull, int depthFunc,
                                                              int blendSrc, int blendDst, int alphaFunc, float alphaRef,
                                                              int texEnv0, int texEnv1) {
    mask = 0xffffffff;
    std::uint32_t v = colorWrite ? 1u : 0u;
    if (depthWrite) v += 2;
    if (depthTest) v |= 4;
    if (alphaTest) v |= 8;
    if (blend) v |= 0x10;
    if (textureStages < 2) {
        if (textureStages != 0) v |= 0x20;
    } else {
        v |= 0x10000020;
    }
    if (cull) v |= 0x40;
    if (depthFunc == -1) mask = 0xfffffc7f;
    else v |= (static_cast<std::uint32_t>(depthFunc) & 7) << 7;
    if (blendSrc == -1) mask &= 0xfffe1fff;
    else v |= (static_cast<std::uint32_t>(blendSrc) & 0xf) << 13;
    if (blendDst == -1) mask &= 0xffe1ffff;
    else v |= (static_cast<std::uint32_t>(blendDst) & 0xf) << 17;
    if (alphaFunc == -1) mask &= 0xffffe3ff;
    else v |= (static_cast<std::uint32_t>(alphaFunc) & 7) << 10;
    if (alphaRef != -1.0f || alphaRef != alphaRef) {
        // (x + 1.5 * 2^23) rounds x to the nearest integer in the low mantissa bits (ties to even)
        const float biased = alphaRef * 15.0f + 12582912.0f;
        v |= (std::bit_cast<std::uint32_t>(biased) & 0xf) << 21;
    } else {
        mask &= 0xfe1fffff;
    }
    if (texEnv0 == -1) mask &= 0xf1ffffff;
    else v |= (static_cast<std::uint32_t>(texEnv0) & 7) << 25;
    if (texEnv1 == -1) mask &= 0x1fffffff;
    else v |= static_cast<std::uint32_t>(texEnv1) << 29;
    value = v;
    return *this;
}

// SC4DrawContext::SetRenderState(cSC4PackedRenderState const&) @ 0x100773906.
void SC4RenderStateCache::SetRenderState(const cSC4PackedRenderState& state) {
    using namespace gd;
    const std::uint32_t changed = (state.value ^ word_) & state.mask;
    if (changed == 0) return;
    word_ = (word_ & ~state.mask) | state.value;
    driver_->TexStage(0);
    if (changed & renderstate::kColorWrite) driver_->ColorMask((word_ & renderstate::kColorWrite) != 0);
    if (changed & renderstate::kDepthWrite) driver_->DepthMask((word_ & renderstate::kDepthWrite) != 0);
    auto toggle = [&](bool on, eGDDriverState s) { if (on) driver_->Enable(s); else driver_->Disable(s); };
    if (changed & renderstate::kDepthTest) toggle((word_ & renderstate::kDepthTest) != 0, state::DepthTest);
    if (changed & renderstate::kAlphaTest) toggle((word_ & renderstate::kAlphaTest) != 0 && !flags_.forceNoAlphaTest, state::AlphaTest);
    if (changed & renderstate::kBlend) toggle((word_ & renderstate::kBlend) != 0 && !flags_.forceNoTranslucency, state::Blend);
    if (changed & renderstate::kTexture0) toggle((word_ & renderstate::kTexture0) != 0, state::Texturing);
    if (changed & renderstate::kCull) toggle((word_ & renderstate::kCull) != 0, state::CullFace);
    if (changed & 0x380) driver_->DepthFunc(renderstate::DepthFunc(word_));
    if (changed & 0x1e01c00)
        driver_->AlphaFunc(renderstate::AlphaFunc(word_), static_cast<float>(renderstate::AlphaRef15(word_)) * 0.06666667f);
    if (changed & 0x1fe000) driver_->BlendFunc(renderstate::BlendSrc(word_), renderstate::BlendDst(word_));
    if (changed & 0xe000000) driver_->TexEnv(0, 0, renderstate::TexEnv0(word_));
    if (changed >= 0x10000000 && flags_.secondTextureStage) {
        driver_->TexStage(1);
        if (changed & renderstate::kTexture1) toggle((word_ >> 24 & 0x10) != 0, state::Texturing);
        if (changed >= 0x20000000) driver_->TexEnv(0, 0, word_ >> 29);
        driver_->TexStage(0);
    }
}

std::string FirstRenderStateMismatch(const gd::StateMirror& d, std::uint32_t w, const SC4DeviceFlags& flags) {
    using namespace renderstate;
    if (d.colorMask != ((w & kColorWrite) != 0)) return "colorWrite";
    if (d.depthWrite != ((w & kDepthWrite) != 0)) return "depthWrite";
    if (d.depthTest != ((w & kDepthTest) != 0)) return "depthTest";
    if (d.alphaTest != ((w & kAlphaTest) != 0 && !flags.forceNoAlphaTest)) return "alphaTest";
    if (d.blend != ((w & kBlend) != 0 && !flags.forceNoTranslucency)) return "blend";
    if (d.stage[0].enabled != ((w & kTexture0) != 0)) return "texture0";
    if (d.cull != ((w & kCull) != 0)) return "cull";
    if (d.depthFunc != DepthFunc(w)) return "depthFunc";
    if (d.alphaFunc != AlphaFunc(w)) return "alphaFunc";
    if (d.alphaRef != static_cast<float>(AlphaRef15(w)) * 0.06666667f) return "alphaRef";
    if (d.blendSrc != BlendSrc(w)) return "blendSrc";
    if (d.blendDst != BlendDst(w)) return "blendDst";
    if (d.stage[0].envMode != TexEnv0(w)) return "texEnv0";
    if (flags.secondTextureStage) {
        if (d.stage[1].enabled != ((w & kTexture1) != 0)) return "texture1";
        if (d.stage[1].envMode != TexEnv1(w)) return "texEnv1";
    }
    return {};
}

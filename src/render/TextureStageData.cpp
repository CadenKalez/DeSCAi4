#include "render/TextureStageData.h"

namespace nSGLDX7 {
namespace {

// vLookupCombineSource: combiner source -> argument source.
constexpr int kCombineSourceToArg[4] = {kTaTexture, kTaCurrent, kTaTFactor, kTaDiffuse};
// Interpolate (src2) -> blend operation.
constexpr int kInterpolateOp[4] = {kTopBlendTextureAlpha, kTopBlendCurrentAlpha, kTopBlendFactorAlpha, kTopBlendDiffuseAlpha};

int ColorArg(uint32_t source, uint32_t operand) {
    int v = source < 4 ? kCombineSourceToArg[source] : 0;
    if (operand == 5) v |= 0x30;
    else if (operand == 4) v |= 0x20;
    else if (operand == 3) v |= 0x10;
    return v;
}

int AlphaArg(uint32_t source, uint32_t operand) {
    int v = source < 4 ? kCombineSourceToArg[source] : 0;
    if (operand == 5) v |= 0x10;
    return v;
}

// Shared by the colour and alpha combiners: function + scale + third source -> operation.
int CombineOp(uint32_t function, uint32_t scale, uint32_t source2) {
    switch (function) {
        case 0: return kTopSelectArg1;
        case 1: return scale < 3 ? int(scale) + kTopModulate : 0;
        case 2: return kTopAdd;
        case 3: return kTopAddSigned2x - (scale == 0 ? 1 : 0);
        case 4: return source2 < 4 ? kInterpolateOp[source2] : 0;
        case 5: return kTopDotProduct3;
        default: return kTopDisable;
    }
}

}  // namespace

cTextureStageData::cTextureStageData() = default;

void cTextureStageData::ApplyState(int stage, ITextureStageHost& host) {
    dirty = 0;
    if (ApplyTextureState(stage, host) && appliedEnvMode != envMode) {
        appliedEnvMode = envMode;
        ApplyEnvModeState(stage, host);
    }
}

bool cTextureStageData::ApplyTextureState(int stage, ITextureStageHost& host) {
    if (!textureEnabled || !host.IsTextureUsable(textureId)) {
        appliedEnvMode = 0xffffffffu;
        host.SetTextureStageState(stage, kTssColorOp, kTopDisable);
        host.SetTextureStageState(stage, kTssAlphaOp, kTopDisable);
        return false;
    }
    host.BindTexture(stage, textureId);
    if (magFilter == 1) host.SetTextureStageState(stage, kTssMagFilter, 2);
    else if (magFilter == 0) host.SetTextureStageState(stage, kTssMagFilter, 1);
    // minFilter: min value / mip value (1 point-or-none, 2 linear-or-point, 3 linear mip). Mode 5 carries on
    // into mode 6's assignments, as in the original.
    switch (minFilter) {
        case 0: host.SetTextureStageState(stage, kTssMinFilter, 1); host.SetTextureStageState(stage, kTssMipFilter, 1); break;
        case 1: host.SetTextureStageState(stage, kTssMinFilter, 2); host.SetTextureStageState(stage, kTssMipFilter, 1); break;
        case 4: host.SetTextureStageState(stage, kTssMinFilter, 1); host.SetTextureStageState(stage, kTssMipFilter, 2); break;
        case 5:
            host.SetTextureStageState(stage, kTssMinFilter, 2);
            host.SetTextureStageState(stage, kTssMipFilter, 2);
            [[fallthrough]];
        case 6: host.SetTextureStageState(stage, kTssMinFilter, 1); host.SetTextureStageState(stage, kTssMipFilter, 3); break;
        case 7: host.SetTextureStageState(stage, kTssMinFilter, 2); host.SetTextureStageState(stage, kTssMipFilter, 3); break;
        default: break;
    }
    if (wrapS == 2) host.SetTextureStageState(stage, kTssAddressU, 3);
    else if (wrapS == 3) host.SetTextureStageState(stage, kTssAddressU, 1);
    if (wrapT == 2) host.SetTextureStageState(stage, kTssAddressV, 3);
    else if (wrapT == 3) host.SetTextureStageState(stage, kTssAddressV, 1);
    return true;
}

void cTextureStageData::ApplyEnvModeState(int stage, ITextureStageHost& host) {
    const auto set = [&](int state, int value) { host.SetTextureStageState(stage, state, value); };
    switch (envMode) {
        case 0:  // replace
            set(kTssColorArg1, kTaTexture); set(kTssColorOp, kTopSelectArg1);
            set(kTssAlphaArg1, kTaTexture); set(kTssAlphaOp, kTopSelectArg1);
            break;
        case 1:  // modulate
            set(kTssColorOp, kTopModulate); set(kTssColorArg1, kTaTexture); set(kTssColorArg2, kTaCurrent);
            if (!textureHasAlpha) {
                set(kTssAlphaOp, kTopSelectArg1);
                set(kTssAlphaArg1, kTaCurrent);
            } else {
                set(kTssAlphaOp, kTopModulate); set(kTssAlphaArg1, kTaTexture); set(kTssAlphaArg2, kTaCurrent);
            }
            break;
        case 2:  // decal
            if (!textureHasAlpha) {
                set(kTssColorOp, kTopSelectArg1); set(kTssColorArg1, kTaTexture);
            } else {
                set(kTssColorArg1, kTaTexture); set(kTssColorOp, kTopBlendTextureAlpha); set(kTssColorArg2, kTaCurrent);
            }
            set(kTssAlphaOp, kTopSelectArg1);
            set(kTssAlphaArg1, kTaCurrent);
            break;
        case 3:  // blend: not supported, stage passes the previous result through
            set(kTssColorOp, kTopDisable);
            set(kTssAlphaOp, kTopDisable);
            break;
        case 4:
        case 5:
            ApplyColorCombinerState(stage, host);
            ApplyAlphaCombinerState(stage, host);
            break;
        default: break;
    }
}

void cTextureStageData::ApplyColorCombinerState(int stage, ITextureStageHost& host) {
    host.SetTextureStageState(stage, kTssColorArg1, ColorArg(rgbSource[0], rgbOperand[0]));
    host.SetTextureStageState(stage, kTssColorArg2, ColorArg(rgbSource[1], rgbOperand[1]));
    host.SetTextureStageState(stage, kTssColorOp, CombineOp(rgbCombine, rgbScale, rgbSource[2]));
}

void cTextureStageData::ApplyAlphaCombinerState(int stage, ITextureStageHost& host) {
    host.SetTextureStageState(stage, kTssAlphaArg1, AlphaArg(alphaSource[0], alphaOperand[0]) & 0xffff);
    host.SetTextureStageState(stage, kTssAlphaArg2, AlphaArg(alphaSource[1], alphaOperand[1]) & 0xffff);
    host.SetTextureStageState(stage, kTssAlphaOp, CombineOp(alphaCombine, alphaScale, alphaSource[2]));
}

}  // namespace nSGLDX7

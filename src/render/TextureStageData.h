// nSGLDX7::cTextureStageData: per-stage texture state (filtering, wrapping, GL-style texture environment
// and combiner) and its translation into fixed-function stage operations. The translation is emitted through
// ITextureStageHost so a backend (Metal: a fragment-function variant selector) can consume the exact
// operation/argument codes the original produced. Codes follow Direct3D 7's texture-stage numbering.
#pragma once
#include <cstdint>

namespace nSGLDX7 {

// Texture stage state identifiers (D3DTEXTURESTAGESTATETYPE).
enum TextureStageState : int {
    kTssColorOp = 1, kTssColorArg1 = 2, kTssColorArg2 = 3, kTssAlphaOp = 4, kTssAlphaArg1 = 5, kTssAlphaArg2 = 6,
    kTssAddressU = 0xd, kTssAddressV = 0xe, kTssMagFilter = 0x10, kTssMinFilter = 0x11, kTssMipFilter = 0x12,
};
// Operation codes (D3DTEXTUREOP) the stage emits.
enum TextureOp : int {
    kTopDisable = 1, kTopSelectArg1 = 2, kTopSelectArg2 = 3, kTopModulate = 4, kTopModulate2x = 5, kTopModulate4x = 6,
    kTopAdd = 7, kTopAddSigned = 8, kTopAddSigned2x = 9, kTopBlendDiffuseAlpha = 0xc, kTopBlendTextureAlpha = 0xd,
    kTopBlendFactorAlpha = 0xe, kTopBlendCurrentAlpha = 0x10, kTopDotProduct3 = 0x18,
};
// Argument sources (D3DTA_*) and modifiers.
enum TextureArg : int {
    kTaDiffuse = 0, kTaCurrent = 1, kTaTexture = 2, kTaTFactor = 3, kTaComplement = 0x10, kTaAlphaReplicate = 0x20,
};

class ITextureStageHost {
public:
    virtual ~ITextureStageHost() = default;
    // True when the texture id names a texture that currently has pixel storage.
    virtual bool IsTextureUsable(uint32_t textureId) = 0;
    virtual void BindTexture(int stage, uint32_t textureId) = 0;
    virtual void SetTextureStageState(int stage, int state, int value) = 0;
};

class cTextureStageData {
public:
    cTextureStageData();

    // Resets the dirty flag, applies filter/wrap state and (if the environment mode changed) the combiner.
    void ApplyState(int stage, ITextureStageHost& host);
    // Binds the texture and applies filter/wrap state. Returns false (and disables the stage) when the stage
    // has no usable texture.
    bool ApplyTextureState(int stage, ITextureStageHost& host);
    void ApplyEnvModeState(int stage, ITextureStageHost& host);
    void ApplyColorCombinerState(int stage, ITextureStageHost& host);
    void ApplyAlphaCombinerState(int stage, ITextureStageHost& host);

    // --- state (the original keeps these in a 0x80-byte record) ---
    uint8_t dirty = 0;               // +0x00, cleared by ApplyState
    uint8_t textureEnabled = 0;      // +0x01
    uint8_t textureHasAlpha = 1;     // +0x02, selects the alpha path of the MODULATE/DECAL environments
    uint32_t textureId = 0;          // +0x04
    uint32_t reserved08 = 0;         // +0x08
    uint32_t magFilter = 0;          // +0x0c: 0 nearest, 1 linear
    uint32_t minFilter = 0;          // +0x10: 0..7 (GL min-filter order)
    uint32_t wrapS = 3;              // +0x14: 2 clamp, 3 repeat
    uint32_t wrapT = 3;              // +0x18
    uint32_t envMode = 0;            // +0x1c: 0 replace, 1 modulate, 2 decal, 3 blend (stage off), 4/5 combine
    uint32_t appliedEnvMode = 0xffffffffu;  // +0x20
    uint32_t rgbCombine = 0;         // +0x24
    uint32_t alphaCombine = 0;       // +0x28
    uint32_t rgbSource[3] = {3, 3, 3};   // +0x2c..0x34
    uint32_t alphaSource[3] = {3, 3, 3}; // +0x38..0x40
    uint32_t rgbOperand[3] = {2, 2, 2};  // +0x44..0x4c
    uint32_t alphaOperand[3] = {4, 4, 4};// +0x50..0x58
    uint32_t rgbScale = 0;           // +0x5c
    uint32_t alphaScale = 0;         // +0x60
};

}  // namespace nSGLDX7

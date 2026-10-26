#include "dx7_to_metal_bridge.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace dx7 {
namespace {

void Identity(float* m) {
    std::memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

void UnpackColor(std::uint32_t argb, float* out) {   // D3DCOLOR 0xAARRGGBB
    out[0] = static_cast<float>((argb >> 16) & 0xff) / 255.0f;
    out[1] = static_cast<float>((argb >> 8) & 0xff) / 255.0f;
    out[2] = static_cast<float>(argb & 0xff) / 255.0f;
    out[3] = static_cast<float>(argb >> 24) / 255.0f;
}

void SetFlag(MetalRenderState& s, std::uint32_t flag, bool on) { s.flags = on ? (s.flags | flag) : (s.flags & ~flag); }

}  // namespace

StateRecorder::StateRecorder() {
    std::memset(&state_, 0, sizeof state_);
    Identity(state_.world); Identity(state_.view); Identity(state_.projection); Identity(state_.texMat0); Identity(state_.texMat1);
    state_.colorMul[0] = state_.colorMul[1] = state_.colorMul[2] = state_.colorMul[3] = 1.0f;
    state_.lightDirection[2] = -1.0f;
    state_.lightDiffuse[0] = state_.lightDiffuse[1] = state_.lightDiffuse[2] = state_.lightDiffuse[3] = 1.0f;
    state_.textureFactor[0] = state_.textureFactor[1] = state_.textureFactor[2] = state_.textureFactor[3] = 1.0f;
    state_.alphaFunc = 8;   // D3DCMP_ALWAYS
    state_.flags = kFlagLighting;
}

bool StateRecorder::SetTransform(std::uint32_t state, const float* m) {
    assert(m != nullptr);
    if (!m) { std::fprintf(stderr, "dx7: SetTransform(%#x) with a null matrix\n", state); return false; }
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(m[i])) {
            std::fprintf(stderr, "dx7: SetTransform(%#x) invalid matrix, element %d = %f; row0 %g %g %g %g\n", state, i, m[i], m[0], m[1], m[2], m[3]);
            return false;
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    float* target = nullptr;
    switch (state) {
    case kTransformWorld: target = state_.world; break;
    case kTransformView: target = state_.view; break;
    case kTransformProjection: target = state_.projection; break;
    case kTransformTexture0: target = state_.texMat0; state_.texMode |= 1u; break;
    default:
        ++unmapped_;
        std::fprintf(stderr, "dx7: unmapped transform state %#x, row0 %g %g %g %g\n", state, m[0], m[1], m[2], m[3]);
        return false;
    }
    std::memcpy(target, m, 16 * sizeof(float));
    return true;
}

bool StateRecorder::SetRenderState(std::uint32_t state, std::uint32_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (state) {
    case kRenderLighting: SetFlag(state_, kFlagLighting, value != 0); break;
    case kRenderAmbient: UnpackColor(value, state_.ambient); break;
    case kRenderTextureFactor: UnpackColor(value, state_.textureFactor); break;
    case kRenderAlphaTestEnable: SetFlag(state_, kFlagAlphaTest, value != 0); break;
    case kRenderAlphaRef: state_.alphaRef = value & 0xff; break;
    case kRenderAlphaFunc:
        if (value < 1 || value > 8) { std::fprintf(stderr, "dx7: invalid D3DCMP %#x for render state %#x\n", value, state); ++unmapped_; return false; }
        state_.alphaFunc = value;
        break;
    case kRenderZEnable: fixed_.zEnable = value; break;
    case kRenderZWriteEnable: fixed_.zWrite = value; break;
    case kRenderZFunc: fixed_.zFunc = value; break;
    case kRenderCullMode: fixed_.cullMode = value; break;
    case kRenderAlphaBlendEnable: fixed_.blendEnable = value; break;
    case kRenderSrcBlend: fixed_.srcBlend = value; break;
    case kRenderDestBlend: fixed_.dstBlend = value; break;
    case kRenderFogEnable: fixed_.fogEnable = value; break;
    case kRenderFillMode:
    case kRenderShadeMode: break;   // accepted, no shader or pipeline effect
    default:
        ++unmapped_;
        std::fprintf(stderr, "dx7: unmapped render state %#x = %#x\n", state, value);
        return false;
    }
    return true;
}

bool StateRecorder::SetLight(const float direction3[3], const float diffuse4[4]) {
    assert(direction3 && diffuse4);
    if (!direction3 || !diffuse4) { std::fprintf(stderr, "dx7: SetLight with a null argument\n"); return false; }
    std::lock_guard<std::mutex> lock(mutex_);
    std::memcpy(state_.lightDirection, direction3, 3 * sizeof(float));
    std::memcpy(state_.lightDiffuse, diffuse4, 4 * sizeof(float));
    return true;
}

bool StateRecorder::SetFVF(std::uint32_t fvf) {
    if ((fvf & kFvfXyzRhw) || !(fvf & kFvfXyz) || (fvf & ~(kFvfXyz | kFvfNormal | kFvfDiffuse | kFvfTex1))) {
        std::fprintf(stderr, "dx7: unmapped FVF %#x\n", fvf);
        std::lock_guard<std::mutex> lock(mutex_);
        ++unmapped_;
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    state_.fvf = fvf;
    SetFlag(state_, kFlagHasNormal, (fvf & kFvfNormal) != 0);
    SetFlag(state_, kFlagHasDiffuse, (fvf & kFvfDiffuse) != 0);
    return true;
}

void StateRecorder::SetTextureBound(bool bound) {
    std::lock_guard<std::mutex> lock(mutex_);
    SetFlag(state_, kFlagTexture, bound);
}

bool StateRecorder::SetStageTransform(std::uint32_t stage, const float* m) {
    if (stage > 1) { std::fprintf(stderr, "dx7: SetStageTransform for stage %#x\n", stage); return false; }
    std::lock_guard<std::mutex> lock(mutex_);
    float* target = stage == 0 ? state_.texMat0 : state_.texMat1;
    const std::uint32_t bit = 1u << stage;
    if (!m) { state_.texMode &= ~bit; return true; }
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(m[i])) {
            std::fprintf(stderr, "dx7: SetStageTransform(%u) invalid matrix, element %d = %f\n", stage, i, m[i]);
            return false;
        }
    }
    std::memcpy(target, m, 16 * sizeof(float));
    state_.texMode |= bit;
    return true;
}

void StateRecorder::SetStageCameraSpaceCoords(std::uint32_t stage, bool on) {
    if (stage > 1) return;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint32_t bit = 4u << stage;
    state_.texMode = on ? (state_.texMode | bit) : (state_.texMode & ~bit);
}

void StateRecorder::SetTextureFactor(const float rgba[4]) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::memcpy(state_.textureFactor, rgba, 4 * sizeof(float));
}

void StateRecorder::SetColorMultiplier(const float rgba[4]) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::memcpy(state_.colorMul, rgba, 4 * sizeof(float));
}

void StateRecorder::SetDebugMode(std::uint32_t bits) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.texMode = (state_.texMode & 0xffu) | (bits & ~0xffu);
}

MetalRenderState StateRecorder::Snapshot() const { std::lock_guard<std::mutex> lock(mutex_); return state_; }
MetalFixedState StateRecorder::SnapshotFixed() const { std::lock_guard<std::mutex> lock(mutex_); return fixed_; }
std::uint32_t StateRecorder::UnmappedCount() const { std::lock_guard<std::mutex> lock(mutex_); return unmapped_; }

}  // namespace dx7

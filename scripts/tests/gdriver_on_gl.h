// GDriverOnGL — the *reference* cIGZGDriver for the render diff harness: the same slot calls mapped onto IGLApi (MetalGL).
// cIGZGDriver is GL-shaped, so this is a thin, independent second implementation of the same semantics; the native
// cMetalGDriver is compared against it pixel by pixel. Test-only; implements just the calls the diff scenes use.
#pragma once
#include <map>
#include <vector>

#include "render/GDIndexOps.h"
#include "render/GLApi.h"
#include "render/gd/TextureTable.h"
#include "render/gd/GDriverBase.h"

namespace gd {

class GDriverOnGL final : public cGDriverBase {
public:
    explicit GDriverOnGL(sgl::IGLApi* gl) : gl_(gl) {}

    void Clear(std::uint32_t mask) override {
        const std::uint32_t argb = mirror_.clearColor;
        gl_->ClearColor(float(argb >> 16 & 0xff) / 255.0f, float(argb >> 8 & 0xff) / 255.0f, float(argb & 0xff) / 255.0f, float(argb >> 24) / 255.0f);
        gl_->ClearDepth(mirror_.clearDepth);
        std::uint32_t glMask = 0;
        if (mask & 0x4000) glMask |= 0x4000;
        if (mask & 0x1000) glMask |= 0x100;
        gl_->Clear(glMask);
    }
    void DepthFunc(eGDTestFunc f) override { cGDriverBase::DepthFunc(f); gl_->DepthFunc(0x200 + f); }
    void DepthMask(bool e) override { cGDriverBase::DepthMask(e); gl_->DepthMask(e); }
    void BlendFunc(eGDBlend s, eGDBlend d) override { cGDriverBase::BlendFunc(s, d); gl_->BlendFunc(Blend(s), Blend(d)); }
    void AlphaFunc(eGDTestFunc f, float ref) override {
        cGDriverBase::AlphaFunc(f, ref);
        gl_->AlphaFunc(0x200 + f, static_cast<float>(AlphaRefByte(ref)) / 255.0f);
    }
    void Enable(eGDDriverState s) override { cGDriverBase::Enable(s); Apply(s, true); }
    void Disable(eGDDriverState s) override { cGDriverBase::Disable(s); Apply(s, false); }
    void MatrixMode(eGDMatrixTarget t) override { target_ = t; }
    void LoadMatrix(const float* m) override {
        // The game hands over D3D row-vector matrices; the shim feeds them to glLoadMatrixf unchanged.
        gl_->MatrixMode(target_ == 0 ? sgl::gl::MODELVIEW : sgl::gl::PROJECTION);
        gl_->LoadMatrixf(m);
    }
    void LoadIdentity() override {
        gl_->MatrixMode(target_ == 0 ? sgl::gl::MODELVIEW : sgl::gl::PROJECTION);
        gl_->LoadIdentity();
    }
    int VertexFormatStride(eGDVertexFormat f) const override { return f == 11 ? 32 : 24; }
    void InterleavedArrays(eGDVertexFormat format, int stride, const void* data) override {
        stride_ = stride ? stride : VertexFormatStride(format);
        base_ = static_cast<const std::uint8_t*>(data);
        Rebase(0);
    }
    void DrawArrays(eGDPrimType mode, int first, int count) override {
        if (mode == prim::Quads || mode == prim::QuadStrip) {   // GL quads are not in the Metal scaffolding: expand like the driver does
            std::vector<std::uint16_t> idx(mode == prim::Quads ? static_cast<std::size_t>(count >> 1) * 3 : static_cast<std::size_t>(count - 2) * 3);
            if (mode == prim::Quads) nSGLDX7::GenerateIndexedTriListForQuads(count, idx.data());
            else nSGLDX7::GenerateIndexedTriListForQuadStrip(count, idx.data());
            Rebase(first);
            gl_->DrawElements(4, static_cast<int>(idx.size()), sgl::gl::UNSIGNED_SHORT, idx.data());
            Rebase(0);
            return;
        }
        Rebase(first);
        gl_->DrawArrays(GLMode(mode), 0, count);
        Rebase(0);
    }
    void DrawElements(eGDPrimType mode, int count, eGDType type, const void* indices) override {
        if (type == 3) gl_->DrawElements(GLMode(mode), count, sgl::gl::UNSIGNED_SHORT, indices);
        else Unsupported("DrawElements(type)");
    }
    void Flush() override {}

    // ---- textures: the same observable slots, implemented on GL objects ----
    void GenTextures(int n, std::uint32_t* ids) override { for (int i = 0; i < n; ++i) ids[i] = nextTexture_++; }
    std::uint32_t CreateTexture(eGDInternalTextureFormat fmt, std::uint32_t w, std::uint32_t h, std::uint32_t mips, eGDTextureHintFlags) override {
        const std::uint32_t id = nextTexture_++;
        tex_[id] = {fmt, w, h, mips, 0};
        return id;
    }
    void LoadTextureLevel(std::uint32_t id, std::uint32_t level, std::uint32_t x, std::uint32_t y, std::uint32_t w, std::uint32_t h,
                          eGDTextureFormat format, eGDType type, std::uint32_t rowLength, const void* pixels) override {
        Tex& t = tex_[id];
        std::uint32_t src;
        if (!nSGLDX7::TranslateGDFormatToSourceFormat(static_cast<int>(format), type, &src)) return;
        std::vector<std::uint8_t> bgra;
        const int rowBytes = static_cast<int>(((rowLength ? rowLength : w) * nSGLDX7::GetSourceFormatNumBits(src)) >> 3);
        if (!ConvertToBGRA8(src, t.format, pixels, rowBytes, static_cast<int>(w), static_cast<int>(h), bgra)) return;
        if (!t.glId) { std::uint32_t g; gl_->GenTextures(1, &g); t.glId = g; }
        gl_->ActiveTexture(sgl::gl::TEXTURE0);
        gl_->BindTexture(sgl::gl::TEXTURE_2D, t.glId);
        if (x == 0 && y == 0) gl_->TexImage2D(sgl::gl::TEXTURE_2D, static_cast<int>(level), 0x8058, w, h, 0, 0x80e1, 0x8367, bgra.data());
        else gl_->TexSubImage2D(sgl::gl::TEXTURE_2D, static_cast<int>(level), x, y, w, h, 0x80e1, 0x8367, bgra.data());
    }
    void BindTexture(eGDTextureTarget, std::uint32_t id) override { stageTexture_[stage_] = id; }
    void SetTexture(std::uint32_t id, std::uint32_t stage) override { stageTexture_[stage & 1] = id; }
    void TexStage(std::uint32_t s) override { cGDriverBase::TexStage(s); stage_ = s & 1; }
    void TexEnv(eGDTextureEnvTarget t, eGDTextureEnvParamType p, eGDTextureEnvModeParam mode) override { cGDriverBase::TexEnv(t, p, mode); env_[stage_] = mode; }
    void TexParameter(eGDTextureTarget, eGDTextureParamType param, eGDTextureParam value) override {
        if (param == 0) mag_[stage_] = value;
        else if (param == 1) min_[stage_] = value;
        else if (param == 2) wrapS_[stage_] = value;
        else if (param == 3) wrapT_[stage_] = value;
    }

private:
    struct Tex { std::uint32_t format, w, h, mips; std::uint32_t glId; };
    void ApplyTexture() {   // stage 0 only (the diff scenes use one stage)
        const bool on = mirror_.stage[0].enabled && tex_.count(stageTexture_[0]) && tex_[stageTexture_[0]].glId;
        gl_->ActiveTexture(sgl::gl::TEXTURE0);
        if (!on) { gl_->Disable(sgl::gl::TEXTURE_2D); return; }
        Tex& t = tex_[stageTexture_[0]];
        gl_->Enable(sgl::gl::TEXTURE_2D);
        gl_->BindTexture(sgl::gl::TEXTURE_2D, t.glId);
        static const std::uint32_t minMap[8] = {0x2600, 0x2601, 0x2600, 0x2601, 0x2700, 0x2701, 0x2702, 0x2703};
        gl_->TexParameteri(sgl::gl::TEXTURE_2D, 0x2800, mag_[0] == 1 ? 0x2601 : 0x2600);
        gl_->TexParameteri(sgl::gl::TEXTURE_2D, 0x2801, minMap[min_[0] & 7]);
        gl_->TexParameteri(sgl::gl::TEXTURE_2D, 0x2802, wrapS_[0] == 2 ? 0x2900 : 0x2901);
        gl_->TexParameteri(sgl::gl::TEXTURE_2D, 0x2803, wrapT_[0] == 2 ? 0x2900 : 0x2901);
        const bool hasAlpha = nSGLDX7::GetFormatInfoNumAlphaBits(static_cast<int>(t.format)) > 1;
        if (env_[0] == 0) {   // replace
            gl_->TexEnvi(0x2300, 0x8571, 0x1e01); gl_->TexEnvi(0x2300, 0x8580, 0x1702);
            gl_->TexEnvi(0x2300, 0x8572, 0x1e01); gl_->TexEnvi(0x2300, 0x8588, 0x1702);
        } else {              // modulate; an alpha-less texture leaves the incoming alpha alone
            gl_->TexEnvi(0x2300, 0x8571, 0x2100); gl_->TexEnvi(0x2300, 0x8580, 0x1702); gl_->TexEnvi(0x2300, 0x8581, 0x8578);
            if (hasAlpha) { gl_->TexEnvi(0x2300, 0x8572, 0x2100); gl_->TexEnvi(0x2300, 0x8588, 0x1702); gl_->TexEnvi(0x2300, 0x8589, 0x8578); }
            else { gl_->TexEnvi(0x2300, 0x8572, 0x1e01); gl_->TexEnvi(0x2300, 0x8588, 0x8578); }
        }
    }
    static std::uint32_t Blend(eGDBlend b) {
        static const std::uint32_t t[11] = {0, 1, 0x300, 0x301, 0x302, 0x303, 0x304, 0x305, 0x306, 0x307, 0x308};
        return t[b];
    }
    static std::uint32_t GLMode(eGDPrimType m) {
        static const std::uint32_t t[8] = {4, 5, 6, 0, 1, 3, 7, 8};   // triangles, strip, fan, points, lines, line strip, quads, quad strip
        return t[m];
    }
    void Apply(eGDDriverState s, bool on) {
        auto set = [&](std::uint32_t cap) { if (on) gl_->Enable(cap); else gl_->Disable(cap); };
        switch (s) {
        case state::AlphaTest: set(sgl::gl::ALPHA_TEST); break;
        case state::DepthTest: set(sgl::gl::DEPTH_TEST); break;
        case state::Blend: set(sgl::gl::BLEND); break;
        case state::CullFace:
            set(sgl::gl::CULL_FACE);
            if (on) { gl_->FrontFace(0x901); gl_->CullFace(0x405); }
            break;
        default: break;
        }
    }
    void Rebase(int first) {
        ApplyTexture();
        const std::uint8_t* b = base_ + static_cast<std::size_t>(first) * stride_;
        gl_->ClientActiveTexture(sgl::gl::TEXTURE0);
        gl_->EnableClientState(sgl::gl::TEXTURE_COORD_ARRAY);
        gl_->TexCoordPointer(2, sgl::gl::FLOAT, stride_, b + 16);
        gl_->EnableClientState(sgl::gl::VERTEX_ARRAY);
        gl_->VertexPointer(3, sgl::gl::FLOAT, stride_, b);
        gl_->EnableClientState(sgl::gl::COLOR_ARRAY);
        gl_->ColorPointer(4, sgl::gl::UNSIGNED_BYTE, stride_, b + 12);
    }
    sgl::IGLApi* gl_;
    eGDMatrixTarget target_ = 0;
    const std::uint8_t* base_ = nullptr;
    int stride_ = 24;
    std::uint32_t nextTexture_ = 1;
    std::map<std::uint32_t, Tex> tex_;
    std::uint32_t stageTexture_[2] = {0, 0}, env_[2] = {1, 1}, mag_[2] = {0, 0}, min_[2] = {0, 0}, wrapS_[2] = {3, 3}, wrapT_[2] = {3, 3};
    std::uint32_t stage_ = 0;
};

}  // namespace gd

// GDriverOnGL — the *reference* cIGZGDriver for the render diff harness: the same slot calls mapped onto IGLApi (MetalGL).
// cIGZGDriver is GL-shaped, so this is a thin, independent second implementation of the same semantics; the native
// cMetalGDriver is compared against it pixel by pixel. Test-only; implements just the calls the diff scenes use.
#pragma once
#include <vector>

#include "render/GDIndexOps.h"
#include "render/GLApi.h"
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

private:
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
        const std::uint8_t* b = base_ + static_cast<std::size_t>(first) * stride_;
        gl_->EnableClientState(sgl::gl::VERTEX_ARRAY);
        gl_->VertexPointer(3, sgl::gl::FLOAT, stride_, b);
        gl_->EnableClientState(sgl::gl::COLOR_ARRAY);
        gl_->ColorPointer(4, sgl::gl::UNSIGNED_BYTE, stride_, b + 12);
    }
    sgl::IGLApi* gl_;
    eGDMatrixTarget target_ = 0;
    const std::uint8_t* base_ = nullptr;
    int stride_ = 24;
};

}  // namespace gd

#include "render/GLState.h"

#include <cstring>

namespace sgl {

namespace {
void Put32(void* base, std::size_t off, std::uint32_t v) { std::memcpy(static_cast<std::uint8_t*>(base) + off, &v, 4); }
void Put64(void* base, std::size_t off, std::uint64_t v) { std::memcpy(static_cast<std::uint8_t*>(base) + off, &v, 8); }
}  // namespace

void GetDefaultOpenGLTexState(GLenum target, GLTexState* out) {
    const std::uint32_t wrap = target == 0x84f5 ? 0x812f : 0x2901;      // rectangle textures clamp, others repeat
    const std::uint32_t minFilter = target == 0x84f5 ? 0x2601 : 0x2702;
    out->wrapS = wrap;
    out->wrapT = wrap;
    out->wrapR = wrap;
    out->minFilter = minFilter;
    out->magFilter = 0x2601;
    Put64(&out->anisotropy, 0, 0x3f800000);   // 1.0f with a zero high word
    Put64(&out->maxLevel, 0, 1000);
}

void GetDefaultOpenGLState(GLStageState* s) {
    std::memset(s->bytes, 0, sizeof(s->bytes));   // the original leaves the untouched gaps as they were (all zero here)
    Put64(s, 0x20, 0); Put64(s, 0x18, 0); Put64(s, 0x10, 0); Put64(s, 8, 0); Put64(s, 0, 0);
    Put64(s, 0x28, 0x210000002100);
    Put64(s, 0x30, 0x170200002100);
    Put64(s, 0x38, 0x857600008578);
    Put64(s, 0x40, 0x857800001702);
    Put64(s, 0x48, 0x30000008576);
    Put64(s, 0x50, 0x30200000300);
    Put64(s, 0x58, 0x30200000302);
    Put64(s, 0x60, 0x100000302);
    Put32(s, 0x68, 1);
    Put64(s, 0x7c, 0); Put64(s, 0x74, 0); Put64(s, 0x6c, 0);
    Put64(s, 0x84, 0x240000002400);
    Put64(s, 0x8c, 0x240000002400);
    Put32(s, 0xd4, 0x3f800000);
    Put32(s, 0x94, 0x3f800000);
    Put64(s, 0xa0, 0); Put64(s, 0x98, 0);
    Put64(s, 0xe0, 0); Put64(s, 0xd8, 0);
    Put32(s, 0xe8, 0x3f800000);
    Put32(s, 0xa8, 0x3f800000);
    Put64(s, 0xcc, 0); Put64(s, 0xc4, 0); Put64(s, 0xbc, 0); Put64(s, 0xb4, 0); Put64(s, 0xac, 0);
    Put64(s, 0x10c, 0); Put64(s, 0x104, 0); Put64(s, 0xfc, 0); Put64(s, 0xf4, 0); Put64(s, 0xec, 0);
}

void GetDefaultOpenGLState(GLState* st) {
    std::memset(st->bytes, 0, sizeof(st->bytes));
    Put64(st, 0x00, 0x800600000000);
    Put64(st, 0x08, 1);
    Put64(st, 0x10, 0x800600000000);
    Put64(st, 0x18, 1);
    Put64(st, 0x20, 0x20700000000);
    Put32(st, 0x28, 0);
    Put32(st, 0x2c, 0x333333ff);                 // a 16-byte store of {0x333333ff, 0, 0, 0}
    Put32(st, 0x30, 0); Put32(st, 0x34, 0); Put32(st, 0x38, 0);
    Put32(st, 0x3c, 0x408);
    Put64(st, 0x40, 0x1602);
    Put64(st, 0x48, 0x90100000405);
    Put64(st, 0x50, 0x1b0200000001);
    Put64(st, 0x58, 0x1b02);
    Put64(st, 0x60, 0);
    Put64(st, 0x68, 0x3f8000003f800000);
    Put64(st, 0x70, 0x845200000800);
    Put64(st, 0x78, 0);
    Put64(st, 0x80, 0x1d01);
    Put64(st, 0x88, 0xccccccff333333ff);
    Put64(st, 0x90, 0xff000000ff);
    Put32(st, 0xa8, 0);
    Put64(st, 0xa0, 0);
    Put64(st, 0x98, 0);
    Put64(st, 0xac, 0x100000201);
    Put64(st, 0xb4, 0);
    Put64(st, 0xbc, 0xffffffff00000404);
    Put64(st, 0xc4, 0x1e0000001e00);
    Put32(st, 0xcc, 0x1e00);
    Put32(st, 0xd0, 0x207);
    Put64(st, 0xd4, 0xffffffff00000000);
    Put64(st, 0xdc, 0x1e0000001e00);
    Put64(st, 0xe4, 0x20700001e00);
    Put64(st, 0xec, 0xffffffff00000000);
    Put64(st, 0xf4, 0x100000001);
    Put64(st, 0xfc, 0x100000001);
    Put32(st, 0x104, 0);
    Put32(st, 0x108, 0);
    Put32(st, 0x10c, 0);
    for (std::size_t off = 0x110; off < 0x190; off += 0x10) { Put64(st, off, 0); Put64(st, off + 8, 0); }
    Put64(st, 0x190, 0);
}

void CClientArray::SetEnable(GLenum array, std::int32_t enable) {
    if (enabled != static_cast<std::int32_t>(kUnset) && enabled == enable) return;
    enabled = enable;
    if (enable != 0) GL()->EnableClientState(array);
    else GL()->DisableClientState(array);
}

void CTextureCoordArray::SetPointer(std::int32_t s, GLenum t, std::int32_t st, const void* p) {
    if (SameAs(s, t, st, p)) return;
    Remember(s, t, st, p);
    GL()->TexCoordPointer(s, t, st, p);
}
void CVertexArray::SetPointer(std::int32_t s, GLenum t, std::int32_t st, const void* p) {
    if (SameAs(s, t, st, p)) return;
    Remember(s, t, st, p);
    GL()->VertexPointer(s, t, st, p);
}
void CColorArray::SetPointer(std::int32_t s, GLenum t, std::int32_t st, const void* p) {
    if (SameAs(s, t, st, p)) return;
    Remember(s, t, st, p);
    GL()->ColorPointer(s, t, st, p);
}

void CTextureStage::SetEnabled(std::int32_t on) {
    if (enabled != static_cast<std::int32_t>(kUnset) && enabled == on) return;
    enabled = on;
    if (on != 0) GL()->Enable(gl::TEXTURE_2D);
    else GL()->Disable(gl::TEXTURE_2D);
}

// Pushes the D3D sampler state (address / filter modes) to the bound GL texture, only where it differs from what the
// texture already has.
void CTextureStage::SetTexture(OpenGLTextureState& ts, IDirectDrawSurface7* surface) {
    texture = surface;
    if (!surface) return;
    IGLApi* g = GL();
    g->BindTexture(gl::TEXTURE_2D, ts.id);
    const std::int32_t mag = 0x2601 - (state[0x10] == 1 ? 1 : 0);
    if (static_cast<std::int32_t>(ts.tex.magFilter) != mag) {
        g->TexParameteri(gl::TEXTURE_2D, 0x2800, mag);
        ts.tex.magFilter = mag;
    }
    const std::uint32_t wrapS = (state[0xd] == 1 ? 1u : 0u) | 0x2900;
    if (ts.tex.wrapS != wrapS) {
        g->TexParameteri(gl::TEXTURE_2D, 0x2802, wrapS);
        ts.tex.wrapS = wrapS;
    }
    const std::uint32_t wrapT = (state[0xe] == 1 ? 1u : 0u) | 0x2900;
    if (ts.tex.wrapT != wrapT) {
        g->TexParameteri(gl::TEXTURE_2D, 0x2803, wrapT);
        ts.tex.wrapT = wrapT;
    }
    const std::int32_t minFilter = static_cast<std::int32_t>(state[0x11]);
    const std::int32_t mipFilter = static_cast<std::int32_t>(state[0x12]);
    std::int32_t linearMip, nearestBase;
    std::int32_t result;
    if (mipFilter == 3) {
        linearMip = minFilter == 2 ? 0x2703 : 0;
        nearestBase = 0x2701;
        result = minFilter != 1 ? linearMip : nearestBase;
    } else if (mipFilter == 2) {
        linearMip = minFilter == 2 ? 0x2702 : 0;
        nearestBase = 0x2700;
        result = minFilter != 1 ? linearMip : nearestBase;
    } else if (mipFilter == 1) {
        linearMip = minFilter == 2 ? 0x2601 : 0;
        nearestBase = 0x2600;
        result = minFilter != 1 ? linearMip : nearestBase;
    } else {
        result = 0;
    }
    if (result != static_cast<std::int32_t>(ts.tex.minFilter)) {
        g->TexParameteri(gl::TEXTURE_2D, 0x2801, result);
        ts.tex.minFilter = result;
    }
}

}  // namespace sgl

#include "render/DDSurface7.h"

#include <cstring>

#include "render/GLApi.h"
#include "render/GLState.h"
#include "render/MacOpenGL.h"

namespace sgl {

namespace {
AlignedBlock* NewBlock(std::uint32_t size) {
    auto* block = new AlignedBlock;
    if (size != 0) {
        block->size = size;
        block->raw = new std::uint8_t[static_cast<std::size_t>(size) + 0x20];
        block->aligned = reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(block->raw) + 0x1f) & ~std::uintptr_t{0x1f});
    }
    return block;
}
void DeleteBlock(AlignedBlock*& block) {
    if (block) {
        delete[] block->raw;
        delete block;
    }
    block = nullptr;
}
const _GUID kNoKey{};   // the private-data "key" is never looked at; the shim passes a zeroed one
}  // namespace

void CDirectDrawSurface7::AllocatePrivateData(std::uint32_t size) { privateData = NewBlock(size); }

void CDirectDrawSurface7::FreePrivateData() { DeleteBlock(privateData); }

std::uint32_t CDirectDrawSurface7::GetLevelSizeInBytes(std::uint32_t width, std::uint32_t height, const _DDPIXELFORMAT& pf) const {
    const std::uint32_t flags = pf.dwFlags;
    if ((flags & 4) == 0) {
        std::int32_t bits = 0;
        if ((flags & 2) || (flags & 0x400) || (flags & 0x40)) bits = static_cast<std::int32_t>(pf.dwRGBBitCount);
        if (width == 0) width = 1;
        if (height == 0) height = 1;
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(height * width) * bits) >> 3;
    }
    if ((pf.dwFourCC & 0xffffff) != 0x545844) return 0;   // "DXT"
    const std::uint32_t digit = pf.dwFourCC >> 24;
    std::int32_t blockBytes = 8;
    if (digit != 0x31) blockBytes = (digit - 0x31 < 5) ? 16 : 0;
    std::uint32_t blocksWide = (width + 3) >> 2;
    if (width == 0) blocksWide = 1;
    std::uint32_t blocksHigh = (height + 3) >> 2;
    if (height == 0) blocksHigh = 1;
    return blocksHigh * blocksWide * blockBytes;
}

std::uint32_t CDirectDrawSurface7::GetImageSizeInBytes() const {
    std::uint32_t height = desc.dwHeight, width = desc.dwWidth;
    std::uint32_t levels = 1;
    if (desc.dwFlags & 0x20000) levels = desc.dwMipMapCount;
    std::int32_t total = 0;
    if (levels != 0 && (width != 0 || height != 0)) {
        std::uint32_t level = 1;
        for (;;) {
            const std::uint32_t w = width == 0 ? 1 : width;
            const std::uint32_t h = height == 0 ? 1 : height;
            total += static_cast<std::int32_t>(GetLevelSizeInBytes(w, h, desc.ddpfPixelFormat));
            if (levels <= level) return static_cast<std::uint32_t>(total);
            width = w >> 1;
            height = h >> 1;
            ++level;
            if (((w | h) >> 1) == 0) break;
        }
    }
    return static_cast<std::uint32_t>(total);
}

CDirectDrawSurface7::CDirectDrawSurface7(const _DDSURFACEDESC2& d) {
    std::memcpy(&desc, &d, sizeof desc);
    const std::uint32_t caps = desc.ddsCaps.dwCaps;
    bool allocate = true;
    if ((caps & 0x2000) == 0) {
        if ((caps & 0x1000) == 0) {
            if ((caps & 0x40) == 0) {
                if (caps & 0x20000) kind = 6;
            } else {
                kind = 5;
            }
        } else {
            kind = 4;
            OpenGLTextureState ts{};
            GetDefaultOpenGLTexState(gl::TEXTURE_2D, &ts.tex);
            SetPrivateData(kNoKey, &ts, sizeof ts, 0);
        }
    } else {
        if (caps & 0x200) {
            kind = 1;
            allocate = false;
        } else if (caps & 0x20000) {
            kind = 2;
            allocate = false;
        } else if ((caps & 4) == 0) {
            allocate = false;
        } else {
            kind = 3;
        }
    }
    if (allocate) pixels = NewBlock(GetImageSizeInBytes());
    AddRef();
}

CDirectDrawSurface7::~CDirectDrawSurface7() {
    if (desc.ddsCaps.dwCaps & 0x1000) {   // a texture: give its GL objects back
        OpenGLTextureState ts{};
        GetDefaultOpenGLTexState(gl::TEXTURE_2D, &ts.tex);
        GetPrivateData(kNoKey, &ts, nullptr);
        if (ts.id != 0) {
            GL()->DeleteTextures(1, &ts.id);
            if (ts.pixelBuffer != 0) GL()->DeleteBuffers(1, &ts.pixelBuffer);
        }
    }
    DeleteBlock(pixels);
    DeleteBlock(privateData);
}

std::uint32_t CDirectDrawSurface7::QueryInterface(const _GUID&, void** out) {
    *out = this;
    AddRef();
    return 0;
}

std::int32_t CDirectDrawSurface7::Release() {
    const std::int32_t count = --refCount;
    if (count == 0) delete this;
    return count;
}

std::uint32_t CDirectDrawSurface7::Flip(IDirectDrawSurface7*, std::uint32_t) {
    MacOpenGL::Flip();
    return 0;
}

std::uint32_t CDirectDrawSurface7::GetAttachedSurface(_DDSCAPS2*, IDirectDrawSurface7** out) {
    *out = this;
    AddRef();
    return 0;
}

std::uint32_t CDirectDrawSurface7::GetPixelFormat(_DDPIXELFORMAT* out) {
    std::memcpy(out, &desc.ddpfPixelFormat, sizeof(_DDPIXELFORMAT));
    return 0;
}

std::uint32_t CDirectDrawSurface7::GetSurfaceDesc(_DDSURFACEDESC2* out) {
    std::memcpy(out, &desc, sizeof desc);
    return 0;
}

std::uint32_t CDirectDrawSurface7::SetPrivateData(const _GUID&, void* data, std::uint32_t size, std::uint32_t) {
    if (privateData) {
        if (size > privateData->size) {
            FreePrivateData();
            if (!privateData) AllocatePrivateData(size);
        }
    } else {
        AllocatePrivateData(size);
    }
    std::memcpy(privateData->aligned, data, size);
    return 0;
}

// The original copies the stored block out with no checks (a surface that never had private data would fault); here an
// empty slot answers DDERR_UNSUPPORTED instead.
std::uint32_t CDirectDrawSurface7::GetPrivateData(const _GUID&, void* data, std::uint32_t*) {
    if (!privateData || !privateData->aligned) return kDDErrUnsupported;
    std::memcpy(data, privateData->aligned, privateData->size);
    return 0;
}

std::uint32_t CDirectDrawSurface7::FreePrivateData(const _GUID&) {
    FreePrivateData();
    return 0;
}

std::uint32_t CDirectDrawSurface7::Unlock(tagRECT*) {
    lockLevel = 0;
    return 0;
}

// Hands out a pointer into the surface's pixel block (offset to the locked mip level and rectangle). A back buffer
// (kind 3) is first filled from the GL framebuffer with glReadPixels, then flipped top-to-bottom because GL rows run up.
std::uint32_t CDirectDrawSurface7::Lock(tagRECT* rect, _DDSURFACEDESC2* out, std::uint32_t, void*) {
    if (kind == 4) {   // a texture being locked will need re-uploading
        OpenGLTextureState ts{};
        GetDefaultOpenGLTexState(gl::TEXTURE_2D, &ts.tex);
        GetPrivateData(kNoKey, &ts, nullptr);
        ts.dirty = 1;
        SetPrivateData(kNoKey, &ts, sizeof ts, 0);
    }
    std::memcpy(out, &desc, sizeof desc);
    const std::uint32_t originalFlags = out->dwFlags;
    out->dwFlags = originalFlags | 0x800;

    std::uint32_t width = desc.dwWidth, height = desc.dwHeight;
    std::int32_t levelsLeft = lockLevel + 1;
    std::uint32_t levelOffset = 0;
    std::uint32_t levelWidth, levelHeight = height, levelBytes;
    for (;;) {
        levelWidth = width == 0 ? 1 : width;
        if (levelHeight == 0) levelHeight = 1;
        levelBytes = GetLevelSizeInBytes(levelWidth, levelHeight, desc.ddpfPixelFormat);
        if (--levelsLeft == 0) {
            width = levelWidth;
            break;
        }
        levelOffset += levelBytes;
        const std::uint32_t combined = levelWidth | levelHeight;
        width = levelWidth >> 1;
        levelHeight >>= 1;
        if ((combined & 0xfffffffe) == 0) break;
    }
    const std::uint32_t pfFlags = desc.ddpfPixelFormat.dwFlags;
    std::uint32_t inLevelOffset = 0;
    if ((pfFlags & 4) == 0) {
        out->dwFlags = originalFlags | 0x808;
        const std::uint32_t bits = (pfFlags & 0x442) ? desc.ddpfPixelFormat.dwRGBBitCount : 0;
        out->lPitch = static_cast<std::int32_t>(bits * levelWidth >> 3);
        if (rect) {
            std::uint32_t rowSkip = 0;
            std::uint32_t colBits = 0;
            if ((pfFlags & 2) || (pfFlags & 0x440)) {
                rowSkip = (bits * (levelWidth * static_cast<std::uint32_t>(rect->top))) >> 3;
                colBits = (bits * static_cast<std::uint32_t>(rect->left)) >> 3;
            }
            inLevelOffset = rowSkip + colBits;
        }
    } else {
        out->dwFlags = originalFlags | 0x80800;
        out->lPitch = static_cast<std::int32_t>(levelBytes);
    }
    out->lpSurface = pixels ? pixels->aligned + levelOffset + inLevelOffset : nullptr;   // (the original faults on kinds 1 and 2)

    if (kind == 3 && out->lpSurface) {
        IGLApi* g = GL();
        const MacOpenGL::OpenGLTextureFormat fmt = MacOpenGL::GetOpenGLFormat(desc.ddpfPixelFormat);
        std::int32_t x0, y0, right, bottom;
        if (rect) {
            x0 = rect->left; y0 = rect->top; right = rect->right; bottom = rect->bottom;
        } else {
            x0 = 0; y0 = 0; right = static_cast<std::int32_t>(width); bottom = static_cast<std::int32_t>(levelHeight);
        }
        g->PixelStorei(gl::PACK_ROW_LENGTH, static_cast<std::int32_t>(width));
        // Save every piece of GL state the read touches.
        static bool scissor, alpha, stencil, depth, blend, dither, lighting, fog;
        static std::uint8_t colorMask[4];
        static std::int32_t stencilWriteMask, depthWriteMask, depthFunc, activeUnit;
        static bool texture2D[2], textureCube[2];
        scissor = g->IsEnabled(gl::SCISSOR_TEST);
        alpha = g->IsEnabled(gl::ALPHA_TEST);
        stencil = g->IsEnabled(gl::STENCIL_TEST);
        depth = g->IsEnabled(gl::DEPTH_TEST);
        blend = g->IsEnabled(gl::BLEND);
        dither = g->IsEnabled(gl::DITHER);
        lighting = g->IsEnabled(gl::LIGHTING);
        fog = g->IsEnabled(gl::FOG);
        g->GetBooleanv(gl::COLOR_WRITEMASK, colorMask);
        g->ColorMask(1, 1, 1, 1);
        g->GetIntegerv(gl::STENCIL_WRITEMASK, &stencilWriteMask);
        g->StencilMask(0);
        g->GetIntegerv(gl::DEPTH_WRITEMASK, &depthWriteMask);
        g->GetIntegerv(gl::DEPTH_FUNC, &depthFunc);
        g->Disable(gl::SCISSOR_TEST);
        g->Disable(gl::ALPHA_TEST);
        g->Disable(gl::DEPTH_TEST);
        g->Disable(gl::BLEND);
        g->Disable(gl::DITHER);
        g->Disable(gl::LIGHTING);
        g->Disable(gl::FOG);
        g->GetIntegerv(gl::ACTIVE_TEXTURE, &activeUnit);
        for (int unit = 1; unit >= 0; --unit) {
            g->ActiveTexture(gl::TEXTURE0 + unit);
            texture2D[unit] = g->IsEnabled(gl::TEXTURE_2D);
            textureCube[unit] = g->IsEnabled(gl::TEXTURE_CUBE_MAP);
            if (texture2D[unit]) g->Disable(gl::TEXTURE_2D);
            if (textureCube[unit]) g->Disable(gl::TEXTURE_CUBE_MAP);
        }
        g->Disable(gl::STENCIL_TEST);
        g->DepthMask(0);
        const std::int32_t rowPixels = right - x0;
        const std::int32_t rows = bottom - y0;
        g->ReadPixels(x0, static_cast<std::int32_t>(levelHeight) - bottom, rowPixels, rows, fmt.format, fmt.type, out->lpSurface);
        g->DepthMask(static_cast<std::uint8_t>(depthWriteMask));
        g->ColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        g->StencilMask(static_cast<GLuint>(stencilWriteMask));
        g->DepthFunc(static_cast<GLenum>(depthFunc));
        auto restore = [&](bool on, GLenum cap) { if (on) g->Enable(cap); else g->Disable(cap); };
        restore(fog, gl::FOG);
        restore(lighting, gl::LIGHTING);
        restore(dither, gl::DITHER);
        restore(blend, gl::BLEND);
        restore(depth, gl::DEPTH_TEST);
        restore(stencil, gl::STENCIL_TEST);
        restore(alpha, gl::ALPHA_TEST);
        restore(scissor, gl::SCISSOR_TEST);
        for (int unit = 1; unit >= 0; --unit) {
            g->ActiveTexture(gl::TEXTURE0 + unit);
            if (texture2D[unit]) g->Enable(gl::TEXTURE_2D);
            if (textureCube[unit]) g->Enable(gl::TEXTURE_CUBE_MAP);
        }
        g->ActiveTexture(static_cast<GLenum>(activeUnit));
        g->PixelStorei(gl::PACK_ROW_LENGTH, 0);
        // flip the rows read from the (bottom-up) framebuffer
        const std::uint32_t rowBits = (pfFlags & 0x442) ? desc.ddpfPixelFormat.dwRGBBitCount : 0;
        const std::uint32_t rowBytes = (static_cast<std::uint32_t>(rowPixels) * rowBits) >> 3;
        std::uint8_t* temp = nullptr;
        std::uint8_t* tempAligned = nullptr;
        if (rowBytes != 0) {
            temp = new std::uint8_t[static_cast<std::size_t>(rowBytes) + 0x20];
            tempAligned = reinterpret_cast<std::uint8_t*>((reinterpret_cast<std::uintptr_t>(temp) + 0x1f) & ~std::uintptr_t{0x1f});
        }
        if (static_cast<std::uint32_t>(rows) + 1 >= 3) {
            auto* top = static_cast<std::uint8_t*>(out->lpSurface);
            const std::int64_t pitch = out->lPitch;
            auto* bottomRow = top + (static_cast<std::int64_t>(rows) - 1) * pitch;
            for (std::int32_t swaps = (rows + (rows >> 31)) >> 1; swaps != 0; --swaps) {
                std::memcpy(tempAligned, top, rowBytes);
                std::memcpy(top, bottomRow, rowBytes);
                std::memcpy(bottomRow, tempAligned, rowBytes);
                top += pitch;
                bottomRow -= pitch;
            }
        }
        delete[] temp;
    }
    return 0;
}

// Copies pixels between a surface's memory and the GL framebuffers. Which way depends on this surface's kind:
//   2 / 3 (depth / back buffer): the source surface's pixels are drawn into GL;  5 / 6 (front buffer / z-buffer): GL's
//   pixels are read back into this surface's memory.  Anything else does nothing. The transfer method (0..2, from the
//   "blt-method" preference) picks glCopyPixels between buffers (0, 1) or glDrawPixels / glReadPixels via memory (>= 2).
std::uint32_t CDirectDrawSurface7::Blt(tagRECT* dest, IDirectDrawSurface7* src, tagRECT* srcRect, std::uint32_t, _DDBLTFX*) {
    IGLApi* g = GL();
    const std::int32_t srcW = srcRect->right - srcRect->left;
    const std::int32_t srcH = srcRect->bottom - srcRect->top;
    const std::int32_t dstW = dest->right - dest->left;
    const std::int32_t dstH = dest->bottom - dest->top;
    std::int32_t viewport[4];
    g->GetIntegerv(gl::VIEWPORT, viewport);
    const std::int32_t windowY = viewport[3] - dest->bottom;
    g->PushAttrib(0x479c8);
    if (MacOpenGL::IsWindowPosSupported()) {
        g->WindowPos2i(dest->left, windowY);
    } else {
        g->MatrixMode(gl::PROJECTION);
        g->PushMatrix();
        g->LoadIdentity();
        g->Ortho(viewport[0], viewport[2], viewport[1], viewport[3], -1.0, 1.0);
        g->MatrixMode(gl::MODELVIEW);
        g->PushMatrix();
        g->LoadIdentity();
        g->Color4f(1.0f, 1.0f, 1.0f, 1.0f);
        g->RasterPos3f(static_cast<float>(dest->left), static_cast<float>(windowY), 1.0f);
        g->PopMatrix();
        g->MatrixMode(gl::PROJECTION);
        g->PopMatrix();
    }
    g->Color4f(1.0f, 1.0f, 1.0f, 1.0f);
    g->ActiveTexture(gl::TEXTURE0 + 1);
    g->Disable(gl::TEXTURE_2D);
    g->ActiveTexture(gl::TEXTURE0);
    g->Disable(gl::TEXTURE_2D);
    g->Disable(gl::LIGHTING);
    g->Disable(gl::FOG);
    g->Disable(gl::ALPHA_TEST);
    g->Disable(gl::DEPTH_TEST);
    g->Disable(gl::BLEND);
    g->Disable(gl::DITHER);
    MacOpenGL::GetOpenGLFormat(desc.ddpfPixelFormat);   // (the result is not used)
    g->PixelStorei(gl::UNPACK_ALIGNMENT, 8);
    g->PixelStorei(gl::PACK_ALIGNMENT, 8);

    _DDSURFACEDESC2 locked{};
    switch (kind) {
    case 2:
        locked.dwSize = 0x88;
        src->Lock(nullptr, &locked, 1, nullptr);
        g->PixelStorei(gl::UNPACK_ROW_LENGTH, static_cast<std::int32_t>(locked.dwWidth));
        g->PixelStorei(gl::PACK_ROW_LENGTH, static_cast<std::int32_t>(locked.dwWidth));
        g->ColorMask(0, 0, 0, 0);
        g->DepthFunc(0x207);
        g->DepthMask(1);
        if (MacOpenGL::GetFrameBufferBltTransferMethod() < 2) {
            g->ReadBuffer(0x409);
            g->DrawBuffer(0x405);
            if (MacOpenGL::GetFrameBufferBltTransferMethod() == 0) g->Disable(gl::DEPTH_TEST);
            else g->Enable(gl::DEPTH_TEST);
            g->CopyPixels(srcRect->left, static_cast<std::int32_t>(locked.dwHeight) - srcRect->bottom, srcW, srcH, 0x1801);
            g->ReadBuffer(0x405);
        } else {
            auto* base = static_cast<std::uint8_t*>(locked.lpSurface);
            g->Enable(gl::DEPTH_TEST);
            const std::uint32_t rowOffset = (locked.dwHeight - static_cast<std::uint32_t>(srcRect->bottom)) * static_cast<std::uint32_t>(locked.lPitch);
            g->DrawPixels(dstW, dstH, 0x1902, 0x1406, base + rowOffset + static_cast<std::int64_t>(srcRect->left) * 4);
        }
        g->PixelStorei(gl::UNPACK_ROW_LENGTH, 0);
        g->PixelStorei(gl::PACK_ROW_LENGTH, 0);
        src->Unlock(nullptr);
        break;
    case 3:
        locked.dwSize = 0x88;
        src->Lock(nullptr, &locked, 1, nullptr);
        g->PixelStorei(gl::UNPACK_ROW_LENGTH, static_cast<std::int32_t>(locked.dwWidth));
        g->PixelStorei(gl::PACK_ROW_LENGTH, static_cast<std::int32_t>(locked.dwWidth));
        g->DepthMask(0);
        if (MacOpenGL::GetFrameBufferBltTransferMethod() < 2) {
            g->ReadBuffer(0x409);
            g->DrawBuffer(0x405);
            g->CopyPixels(srcRect->left, static_cast<std::int32_t>(locked.dwHeight) - srcRect->bottom, srcW, srcH, 0x1800);
            g->ReadBuffer(0x405);
        } else {
            locked.dwSize = 0x88;
            src->Lock(nullptr, &locked, 1, nullptr);
            const std::uint32_t bytes = locked.ddpfPixelFormat.dwRGBBitCount >> 3;
            const std::uint32_t rowOffset = (locked.dwHeight - static_cast<std::uint32_t>(srcRect->bottom)) * static_cast<std::uint32_t>(locked.lPitch);
            auto* base = static_cast<std::uint8_t*>(locked.lpSurface);
            g->DrawPixels(dstW, dstH, 0x80e1, (bytes == 4 ? 1u : 0u) | 0x8366, base + (srcRect->left * bytes + rowOffset));
        }
        g->PixelStorei(gl::UNPACK_ROW_LENGTH, 0);
        g->PixelStorei(gl::PACK_ROW_LENGTH, 0);
        src->Unlock(nullptr);
        break;
    case 5:
        locked.dwSize = 0x88;
        Lock(nullptr, &locked, 1, nullptr);
        g->PixelStorei(gl::UNPACK_ROW_LENGTH, static_cast<std::int32_t>(locked.dwWidth));
        g->PixelStorei(gl::PACK_ROW_LENGTH, static_cast<std::int32_t>(locked.dwWidth));
        g->DepthMask(0);
        if (MacOpenGL::GetFrameBufferBltTransferMethod() < 2) {
            g->DrawBuffer(0x409);
            g->ReadBuffer(0x405);
            g->CopyPixels(srcRect->left, static_cast<std::int32_t>(locked.dwHeight) - srcRect->bottom, srcW, srcH, 0x1800);
            g->DrawBuffer(0x405);
        } else {
            const std::uint32_t bytes = locked.ddpfPixelFormat.dwRGBBitCount >> 3;
            g->PixelStorei(gl::PACK_ROW_LENGTH, static_cast<std::int32_t>(locked.dwWidth));
            const std::uint32_t rowOffset = (locked.dwHeight - static_cast<std::uint32_t>(dest->bottom)) * static_cast<std::uint32_t>(locked.lPitch);
            auto* base = static_cast<std::uint8_t*>(locked.lpSurface);
            g->ReadPixels(srcRect->left, static_cast<std::int32_t>(locked.dwHeight) - srcRect->bottom, srcW, srcH, 0x80e1,
                          (bytes == 4 ? 1u : 0u) | 0x8366, base + (dest->left * bytes + rowOffset));
        }
        g->PixelStorei(gl::UNPACK_ROW_LENGTH, 0);
        g->PixelStorei(gl::PACK_ROW_LENGTH, 0);
        Unlock(nullptr);
        break;
    case 6:
        locked.dwSize = 0x88;
        Lock(nullptr, &locked, 1, nullptr);
        g->PixelStorei(gl::UNPACK_ROW_LENGTH, static_cast<std::int32_t>(locked.dwWidth));
        g->PixelStorei(gl::PACK_ROW_LENGTH, static_cast<std::int32_t>(locked.dwWidth));
        g->ColorMask(0, 0, 0, 0);
        g->DepthFunc(0x207);
        g->DepthMask(1);
        if (MacOpenGL::GetFrameBufferBltTransferMethod() < 2) {
            g->ReadBuffer(0x405);
            g->DrawBuffer(0x409);
            if (MacOpenGL::GetFrameBufferBltTransferMethod() == 0) g->Disable(gl::DEPTH_TEST);
            else g->Enable(gl::DEPTH_TEST);
            g->CopyPixels(srcRect->left, static_cast<std::int32_t>(locked.dwHeight) - srcRect->bottom, srcW, srcH, 0x1801);
            g->DrawBuffer(0x405);
        } else {
            auto* base = static_cast<std::uint8_t*>(locked.lpSurface);
            g->Enable(gl::DEPTH_TEST);
            const std::uint32_t rowOffset = (locked.dwHeight - static_cast<std::uint32_t>(dest->bottom)) * static_cast<std::uint32_t>(locked.lPitch);
            g->ReadPixels(srcRect->left, static_cast<std::int32_t>(locked.dwHeight) - srcRect->bottom, srcW, srcH, 0x1902, 0x1406,
                          base + rowOffset + static_cast<std::int64_t>(dest->left) * 4);
        }
        g->PixelStorei(gl::UNPACK_ROW_LENGTH, 0);
        g->PixelStorei(gl::PACK_ROW_LENGTH, 0);
        Unlock(nullptr);
        break;
    default:
        break;
    }
    g->PopAttrib();
    g->PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    g->PixelStorei(gl::PACK_ALIGNMENT, 1);
    return 0;
}

}  // namespace sgl

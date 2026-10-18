#include "render/MacOpenGL.h"

#include <cstring>
#include <vector>

#include "render/D3DDevice7.h"
#include "render/DX7Structs.h"
#include "render/FastATIVertexProgram.h"
#include "render/GLApi.h"
#include "render/MacPreferences.h"
#include "rz/cRZTimer.h"

namespace sgl {

std::uint32_t MacOpenGL::dwVendorID = 0, MacOpenGL::dwDeviceID = 0;
bool MacOpenGL::bIsFastATICard = false, MacOpenGL::bIsTextureLODSupported = false,
     MacOpenGL::bIsVertexArraySupported = false, MacOpenGL::bIsWindowPosSupported = false;
std::uint32_t MacOpenGL::uiFrameBufferBltTransferMethod = 0;
OpaqueContextRef* MacOpenGL::sPrimaryContext = nullptr;

std::uint32_t MacOpenGL::OpenGLTextureFormat::GetExternalFormatPixelSizeInBytes() const {
    if (format == 0x1902) return 4;
    if (format == 0x80e1) {
        if (type - 0x8365u < 2) return 2;
        if (type == 0x1401 || type == 0x8367) return 4;
    }
    return 0;
}

std::uint32_t MacOpenGL::OpenGLTextureFormat::GetImageSizeInBytes(std::uint32_t width, std::uint32_t height) const {
    std::uint32_t pixelBytes, units;
    if ((internalFormat & 0xfffffffc) == 0x83f0) {   // S3TC: 4x4 blocks of 8 (DXT1) or 16 bytes
        pixelBytes = ((internalFormat | 1) != 0x83f1) * 8 + 8;
        units = ((height + 3) >> 2) * ((width + 3) >> 2);
    } else {
        units = height * width;
        pixelBytes = GetExternalFormatPixelSizeInBytes();
    }
    return units * pixelBytes;
}

void MacOpenGL::SetDeviceID(std::uint32_t a, std::uint32_t b, bool fastATI) {
    dwVendorID = a;
    dwDeviceID = b;
    bIsFastATICard = fastATI;
}

bool MacOpenGL::IsFastATICard() { return false; }
bool MacOpenGL::IsTextureLODSupported() { return bIsTextureLODSupported; }

bool MacOpenGL::IsVertexArraySupported() {
    static bool ran = false, useVertexArray = false;
    if (!ran) {
        ran = true;
        useVertexArray = MacPreferences::GetBoolean("vertex-array", false);
    }
    return useVertexArray && bIsVertexArraySupported;
}

bool MacOpenGL::IsWindowPosSupported() { return bIsWindowPosSupported; }
std::uint32_t MacOpenGL::GetFrameBufferBltTransferMethod() { return uiFrameBufferBltTransferMethod; }

MacOpenGL::OpenGLTextureFormat MacOpenGL::GetOpenGLFormat(const _DDPIXELFORMAT& pf) {
    static const std::uint32_t kInternal[5] = {0x83f1, 0x83f1, 0x83f2, 0x1908, 0x83f3};
    static const std::uint32_t kFormat[5] = {0, 0, 0, 0x1908, 0};
    static const std::uint32_t kType[5] = {0, 0, 0, 0x1401, 0};
    const std::uint32_t flags = pf.dwFlags;
    if ((flags & 4) == 0) {   // not a FourCC format
        if ((flags & 0x400) == 0) {
            const std::uint32_t bits = pf.dwRGBBitCount;
            if ((flags & 1) == 0) {
                if (bits == 0x20) return {0x8051, 0x80e1, 0x8367};
            } else {
                if (bits == 0x20) return {0x8058, 0x80e1, 0x8367};
                if (bits == 0x10) {
                    if (pf.dwRGBAlphaBitMask == 0xf000) return {0x8056, 0x80e1, 0x8365};
                    if (pf.dwRGBAlphaBitMask == 0x8000) return {0x8057, 0x80e1, 0x8366};
                }
            }
        } else if (pf.dwRGBBitCount < 0x21 && ((0x101010000ull >> pf.dwRGBBitCount) & 1)) {
            return {0x1902, 0x1902, 0x1406};   // depth buffers
        }
    } else {
        const std::uint32_t biased = pf.dwFourCC + 0xceaba7bc;
        const std::uint32_t index = (biased << 8) | (biased >> 24);
        if (index < 5) return {kInternal[index], kFormat[index], kType[index]};
    }
    return {0x1908, 0x1908, 0x1401};
}

void MacOpenGL::Flip() { MacDisplay::SwapContext(sPrimaryContext); }

void MacOpenGL::ShutdownOpenGL(IDirect3DDevice7*& device, IDirectDrawSurface7*& primary, IDirectDrawSurface7*& zbuffer,
                               IDirectDrawSurface7*& back) {
    if (device) {
        device->Release();
        device = nullptr;
    }
    if (back) {
        back->Release();
        back = nullptr;
    }
    if (primary) {
        primary->Release();
        primary = nullptr;
    }
    if (zbuffer) {
        zbuffer->Release();
        zbuffer = nullptr;
    }
    if (sPrimaryContext) MacDisplay::ReleaseContext(sPrimaryContext);
}

namespace {
bool Contains(const char* haystack, const char* needle) { return std::strstr(haystack, needle) != nullptr; }
}  // namespace

std::uint32_t MacOpenGL::InitializeOpenGL(int, std::uint32_t width, std::uint32_t height, std::uint32_t depth,
                                          std::uint32_t display, IDirectDraw7* dd, IDirect3DDevice7*& deviceOut,
                                          IDirectDrawSurface7*& primaryOut, IDirectDrawSurface7*& zbufferOut,
                                          IDirectDrawSurface7*& backOut) {
    if (sPrimaryContext) MacDisplay::ReleaseContext(sPrimaryContext);
    MacDisplay::SetMode(static_cast<int>(width), static_cast<int>(height), static_cast<int>(depth));
    std::uint8_t fastFlag = 0;
    sPrimaryContext = MacDisplay::CreateScreenContext(static_cast<int>(display), fastFlag);
    if (!sPrimaryContext) return 0x80000008;
    MacDisplay::SetCurrentContext(sPrimaryContext);
    DisplayPlatform()->EnableMultithreadedGL(true);
    IGLApi* g = GL();
    const char* extensions = g->GetString(gl::EXTENSIONS);
    bIsTextureLODSupported = Contains(extensions, "GL_SGIS_texture_lod");
    if (!Contains(extensions, "GL_APPLE_vertex_array_object") || !Contains(extensions, "GL_APPLE_fence"))
        bIsVertexArraySupported = false;
    else
        bIsVertexArraySupported = Contains(extensions, "GL_APPLE_element_array");
    bIsWindowPosSupported = Contains(extensions, "GL_ARB_window_pos");

    if (fastFlag == 0) {
        uiFrameBufferBltTransferMethod = 2;
    } else {
        // Time three ways of moving a depth image (copy between buffers with depth test off / on, or draw from memory)
        // and keep the fastest.
        g->PushAttrib(0x479c8);
        if (!bIsWindowPosSupported) {
            std::int32_t vp[4];
            g->GetIntegerv(gl::VIEWPORT, vp);
            g->MatrixMode(gl::PROJECTION);
            g->PushMatrix();
            g->LoadIdentity();
            g->Ortho(vp[0], vp[2], vp[1], vp[3], -1.0, 1.0);
            g->MatrixMode(gl::MODELVIEW);
            g->PushMatrix();
            g->LoadIdentity();
            g->Color4f(1.0f, 1.0f, 1.0f, 1.0f);
            g->RasterPos3f(0.0f, 0.0f, 1.0f);
            g->PopMatrix();
            g->MatrixMode(gl::PROJECTION);
            g->PopMatrix();
        } else {
            g->WindowPos2i(0, 0);
        }
        g->Color4f(1.0f, 1.0f, 1.0f, 1.0f);
        g->ActiveTexture(gl::TEXTURE0 + 1);
        g->Disable(gl::TEXTURE_2D);
        g->ActiveTexture(gl::TEXTURE0);
        g->Disable(gl::TEXTURE_2D);
        g->Disable(gl::LIGHTING);
        g->Disable(gl::FOG);
        g->Disable(gl::SCISSOR_TEST);
        g->Disable(gl::ALPHA_TEST);
        g->Disable(gl::DEPTH_TEST);
        g->Disable(gl::BLEND);
        g->Disable(gl::DITHER);
        g->PixelStorei(gl::UNPACK_ALIGNMENT, 8);
        g->PixelStorei(gl::PACK_ALIGNMENT, 8);
        const std::uint32_t bytes = width * height * 4;
        std::vector<std::uint8_t> scratch(bytes + 0x20);
        std::uint8_t* pixels = scratch.data();
        std::memset(pixels, 0, bytes);
        g->Enable(gl::DEPTH_TEST);
        g->DrawPixels(width, height, 0x1902, 0x1406, pixels);
        g->Finish();
        std::int32_t best[3];
        for (std::uint32_t method = 0; method < 3; ++method) {
            best[method] = 0x7fffffff;
            std::int32_t fastest = 0x7fffffff;
            for (int run = 0; run < 10; ++run) {
                g->Finish();
                cRZTimer timer(cRZTimer::kMicroseconds);
                timer.Start();
                g->PixelStorei(gl::UNPACK_ROW_LENGTH, width);
                g->PixelStorei(gl::PACK_ROW_LENGTH, width);
                g->ColorMask(0, 0, 0, 0);
                g->DepthFunc(0x207);
                g->DepthMask(1);
                if (method < 2) {
                    g->ReadBuffer(0x409);
                    g->DrawBuffer(0x405);
                    if (method == 0) g->Disable(gl::DEPTH_TEST);
                    else g->Enable(gl::DEPTH_TEST);
                    g->CopyPixels(0, 0, width, height, 0x1801);
                    g->ReadBuffer(0x405);
                } else {
                    g->Enable(gl::DEPTH_TEST);
                    g->DrawPixels(width, height, 0x1902, 0x1406, pixels);
                }
                g->Finish();
                const std::int32_t elapsed = static_cast<std::int32_t>(timer.GetElapsedTimeU32());
                if (elapsed < fastest) {
                    best[method] = elapsed;
                    fastest = elapsed;
                }
            }
        }
        g->PopAttrib();
        uiFrameBufferBltTransferMethod = 0;
        std::uint32_t winner = 0;
        for (std::uint32_t method = 1; method < 3; ++method) {
            if (best[method] < best[winner]) {
                uiFrameBufferBltTransferMethod = method;
                winner = method;
            }
        }
    }
    // The measured choice is then replaced by the preference (default -1, which reads as "draw from memory").
    {
        uiFrameBufferBltTransferMethod = static_cast<std::uint32_t>(MacPreferences::GetInteger("blt-method", -1));
    }
    std::uint32_t videoA = 0, videoB = 0;
    MacDisplay::GetVideoMemoryInfo(videoA, videoB);

    const std::uint32_t pitch32 = depth * width >> 3;
    // Colour masks: 16-bit surfaces are 5551, deeper ones are RGBA8888.
    const bool sixteen = depth == 0x10;
    const std::uint32_t maskR = sixteen ? 0x7c00 : 0xff, maskG = sixteen ? 0x3e0 : 0xff00, maskB = sixteen ? 0x1f : 0xff0000,
                        maskA = sixteen ? 0x8000 : 0xff000000;
    auto describe = [&](std::uint32_t caps, std::uint32_t pitch, std::uint32_t backBuffers, _DDPIXELFORMAT pf) {
        _DDSURFACEDESC2 d{};
        d.dwSize = 0x88;
        d.dwFlags = 0x100f;
        d.dwHeight = height;
        d.dwWidth = width;
        d.lPitch = static_cast<std::int32_t>(pitch);
        d.dwBackBufferCount = backBuffers;
        d.ddpfPixelFormat = pf;
        d.ddsCaps.dwCaps = caps;
        return d;
    };
    _DDPIXELFORMAT colorFormat{0x20, 0x41, 0, depth, maskR, maskG, maskB, maskA};
    _DDSURFACEDESC2 d = describe(0x2200, pitch32, 0, colorFormat);
    dd->CreateSurface(&d, &primaryOut, nullptr);
    _DDPIXELFORMAT zFormat{0x20, 0x400, 0, 0x20, 0, 0xffffffff, 0, 0};
    d = describe(0x22000, (width * 4) & 0x1ffffffc, 0, zFormat);
    dd->CreateSurface(&d, &zbufferOut, nullptr);
    d = describe(0x2004, pitch32, 1, colorFormat);
    dd->CreateSurface(&d, &backOut, nullptr);

    _D3DDeviceDesc7 desc{};
    auto put32 = [&](std::size_t off, std::uint32_t v) { std::memcpy(desc.bytes + off, &v, 4); };
    put32(0, 0x90000);
    put32(0x44, 0x80);
    put32(0x5c, 1);
    put32(0x74, depth);
    put32(0x78, 0x20);
    put32(0xac, 0);
    put32(0xb8, 0x20002);
    put32(0xe0, 4);

    std::vector<_DDPIXELFORMAT> formats;
    formats.push_back({0x20, 0x41, 0, 0x10, 0xf00, 0xf0, 0xf, 0xf000});
    formats.push_back({0x20, 0x41, 0, 0x10, 0x7c00, 0x3e0, 0x1f, 0x8000});
    if (depth > 0x10) {
        formats.push_back({0x20, 0x40, 0, 0x20, 0xff0000, 0xff00, 0xff, 0});
        formats.push_back({0x20, 0x41, 0, 0x20, 0xff0000, 0xff00, 0xff, 0xff000000});
    }
    if (Contains(extensions, "GL_ARB_texture_compression")) {
        for (std::uint32_t fourcc : {0x31545844u, 0x32545844u, 0x33545844u, 0x35545844u})
            formats.push_back({0x20, 0x4, fourcc, 0, 0, 0, 0, 0});
    }
    deviceOut = new CDirect3DDevice7(backOut, zbufferOut, &desc, &formats);
    return 0;
}

}  // namespace sgl

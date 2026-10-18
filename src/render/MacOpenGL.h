// MacOpenGL (0x10063cd70..0x10063dba2, plus OpenGLTextureFormat at 0x100638f58 / 0x10063cdf8): capability flags read
// from the GL extension string, the pixel-format table that maps DirectDraw formats to GL formats, and the start-up
// and shut-down of the whole DX7 shim (a display context, the three surfaces and the device).
#pragma once
#include <cstdint>

#include "render/DX7Types.h"
#include "render/MacDisplay.h"

namespace sgl {

struct MacOpenGL {
    // A DirectDraw pixel format as GL sees it: internal format, external format, component type.
    struct OpenGLTextureFormat {
        std::uint32_t internalFormat, format, type;
        std::uint32_t GetImageSizeInBytes(std::uint32_t width, std::uint32_t height) const;   // @ 0x100638f58
        std::uint32_t GetExternalFormatPixelSizeInBytes() const;                                // @ 0x10063cdf8
    };

    static void SetDeviceID(std::uint32_t a, std::uint32_t b, bool fastATI);   // @ 0x10063cd70
    static bool IsFastATICard();                                                // @ 0x10063cd8b (always false)
    static bool IsTextureLODSupported();                                        // @ 0x10063cd93
    static bool IsVertexArraySupported();                                       // @ 0x10063cd9f
    static bool IsWindowPosSupported();                                         // @ 0x10063cddf
    static std::uint32_t GetFrameBufferBltTransferMethod();                     // @ 0x10063cdeb
    // Brings GL up on a new context and builds the primary / depth / back surfaces and the device; returns 0 on success
    // (0x80000008 when no context can be created).
    static std::uint32_t InitializeOpenGL(int unused, std::uint32_t width, std::uint32_t height, std::uint32_t depth,
                                          std::uint32_t display, IDirectDraw7* dd, IDirect3DDevice7*& device,
                                          IDirectDrawSurface7*& primary, IDirectDrawSurface7*& zbuffer,
                                          IDirectDrawSurface7*& back);        // @ 0x10063ce47
    static void ShutdownOpenGL(IDirect3DDevice7*& device, IDirectDrawSurface7*& primary, IDirectDrawSurface7*& zbuffer,
                               IDirectDrawSurface7*& back);                   // @ 0x10063da1b
    static OpenGLTextureFormat GetOpenGLFormat(const _DDPIXELFORMAT& pf);      // @ 0x10063daaf
    static void Flip();                                                         // @ 0x10063dba2

    static std::uint32_t dwVendorID, dwDeviceID;
    static bool bIsFastATICard, bIsTextureLODSupported, bIsVertexArraySupported, bIsWindowPosSupported;
    static std::uint32_t uiFrameBufferBltTransferMethod;
    static OpaqueContextRef* sPrimaryContext;
};

}  // namespace sgl

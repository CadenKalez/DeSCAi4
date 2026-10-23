// cIGZFontRenderer — the interface cGZFont draws its glyph runs through (iid 0xa8bc2a75).
// Vtable (cGZBufferFontRenderer 0x100a91a90, cGZDrawFontRenderer 0x100addc40): [0..2] cIGZUnknown, [3] Init, [4] Shutdown,
// [5] DrawBegin, [6] DrawEnd, [7] DrawChars. The two renderers add their own members after that.
#pragma once
#include <cstdint>

#include "gz/StreamOps.h"  // cRZRect
#include "gz/cIGZUnknown.h"

class cIGZFont;

constexpr std::uint32_t kGZIID_cIGZFontRenderer = 0xa8bc2a75;

class cIGZFontRenderer : public cIGZUnknown {
public:
    // How the coverage bytes of a glyph bitmap are turned into pixels. cGZFont maps any antialiasing mode that is not
    // 1..3 to kMono before it calls the renderer (0x1002bef08).
    enum eRenderingMode : std::int32_t {
        kMono = 1,      // coverage bit 7 set -> the foreground colour is written
        kPalette = 2,   // the coverage byte indexes a colour table
        kAlpha = 3,     // the coverage byte is an alpha: the foreground colour is blended in
    };

    // One glyph of a run (0x28 bytes). `pixels` points at the glyph's top-left coverage byte inside the font's atlas.
    struct sGlyphInfo {
        std::int32_t left = 0, top = 0, right = 0, bottom = 0;   // destination rectangle on the surface
        const std::uint8_t* pixels = nullptr;                    // +0x10
        std::uint32_t pitch = 0;                                 // +0x18 atlas row pitch
        std::uint32_t width = 0, height = 0;                     // +0x1c, +0x20
        std::uint32_t character = 0;                             // +0x24 UTF-16 code (DrawChars skips codes below 0x20)
    };

    virtual bool Init(cIGZFont* font) = 0;                                                       // [3]
    virtual bool Shutdown(cIGZFont* font) = 0;                                                   // [4]
    virtual bool DrawBegin(cIGZFont* font, void* surface, eRenderingMode mode) = 0;              // [5]
    virtual bool DrawEnd(cIGZFont* font, void* surface, eRenderingMode mode) = 0;                // [6]
    // `colors`: for kPalette the colour table, otherwise a pointer to the one foreground colour.
    virtual bool DrawChars(cIGZFont* font, void* surface, sGlyphInfo* glyphs, std::uint32_t count, eRenderingMode mode,
                           const std::uint32_t* colors, const cRZRect* clip) = 0;                 // [7]
};
static_assert(sizeof(cIGZFontRenderer::sGlyphInfo) == 0x28);

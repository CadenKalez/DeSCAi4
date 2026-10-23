// cGZDrawFontRenderer — the font renderer that draws through a draw context (class vtable 0x100addc40, 16 functions at
// 0x1006572ce..0x1006585a8). Targets that cannot be locked as plain pixels (hardware surfaces) get their glyphs from a
// glyph texture atlas: every glyph is copied once into the 256x256 ARGB4444 atlas (as white with the coverage in the
// alpha bits) and blitted from there, modulated by the foreground colour. Targets that can be locked are handed to an
// inner cGZBufferFontRenderer.
//
// Object layout of the original (0x480 bytes): +8 refcount, +0xc / +0x10 atlas size, +0x18 atlas buffer, +0x20 / +0x24
// pen of the next free spot, +0x28 height of the current row, +0x30 per-font glyph position maps, +0x50 map load factor,
// +0x58 the current font's map, +0x60 coverage -> texel table (256 x u32), +0x460 inner buffer renderer, +0x468 draw
// context, +0x470 direct (inner renderer) mode, +0x474 / +0x478 origin of the context in direct mode.
#pragma once
#include <cstdint>
#include <unordered_map>

#include "gz/Buffer.h"
#include "gz/DrawContext.h"
#include "gz/FontRenderer.h"

// The two class instances the constructor and CreateTexturePages ask the framework for, and the colour conversion.
class IDrawFontRendererHost {
public:
    virtual ~IDrawFontRendererHost() = default;
    virtual cIGZFontRenderer* CreateBufferFontRenderer() = 0;   // GZCOM() vslot 0x18 (clsid 0x08cda30b, iid 0xa8bc2a75): counted reference
    virtual cIGZBuffer* CreateBuffer() = 0;                     // GZCOM() vslot 0x18 (clsid 0xc470d325, iid 0x20732180): counted reference
    virtual void NativeColorToRGB(std::uint32_t native, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) = 0;   // RZ::GS() vslot 0x120
};

class cGZDrawFontRenderer : public cIGZFontRenderer {
public:
    struct cCachedGlyphImgInfo {   // where a glyph sits in the atlas
        std::int32_t x = 0, y = 0;
    };

    explicit cGZDrawFontRenderer(IDrawFontRendererHost* host);   // 0x1006572ce
    ~cGZDrawFontRenderer();                                      // 0x1006573fc / 0x10065749c / 0x1006574a6

    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x1006574d4
    std::uint32_t AddRef() override;                               // 0x1006574f6
    std::uint32_t Release() override;                              // 0x100657504 (deletes at zero)
    bool Init(cIGZFont* font) override;                            // 0x10065751e
    bool Shutdown(cIGZFont* font) override;                        // 0x100657526
    bool DrawBegin(cIGZFont* font, void* surface, eRenderingMode mode) override;   // 0x10065754a
    bool DrawEnd(cIGZFont* font, void* surface, eRenderingMode mode) override;     // 0x100657b0c
    bool DrawChars(cIGZFont* font, void* surface, sGlyphInfo* glyphs, std::uint32_t count, eRenderingMode mode,
                   const std::uint32_t* colors, const cRZRect* clip) override;      // 0x100657b92

    virtual bool PrepChar(std::uint32_t character, const std::uint8_t* bitmap, const cRZRect* rect, std::uint32_t flags);   // 0x1006585a0: returns true
    virtual void SetGlyphCacheDimensionsHint(std::uint32_t width, std::uint32_t height);   // 0x1006585a8

    void CreateTexturePages();   // 0x10065791e
    // Copies the glyph into the atlas and records where; null when it does not fit and `flushIfFull` is false.
    const cCachedGlyphImgInfo* CreateCachedGlyphImg(cIGZFont* font, const sGlyphInfo& glyph, bool flushIfFull);   // 0x100658052

    // --- inspection (tests) ---
    cIGZBuffer* Atlas() const { return atlas_; }
    std::uint32_t PenX() const { return penX_; }
    std::uint32_t PenY() const { return penY_; }
    std::uint32_t RowHeight() const { return rowHeight_; }
    std::size_t CachedGlyphCount(cIGZFont* font) const;
    bool DirectMode() const { return direct_; }
    const std::uint32_t* CoverageTable() const { return coverageTable_; }
    std::int32_t OriginX() const { return originX_; }

private:
    using GlyphMap = std::unordered_map<std::uint32_t, cCachedGlyphImgInfo>;
    IDrawFontRendererHost* host_;
    std::uint32_t refCount_ = 0;                       // +8
    std::uint32_t atlasWidth_ = 0x100, atlasHeight_ = 0x100;   // +0xc, +0x10
    cIGZBuffer* atlas_ = nullptr;                      // +0x18
    std::uint32_t penX_ = 0, penY_ = 0, rowHeight_ = 0;   // +0x20, +0x24, +0x28
    std::unordered_map<cIGZFont*, GlyphMap> fontMaps_;    // +0x30
    GlyphMap* currentMap_ = nullptr;                   // +0x58
    std::uint32_t coverageTable_[256] = {};            // +0x60
    cIGZFontRenderer* bufferRenderer_ = nullptr;       // +0x460
    gz::cIGZDrawContext* context_ = nullptr;           // +0x468
    bool direct_ = false;                              // +0x470
    std::int32_t originX_ = 0, originY_ = 0;           // +0x474, +0x478
};

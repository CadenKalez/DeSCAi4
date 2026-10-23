// cGZBufferFontRenderer — the software font renderer: blits glyph coverage bitmaps straight into a cIGZBuffer
// (class vtable 0x100a91a90, 11 functions at 0x1002ba254..0x1002bab59).
//
// Object layout (0x10 bytes): +0 vptr, +8 reference count. There is no virtual destructor slot: Release() deletes the object
// itself once the count would drop to zero (or was never taken).
//
// The 16-bit alpha path blends through fontBlendTable5Bit (built once by PrepAlpha): for every pair of 5-bit channel levels
// (foreground, background) and 32 coverage steps it holds floor(((32 - a) * background + a * foreground) / 32).
#pragma once
#include <cstdint>

#include "gz/FontRenderer.h"

class cIGZBuffer;

class cGZBufferFontRenderer final : public cIGZFontRenderer {
public:
    cGZBufferFontRenderer();                                        // 0x1002ba3ea
    ~cGZBufferFontRenderer() = default;

    // --- cIGZUnknown ---
    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x1002ba442: iids 1 and 0xa8bc2a75
    std::uint32_t AddRef() override;                               // 0x1002ba418
    std::uint32_t Release() override;                              // 0x1002ba426

    // --- cIGZFontRenderer ---
    bool Init(cIGZFont* font) override;                            // 0x1002ba464: always true
    bool Shutdown(cIGZFont* font) override;                        // 0x1002ba46c: always true
    bool DrawBegin(cIGZFont* font, void* surface, eRenderingMode mode) override;   // 0x1002ba474: surface->Lock(0x8080)
    bool DrawEnd(cIGZFont* font, void* surface, eRenderingMode mode) override;     // 0x1002ba48a: surface->Unlock(0x8080)
    bool DrawChars(cIGZFont* font, void* surface, sGlyphInfo* glyphs, std::uint32_t count, eRenderingMode mode,
                   const std::uint32_t* colors, const cRZRect* clip) override;      // 0x1002baadc

    // One glyph (0x1002ba4a0): `dest` is the glyph rectangle on the surface, `source` the (x, y) start inside the
    // coverage bitmap `pixels` (row pitch `pitch`), `clip` an optional extra clip. Always returns true.
    bool DrawChar(cIGZFont* font, void* surface, const cRZRect* dest, const std::uint8_t* pixels, const cRZRect* source,
                  std::uint32_t pitch, eRenderingMode mode, const std::uint32_t* colors, const cRZRect* clip);

    // 0x1002ba254: fills fontBlendTable5Bit.
    static bool PrepAlpha();
    static bool IsAlphaInitialized() { return alphaInitialized_; }
    // fontBlendTable5Bit[foreground][background][alpha] (32 x 32 x 32 bytes, 0x100b800f0).
    static const std::uint8_t* BlendTable() { return &blendTable_[0][0][0]; }

private:
    std::uint32_t refCount_ = 0;   // +8
    static bool alphaInitialized_;
    static std::uint8_t blendTable_[32][32][32];
};

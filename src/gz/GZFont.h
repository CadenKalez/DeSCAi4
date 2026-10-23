// cGZFont — the GZ font: one FreeType face rendered into a glyph atlas, plus the text measuring, wrapping and drawing
// that the window controls use (class vtable 0x100a91ae0, 77 functions at 0x1002bab66..0x1002bf816; the cIGZFont
// interface itself is in gz/Font.h).
//
// Object layout of the original (0x250 bytes): +8 initialised, +0xc refcount, +0x10 file path, +0x38 face name,
// +0x60 type (1 proportional, 2 fixed width), +0x64 size, +0x68 pixel height, +0x6c style bits (0x3c editable),
// +0x70/0x74 colours, +0x78 antialiasing mode (1 mono, 2 palette, 3 alpha; default 1), +0x7c palette mode (default 2),
// +0x80 gamma (1.25), +0x84 sharpness, +0x88 gamma lookup (0 or 256 bytes), +0xa0/0xa4 glyph scale, +0xa8 advance scale,
// +0xac/0xb0 spacing, +0xb4 code page of the text passed in (0 ASCII, 8 UTF-8, 0x10 UTF-16), +0xb8 surface lock count,
// +0xbc ascent, +0xc0 line gap, +0xc8 renderer, +0xd0 five text conversion buffers, +0x148 Thai substitution buffer,
// +0x160 substitution index map, +0x178 default character (0x20), +0x17c its glyph index, +0x180 face cell,
// +0x188 glyph run, +0x1a0 current palette, +0x1a8 palette cache, +0x1c0 / +0x208 the mono / gray glyph atlases.
//
// The glyph atlas (CachedBitmapData, 0x48 bytes) is one growing block of coverage bytes with two spare rows above and
// below; every glyph is a column strip appended at `cursor`, described by a CachedBitmapDataDetail.
#pragma once
#include <cstdint>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

#include "gz/Font.h"
#include "gz/FontRenderer.h"
#include "gz/FontServices.h"
#include "rz/cRZString.h"

constexpr std::uint32_t kGZIID_cIGZFont = 0x10673960;

// FT_GZFont_GetStyleFlags @ 0x1002b9fd0: GZ style bits (1 bold, 2 italic) from the FreeType style flags and style name.
std::uint32_t FontStyleFlagsFromFace(std::uint32_t freeTypeStyleFlags, const std::string& styleName);

class cGZFont : public cIGZFont {
public:
    // One cached glyph (node + 0x14 of the original hash table, 0x18 bytes). x0..x1 / y0..y1 delimit the glyph inside the
    // atlas; xOffset / yOffset position it relative to the pen and baseline.
    struct CachedBitmapDataDetail {
        std::int32_t x0 = 0;          // +0x00
        std::int32_t y0 = 0;          // +0x04
        std::int32_t x1 = 0;          // +0x08 (exclusive)
        std::int32_t y1 = 0;          // +0x0c (exclusive: the glyph height when y0 is 0)
        std::int16_t xOffset = 0;     // +0x10 left bearing
        std::int16_t yOffset = 0;     // +0x12 ascent minus bitmap top
        std::uint16_t advance = 0;    // +0x14
        std::uint16_t padding = 0;    // +0x16
    };

    // 0x48 bytes: +0 allocation, +8 first pixel row, +0x10 width, +0x14 height, +0x18 pitch, +0x1c cursor, +0x20 glyph map.
    struct CachedBitmapData {
        std::unique_ptr<std::uint8_t[]> allocation;   // +0 (height + 4 rows of `width` bytes)
        std::uint8_t* pixels = nullptr;               // +8 (allocation + 2 * width)
        std::uint32_t width = 0, height = 0, pitch = 0;
        std::uint32_t cursor = 0;
        std::unordered_map<std::uint32_t, CachedBitmapDataDetail> glyphs;
        void Clear();                                 // 0x1002bab66
    };

    explicit cGZFont(IFontHost* host);                // 0x1002bab9c
    virtual ~cGZFont();                                       // 0x1002bae80 / 0x1002bb07e / 0x1002bb088

    // --- cIGZUnknown ---
    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x1002bb0b6: iids 1 and 0x10673960
    std::uint32_t AddRef() override;                               // 0x1002bb0d8
    std::uint32_t Release() override;                              // 0x1002bb0e6

    // --- cIGZFont ---
    bool Init() override;                                          // 0x1002bb12a
    bool Shutdown() override;                                      // 0x1002bb9a2
    void GetName(cIGZString& out) const override;                  // 0x1002bf76a
    std::uint32_t GetStyle() const override;                       // 0x1002bf794
    bool GetStyle(int style) const override;                       // 0x1002bf79e
    bool SetStyle(int style, bool on) override;                    // 0x1002bba38
    std::uint32_t GetType() const override;                        // 0x1002bf7ae
    std::uint32_t GetSize() const override;                        // 0x1002bf7b8
    bool SetSize(std::uint32_t size) override;                     // 0x1002bd7d2
    std::uint32_t GetFontColor() const override;                   // 0x1002bf7c2
    std::uint32_t GetFontBackgroundColor() const override;         // 0x1002bf7cc
    void SetFontColor(std::uint32_t color, std::uint32_t background) override;   // 0x1002bf7d6
    int GetAntialiasing() override;                                // 0x1002bf7e2
    void SetAntialiasing(int mode) override;                       // 0x1002bd984
    float GetAntialiasingGammaCorrection() override;               // 0x1002bd992
    void SetAntialiasingGammaCorrection(float gamma) override;     // 0x1002bd9a0
    float GetAntialiasingSharpness() override;                     // 0x1002bd9d6
    void SetAntialiasingSharpness(float sharpness) override;       // 0x1002bd9e4
    void GetFilePath(cIGZString& out) const override;              // 0x1002bf7ec
    void SetFilePath(const cIGZString& path) override;             // 0x1002bf816
    bool GetCodePage(int& codePage) const override;                // 0x1002bda1c
    bool SetCodePage(int codePage) override;                       // 0x1002bda2c
    bool IsCodePageAvailable(int codePage) override;               // 0x1002bdb46
    bool IsCharEncodingAvailable(int encoding) override;           // 0x1002bda3a
    bool IsCharAvailable(void* chars, std::uint32_t length) override;   // 0x1002bda96
    bool PreloadChars(void* chars, std::uint32_t length) override;      // 0x1002bdf70
    void GetScaling(float& x, float& y) const override;            // 0x1002bdfe8
    void SetScaling(float x, float y) override;                    // 0x1002bdffe
    void GetAdvanceScaling(float& x, float& y) const override;     // 0x1002be0c2
    void SetAdvanceScaling(float x, float y) override;             // 0x1002be0d6
    void GetSpacingAdjustment(int& x, int& y) const override;      // 0x1002be136
    void SetSpacingAdjustment(int x, int y) override;              // 0x1002be14c
    int GetLineHeight() override;                                  // 0x1002be2d2
    int CharWidth(const void* chars, std::uint32_t length) override;    // 0x1002be16e
    int CharHeight(const void* chars, std::uint32_t length) override;   // 0x1002be2e2
    int GetSizeAboveBaseline() override;                           // 0x1002be2ec
    int GetSizeBelowBaseline() override;                           // 0x1002be2fe
    int GetAdvanceWidth(const void* chars, std::uint32_t length) override;   // 0x1002be378
    std::uint32_t CalculateWordsToFitInWidth(const void* text, std::uint32_t length, std::uint32_t width,
                                             std::uint32_t* fitted, WordWrapMode mode) override;   // 0x1002be3ca
    std::uint32_t CalculateCharsToFitInWidth(const void* text, std::uint32_t length, std::uint32_t width,
                                             std::uint32_t* fitted) override;   // 0x1002be6d8
    std::uint32_t CalculateNumberOfLines(const void* text, std::uint32_t length, std::uint32_t width) override;   // 0x1002be7a6
    std::uint32_t CalculateWidthOfLines(const void* text, std::uint32_t length, std::uint32_t width) override;    // 0x1002be848
    bool CalculateTextArea(const void* text, std::uint32_t length, cRZRect& area, bool a, bool b) override;       // 0x1002be8dc
    bool CalculateTextArea(const void* text, std::uint32_t length, cRZRect& area, int a, int b, int c) override;  // 0x1002bea0c
    bool TextArea(const void* text, std::uint32_t length, cRZRect& area) override;             // 0x1002be9b2
    int GetStringWidth(const void* text, std::uint32_t length) override;                       // 0x1002bebea
    int GetStringVisibleWidth(const void* text, std::uint32_t length, std::uint32_t& visible) override;   // 0x1002beca8
    bool FormatForEllipsis(cIGZString& out, const cIGZString& in, const cRZRect& area, bool a) override;  // 0x1002bed8e
    bool DrawTextLine(void* surface, int x, int y, const void* text, std::uint32_t length, std::uint32_t color,
                      std::uint32_t background, const cRZRect* clip, cRZPoint* end) override;   // 0x1002bef08
    bool DrawTextParagraph(void* surface, const cRZRect& area, const void* text, std::uint32_t length, int alignment,
                           std::uint32_t color, std::uint32_t background, const cRZRect* clip, cRZPoint* end) override;   // 0x1002bf308
    cIGZFontRenderer* GetRenderer() const override;                // 0x1002bf654
    void SetRenderer(cIGZFontRenderer* renderer) override;         // 0x1002bf662
    bool LockSurfaceForFontUse(void* surface) override;            // 0x1002bf6c4
    bool UnlockSurfaceForFontUse(void* surface) override;          // 0x1002bf716

    // --- cGZFont's own virtual helpers (vtable slots 59..64) ---
    virtual int GetXOffset(const void* chars, std::uint32_t length);                       // 0x1002be32c
    virtual CachedBitmapData* GetAppropriateCachedBitmapData();                            // 0x1002bc1d6
    virtual CachedBitmapDataDetail* GetCharData(std::uint32_t character);                  // 0x1002bc1f2
    virtual CachedBitmapDataDetail* AddCharData(std::uint32_t character, CachedBitmapData* cache);   // 0x1002bc24c
    // Copies a w x h glyph bitmap (mode 2: 1 bit per pixel, 0x100: 8 bit) to (x, y) of the atlas, applies the gamma
    // lookup and the underline / strike-through styles, and fixes `detail` up.
    virtual bool CopyGlyphToSourceData(CachedBitmapData* cache, CachedBitmapDataDetail* detail, int x, int y,
                                       const std::uint8_t* source, std::uint32_t w, std::uint32_t h, std::uint32_t pitch,
                                       std::uint32_t mode);                                // 0x1002bcc2c
    virtual bool ReallocateSourceData(CachedBitmapData* cache, std::uint32_t width, std::uint32_t height);   // 0x1002bcaf2

    // --- text conversion helpers (non virtual) ---
    void GetTextAsUnicode(const void* text, std::uint32_t length, const std::uint16_t*& out, std::uint32_t& outLength,
                          int buffer, bool recordMapping);                                  // 0x1002bba50
    bool IsSubstitutableCharPresent(const std::uint16_t* text, std::uint32_t length);       // 0x1002bbba6
    std::uint32_t ConvertStringToUnicode(const void* text, std::uint32_t length, std::vector<std::uint16_t>& out);   // 0x1002bbbfe
    void MakeSubstitutedString(std::uint16_t* text, std::uint32_t length, const std::uint16_t*& out,
                               std::uint32_t& outLength, bool recordMapping);               // 0x1002bbc7c
    std::uint32_t ConvertCharToUnicode(const void* text, std::uint32_t length, std::uint32_t* consumed);   // 0x1002be1bc
    static std::uint32_t GetSubstitutionCharCount(std::uint16_t character);                 // 0x1002bd004
    static std::uint32_t GetSubstitutionChar(std::uint16_t character, std::uint32_t variant);   // 0x1002bd056
    void SetCurrentPalette(std::uint32_t foreground, std::uint32_t background);             // 0x1002bd554
    void BuildAntialiasingLookupArray();                                                    // 0x1002bb7c4

    std::uint32_t FormatForEllipsisLength(cIGZString& text, const cIGZString& ellipsis, const cRZRect& area, bool skip);   // 0x1002bed8e (returns the original length)

    // --- inspection (tests) ---
    int PixelHeight() const { return pixelHeight_; }
    int Ascent() const { return ascent_; }
    int LineGap() const { return lineGap_; }
    const std::vector<std::uint8_t>& AntialiasingLookup() const { return lookup_; }
    const std::uint32_t* CurrentPalette() const { return currentPalette_; }
    std::size_t PaletteCacheSize() const { return paletteCache_.size(); }
    int SurfaceLockCount() const { return lockCount_; }
    const std::vector<std::uint32_t>& SubstitutionMap() const { return substitutionMap_; }

private:
    static constexpr std::size_t kPaletteEntries = 256;   // the original allocates 128 and indexes up to 255 (see GZFont.cpp)
    static int NormalisedMode(int antialiasing) { return (antialiasing >= 1 && antialiasing <= 3) ? antialiasing : 1; }
    bool InitialiseFace();
    bool StoreGlyph(CachedBitmapData* cache, std::uint32_t character, const CachedBitmapDataDetail& detail);

    IFontHost* host_;
    bool initialized_ = false;                 // +8
    std::uint32_t refCount_ = 0;               // +0xc
    cRZString filePath_;                       // +0x10
    cRZString name_;                           // +0x38
    std::uint32_t type_ = 0;                   // +0x60
    std::uint32_t size_ = 0x10;                // +0x64
    int pixelHeight_ = 0x10;                   // +0x68
    std::uint32_t style_ = 0;                  // +0x6c
    std::uint32_t color_ = 0;                  // +0x70
    std::uint32_t backgroundColor_ = 0;        // +0x74
    int antialiasing_ = 1;                     // +0x78
    int paletteMode_ = 2;                      // +0x7c
    float gamma_ = 1.25f;                      // +0x80
    float sharpness_ = 0.0f;                   // +0x84
    std::vector<std::uint8_t> lookup_;         // +0x88
    float scaleX_ = 1.0f, scaleY_ = 1.0f;      // +0xa0, +0xa4
    float advanceScale_ = 1.0f;                // +0xa8
    int spacingX_ = 0, spacingY_ = 0;          // +0xac, +0xb0
    int codePage_ = 0;                         // +0xb4
    int lockCount_ = 0;                        // +0xb8
    int ascent_ = 0;                           // +0xbc
    int lineGap_ = 0;                          // +0xc0
    cIGZFontRenderer* renderer_ = nullptr;     // +0xc8
    std::vector<std::uint16_t> textBuffers_[5];   // +0xd0
    std::vector<std::uint16_t> substitutionBuffer_;   // +0x148
    std::vector<std::uint32_t> substitutionMap_;      // +0x160
    std::uint32_t defaultChar_ = 0x20;         // +0x178
    std::uint32_t defaultGlyph_ = 0;           // +0x17c
    std::unique_ptr<IFontFace> face_;          // +0x180 (cell holding the FT_Face)
    std::vector<cIGZFontRenderer::sGlyphInfo> glyphRun_;   // +0x188
    std::uint32_t* currentPalette_ = nullptr;  // +0x1a0
    std::list<std::unique_ptr<std::uint32_t[]>> paletteCache_;   // +0x1a8 (most recently used first)
    CachedBitmapData monoCache_;               // +0x1c0
    CachedBitmapData grayCache_;               // +0x208
};

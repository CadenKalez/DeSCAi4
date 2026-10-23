// Seams of cGZFont (src/gz/GZFont.*): everything the font reaches outside itself.
//
//  * IFontRasterizer / IFontFace / IFontGlyph stand for the FreeType library the game links in (the build is a 2.0-era
//    FreeType with 32-bit FT_Long, so the structure offsets read by cGZFont are the ones documented on FontFaceMetrics).
//    Each method mirrors exactly one FreeType call; the real adapter wraps FT_Library / FT_Face / FT_Glyph.
//  * IFontHost is the rest: file lookups (cRZFile statics), the language manager (RZ::LM()) and the graphics system
//    (RZ::GS()) colour conversions.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "gz/cIGZString.h"

// FT_MulFix (0x1008f5da0): (a * b + 0x8000) >> 16 on the magnitudes, the sign restored afterwards.
inline std::int32_t FontMulFix(std::int32_t a, std::int32_t b) {
    std::int64_t sign = 1;
    std::int64_t x = a, y = b;
    if (x < 0) { x = -x; sign = -sign; }
    if (y < 0) { y = -y; sign = -sign; }
    const std::int64_t product = (x * y + 0x8000) >> 16;
    return static_cast<std::int32_t>(sign < 0 ? -product : product);
}

// The fields of FT_FaceRec / FT_SizeRec cGZFont reads (offsets in the game's FreeType build).
struct FontFaceMetrics {
    std::int32_t bboxXMin = 0, bboxYMin = 0, bboxXMax = 0, bboxYMax = 0;   // face +0x58..+0x64 (font units, or 26.6 for bitmap fonts)
    std::int16_t ascender = 0;       // face +0x6a
    std::int16_t descender = 0;      // face +0x6c
    std::int16_t height = 0;         // face +0x6e
    std::int16_t maxAdvanceWidth = 0; // face +0x70
    std::int32_t xScale = 0;         // face->size->metrics +0x1c (16.16)
    std::int32_t yScale = 0;         // face->size->metrics +0x20 (16.16)
};

constexpr std::uint32_t kFontFaceFlagFixedWidth = 4;   // face_flags bit 2 (face +8)
constexpr std::uint32_t kFontStyleFlagItalic = 1;      // style_flags bit 0 (face +0xc)
constexpr std::uint32_t kFontStyleFlagBold = 2;        // style_flags bit 1

// The bitmap part of an FT_BitmapGlyph.
struct FontBitmap {
    std::int32_t left = 0;           // +0x20
    std::int32_t top = 0;            // +0x24
    std::uint32_t rows = 0;          // +0x28
    std::uint32_t width = 0;         // +0x2c
    std::int32_t pitch = 0;          // +0x30
    const std::uint8_t* buffer = nullptr;   // +0x38 (8 bit coverage, or 1 bit per pixel when rendered mono)
    std::uint16_t numGrays = 0;      // +0x40 (0 for mono, 256 for gray; any other value is refused by cGZFont)
};

class IFontGlyph {
public:
    virtual ~IFontGlyph() = default;                       // FT_Done_Glyph (0x1008f96e0)
    // glyph->format == 'bits' (0x62697473): already a bitmap glyph.
    virtual bool IsBitmap() const = 0;
    // FT_Glyph_To_Bitmap(&glyph, mono ? 1 : 0, 0): true when it succeeded; the glyph is then a bitmap glyph.
    virtual bool ConvertToBitmap(bool mono) = 0;           // 0x1008f9870
    virtual std::int32_t AdvanceX() const = 0;             // glyph +0x14 (16.16)
    virtual const FontBitmap& Bitmap() const = 0;
};

class IFontFace {
public:
    virtual ~IFontFace() = default;                        // FT_Done_Face (0x1008f71e0)
    virtual bool AttachFile(const std::string& utf8Path) = 0;       // FT_Attach_File (true = FT_Err_Ok)
    virtual bool SelectCharmap(std::uint32_t encodingTag) = 0;      // FT_Select_Charmap (true = FT_Err_Ok)
    // FT_Set_Transform(face, &matrix, 0) with the 16.16 matrix {xx, xy, yx, yy}.
    virtual void SetTransform(const std::int32_t matrix[4]) = 0;
    virtual std::uint32_t GetCharIndex(std::uint32_t character) = 0;    // FT_Get_Char_Index (0 = no glyph)
    virtual bool SetPixelSizes(std::uint32_t width, std::uint32_t height) = 0;   // FT_Set_Pixel_Sizes (true = FT_Err_Ok)
    // FT_Load_Glyph(face, index, 0) followed by FT_Get_Glyph(face->glyph): null when either failed.
    virtual std::unique_ptr<IFontGlyph> LoadGlyph(std::uint32_t glyphIndex) = 0;
    virtual FontFaceMetrics& Metrics() = 0;
    virtual std::uint32_t FaceFlags() const = 0;           // face +8
    virtual std::uint32_t StyleFlags() const = 0;          // face +0xc
    virtual std::string FamilyName() const = 0;            // face +0x18 (empty when null)
    virtual std::string StyleName() const = 0;             // face +0x20 (empty when null)
};

class IFontRasterizer {
public:
    virtual ~IFontRasterizer() = default;
    // FT_New_Face(DAT_100b800e8, path, 0, &face): null on failure.
    virtual std::unique_ptr<IFontFace> NewFace(const std::string& utf8Path) = 0;
};

class IFontHost {
public:
    virtual ~IFontHost() = default;
    virtual IFontRasterizer* Rasterizer() = 0;
    virtual bool FileExists(const cIGZString& path) = 0;                                  // cRZFile::FileExists
    virtual bool GetPathExtension(const cIGZString& path, cIGZString& extension) = 0;    // cRZFile::GetPathExtension
    virtual std::uint32_t GetCurrentLanguage() = 0;                                       // RZ::LM() vslot 0x38 (0x13 = Thai)
    virtual void NativeColorToRGB(std::uint32_t native, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) = 0;   // RZ::GS() vslot 0x120
    virtual std::uint32_t RGBToNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;                    // RZ::GS() vslot 0x110
};

constexpr std::uint32_t kLanguageThai = 0x13;

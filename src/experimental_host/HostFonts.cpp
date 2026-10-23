// EARLY TEST / EXPERIMENTAL HOST — not recovered game code. See README_EARLY_TEST.md.
#include "experimental_host/HostFonts.h"

#include <sys/stat.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H

namespace exphost {
namespace {

class FtGlyph final : public IFontGlyph {
public:
    explicit FtGlyph(FT_Glyph g) : glyph_(g) {}
    ~FtGlyph() override { if (glyph_) FT_Done_Glyph(glyph_); }
    bool IsBitmap() const override { return glyph_->format == FT_GLYPH_FORMAT_BITMAP; }
    bool ConvertToBitmap(bool mono) override {
        return FT_Glyph_To_Bitmap(&glyph_, mono ? FT_RENDER_MODE_MONO : FT_RENDER_MODE_NORMAL, nullptr, 1) == 0;
    }
    std::int32_t AdvanceX() const override { return std::int32_t(glyph_->advance.x); }
    const FontBitmap& Bitmap() const override {
        auto* b = reinterpret_cast<FT_BitmapGlyph>(glyph_);
        bitmap_.left = b->left; bitmap_.top = b->top;
        bitmap_.rows = b->bitmap.rows; bitmap_.width = b->bitmap.width; bitmap_.pitch = b->bitmap.pitch;
        bitmap_.buffer = b->bitmap.buffer;
        bitmap_.numGrays = b->bitmap.pixel_mode == FT_PIXEL_MODE_MONO ? 0 : b->bitmap.num_grays;
        return bitmap_;
    }
private:
    FT_Glyph glyph_;
    mutable FontBitmap bitmap_;
};

class FtFace final : public IFontFace {
public:
    explicit FtFace(FT_Face f) : face_(f) {}
    ~FtFace() override { FT_Done_Face(face_); }
    bool AttachFile(const std::string& p) override { return FT_Attach_File(face_, p.c_str()) == 0; }
    bool SelectCharmap(std::uint32_t tag) override { return FT_Select_Charmap(face_, FT_Encoding(tag)) == 0; }
    void SetTransform(const std::int32_t m[4]) override {
        FT_Matrix matrix{m[0], m[1], m[2], m[3]};
        FT_Set_Transform(face_, &matrix, nullptr);
    }
    std::uint32_t GetCharIndex(std::uint32_t c) override { return FT_Get_Char_Index(face_, c); }
    bool SetPixelSizes(std::uint32_t w, std::uint32_t h) override { return FT_Set_Pixel_Sizes(face_, w, h) == 0; }
    std::unique_ptr<IFontGlyph> LoadGlyph(std::uint32_t index) override {
        if (FT_Load_Glyph(face_, index, 0) != 0) return nullptr;
        FT_Glyph g;
        if (FT_Get_Glyph(face_->glyph, &g) != 0) return nullptr;
        return std::make_unique<FtGlyph>(g);
    }
    FontFaceMetrics& Metrics() override {   // re-read each time: the scales change with SetPixelSizes
        m_.bboxXMin = std::int32_t(face_->bbox.xMin); m_.bboxYMin = std::int32_t(face_->bbox.yMin);
        m_.bboxXMax = std::int32_t(face_->bbox.xMax); m_.bboxYMax = std::int32_t(face_->bbox.yMax);
        m_.ascender = face_->ascender; m_.descender = face_->descender;
        m_.height = face_->height; m_.maxAdvanceWidth = face_->max_advance_width;
        m_.xScale = std::int32_t(face_->size->metrics.x_scale); m_.yScale = std::int32_t(face_->size->metrics.y_scale);
        return m_;
    }
    std::uint32_t FaceFlags() const override { return std::uint32_t(face_->face_flags); }
    std::uint32_t StyleFlags() const override { return std::uint32_t(face_->style_flags); }
    std::string FamilyName() const override { return face_->family_name ? face_->family_name : ""; }
    std::string StyleName() const override { return face_->style_name ? face_->style_name : ""; }
private:
    FT_Face face_;
    FontFaceMetrics m_;
};

}  // namespace

FreeTypeFontHost::FreeTypeFontHost() {
    FT_Library lib = nullptr;
    if (FT_Init_FreeType(&lib) == 0) library_ = lib;
}
FreeTypeFontHost::~FreeTypeFontHost() { if (library_) FT_Done_FreeType(static_cast<FT_Library>(library_)); }

std::unique_ptr<IFontFace> FreeTypeFontHost::NewFace(const std::string& path) {
    FT_Face face = nullptr;
    if (!library_ || FT_New_Face(static_cast<FT_Library>(library_), path.c_str(), 0, &face) != 0) return nullptr;
    return std::make_unique<FtFace>(face);
}

bool FreeTypeFontHost::FileExists(const cIGZString& path) {
    struct stat st;
    return ::stat(path.ToChar(), &st) == 0;
}

bool FreeTypeFontHost::GetPathExtension(const cIGZString& path, cIGZString& extension) {
    const std::string s = path.ToChar();
    const auto dot = s.rfind('.');
    if (dot == std::string::npos) return false;
    extension.AssignFromCString(s.substr(dot).c_str());
    return true;
}

}  // namespace exphost

// EARLY TEST / EXPERIMENTAL HOST — not recovered game code. See README_EARLY_TEST.md.
// The FreeType adapter behind the recovered cGZFont's seams (IFontHost / IFontRasterizer / IFontFace / IFontGlyph).
// The game links its own 2.0-era FreeType; this wraps the system one (Homebrew), one call per seam method.
#pragma once
#include <memory>
#include <string>

#include "gz/FontServices.h"

namespace exphost {

class FreeTypeFontHost final : public IFontHost, public IFontRasterizer {
public:
    FreeTypeFontHost();
    ~FreeTypeFontHost() override;
    bool Ok() const { return library_ != nullptr; }

    IFontRasterizer* Rasterizer() override { return this; }
    std::unique_ptr<IFontFace> NewFace(const std::string& utf8Path) override;
    bool FileExists(const cIGZString& path) override;
    bool GetPathExtension(const cIGZString& path, cIGZString& extension) override;
    std::uint32_t GetCurrentLanguage() override { return 9; }   // English
    // XRGB8888, the host's display format
    void NativeColorToRGB(std::uint32_t n, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override { r = n >> 16; g = n >> 8; b = n; }
    std::uint32_t RGBToNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { return std::uint32_t(r) << 16 | std::uint32_t(g) << 8 | b; }

private:
    void* library_ = nullptr;   // FT_Library
};

}  // namespace exphost

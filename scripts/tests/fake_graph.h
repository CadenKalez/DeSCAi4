// Shared fakes for the graph tests: a font that records its draws and a host that hands out fake fonts.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "gz/Graph.h"

namespace graphfakes {
using namespace gz;
struct Font final : cIGZFont {
    int refs = 1;
    struct Call { cRZRect rect; std::string text; int align; std::uint32_t color; };
    std::vector<Call> calls;
    struct Glyph { int x, y; char ch; std::uint32_t color; };
    std::vector<Glyph> glyphs;                  // DrawTextLine calls (symbols)
    std::uint32_t fontColor = 0x1234, setColorCalls = 0;
    int locks = 0, unlocks = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init() override { return true; } bool Shutdown() override { return true; }
    void GetName(cIGZString&) const override {} std::uint32_t GetStyle() const override { return 0; }
    bool GetStyle(int) const override { return false; } bool SetStyle(int, bool) override { return true; }
    std::uint32_t GetType() const override { return 0; } std::uint32_t GetSize() const override { return 0; }
    bool SetSize(std::uint32_t) override { return true; } std::uint32_t GetFontColor() const override { return fontColor; }
    std::uint32_t GetFontBackgroundColor() const override { return 0; } void SetFontColor(std::uint32_t color, std::uint32_t) override { fontColor = color; ++setColorCalls; }
    int GetAntialiasing() override { return 0; } void SetAntialiasing(int) override {}
    float GetAntialiasingGammaCorrection() override { return 0; } void SetAntialiasingGammaCorrection(float) override {}
    float GetAntialiasingSharpness() override { return 0; } void SetAntialiasingSharpness(float) override {}
    void GetFilePath(cIGZString&) const override {} void SetFilePath(const cIGZString&) override {}
    bool GetCodePage(int&) const override { return false; } bool SetCodePage(int) override { return false; }
    bool IsCodePageAvailable(int) override { return false; } bool IsCharEncodingAvailable(int) override { return false; }
    bool IsCharAvailable(void*, std::uint32_t) override { return false; } bool PreloadChars(void*, std::uint32_t) override { return false; }
    void GetScaling(float&, float&) const override {} void SetScaling(float, float) override {}
    void GetAdvanceScaling(float&, float&) const override {} void SetAdvanceScaling(float, float) override {}
    void GetSpacingAdjustment(int&, int&) const override {} void SetSpacingAdjustment(int, int) override {}
    int GetLineHeight() override { return 10; } int CharWidth(const void*, std::uint32_t) override { return 6; }
    int CharHeight(const void*, std::uint32_t) override { return 10; }
    int GetSizeAboveBaseline() override { return 8; } int GetSizeBelowBaseline() override { return 2; }
    int GetAdvanceWidth(const void*, std::uint32_t) override { return 6; }
    std::uint32_t CalculateWordsToFitInWidth(const void*, std::uint32_t, std::uint32_t, std::uint32_t*, WordWrapMode) override { return 0; }
    std::uint32_t CalculateCharsToFitInWidth(const void*, std::uint32_t, std::uint32_t, std::uint32_t*) override { return 0; }
    std::uint32_t CalculateNumberOfLines(const void*, std::uint32_t, std::uint32_t) override { return 1; }
    std::uint32_t CalculateWidthOfLines(const void*, std::uint32_t, std::uint32_t) override { return 0; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, bool, bool) override { return false; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, int, int, int) override { return false; }
    bool TextArea(const void*, std::uint32_t, cRZRect&) override { return false; }
    int GetStringWidth(const void*, std::uint32_t length) override { return static_cast<int>(length) * 6; }
    int GetStringVisibleWidth(const void*, std::uint32_t, std::uint32_t&) override { return 0; }
    bool FormatForEllipsis(cIGZString&, const cIGZString&, const cRZRect&, bool) override { return false; }
    bool DrawTextLine(void*, int x, int y, const void* text, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, cRZPoint*) override {
        glyphs.push_back({x, y, *static_cast<const char*>(text), fontColor});
        return true;
    }
    bool DrawTextParagraph(void* surface, const cRZRect& area, const void* text, std::uint32_t length, int alignment,
                           std::uint32_t color, std::uint32_t, const cRZRect*, cRZPoint*) override {
        calls.push_back({area, std::string(static_cast<const char*>(text), length), alignment, color});
        // mark the top-left pixel of the rectangle so rotated copies can be located
        static_cast<cIGZBuffer*>(surface)->SetPixel(static_cast<std::uint32_t>(area.left), static_cast<std::uint32_t>(area.top), 0x1234);
        return true;
    }
    cIGZFontRenderer* GetRenderer() const override { return nullptr; } void SetRenderer(cIGZFontRenderer*) override {}
    bool LockSurfaceForFontUse(void*) override { ++locks; return true; } bool UnlockSurfaceForFontUse(void*) override { ++unlocks; return true; }
};

struct Host final : IGraphHost {
    std::vector<std::string> faces; Font* last = nullptr; std::vector<std::unique_ptr<Font>> fonts;
    bool arialMissing = false; cIGZBuffer* window = nullptr;
    cIGZFont* CreateFont(const char* face, std::uint32_t) override {
        faces.push_back(face);
        if (arialMissing && std::string(face) == "arial") return nullptr;
        fonts.push_back(std::make_unique<Font>());
        last = fonts.back().get();
        return last;
    }
    bool FontsAvailable() override { return true; }
    cIGZBuffer* CreateBuffer() override { auto* b = new cGZBuffer(); b->AddRef(); return b; }
    std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }
    cIGZBuffer* WindowBuffer(IWinBuffer*) override { return window; }
};

}  // namespace graphfakes

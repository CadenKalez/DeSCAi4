// Fakes for the file browser test: a counted buffer and a font that measures one pixel per character per unit.
#pragma once
#include <algorithm>
#include "gz/Buffer.h"
#include "gz/Font.h"

struct FakeCountedBuffer final : cIGZBuffer {
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init(std::uint32_t, std::uint32_t, cGZBufferColorType) override { return {}; }
    bool Shutdown() override { return {}; }
    bool Uninitialize() override { return {}; }
    bool Lock(eLockFlags) override { return {}; }
    bool Unlock(eLockFlags) override { return {}; }
    bool IsLocked() const override { return {}; }
    std::uint32_t Width() const override { return {}; }
    std::uint32_t Height() const override { return {}; }
    void GetBufferArea(cRZRect&) const override {}
    const cRZRect* GetBufferArea() const override { return nullptr; }
    std::uint32_t ConvertRGBValueToNative(std::uint8_t, std::uint8_t, std::uint8_t) override { return {}; }
    std::uint32_t GetMemoryUsage() const override { return {}; }
    cGZBufferColorType GetColorType() const override { return {}; }
    void GetColorType(cGZBufferColorType&) const override {  }
    std::uint32_t GetBitsPerPixel() const override { return {}; }
    std::uint32_t GetBytesPerPixel() const override { return {}; }
    bool Fill(std::uint32_t, const cRZRect&, const cRZRect*) override { return {}; }
    bool Fill(std::uint32_t, int, int, int, int, const cRZRect*) override { return {}; }
    bool Fill(std::uint32_t, const cRZRect*) override { return {}; }
    std::uint32_t GetPixel(std::uint32_t, std::uint32_t) const override { return {}; }
    void SetPixel(std::uint32_t, std::uint32_t, std::uint32_t) override {  }
    void SetTransparency(std::uint32_t) override {  }
    bool GetTransparency() const override { return {}; }
    bool IsPixelTransparent(std::uint32_t, std::uint32_t) override { return {}; }
    bool GetTransparentColor(std::uint32_t&) const override { return {}; }
    void RemoveTransparency() override {  }
    bool DrawLine(int, int, int, int, int, std::uint32_t) override { return {}; }
    bool Blt(cIGZBuffer*, const cRZRect&, const cRZRect&, const cRZRect*) override { return {}; }
    bool ConvertNativeValueToRGB(std::uint32_t, std::uint8_t&, std::uint8_t&, std::uint8_t&) override { return {}; }
    std::uint32_t ConvertRGBAValueToNative(std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) override { return {}; }
    bool ConvertNativeValueToRGBA(std::uint32_t, std::uint8_t&, std::uint8_t&, std::uint8_t&, std::uint8_t&) override { return {}; }
    void* GetColorSurfaceBits() override { return {}; }
    std::uint32_t GetColorSurfaceStride() override { return {}; }
    std::uint32_t GetChangeCounter() override { return {}; }
    cIGZBufferHardwareCache* GetHardwareCache() override { return {}; }
    bool SetHardwareCache(cIGZBufferHardwareCache*) override { return {}; }
    bool IsReady() override { return {}; }
};

struct FakeCharFont final : cIGZFont {
    int refs = 0;
    int charWidth = 10;    // every character is this wide
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init() override { return {}; }
    bool Shutdown() override { return {}; }
    void GetName(cIGZString&) const override {  }
    std::uint32_t GetStyle() const override { return {}; }
    bool GetStyle(int) const override { return {}; }
    bool SetStyle(int, bool) override { return {}; }
    std::uint32_t GetType() const override { return {}; }
    std::uint32_t GetSize() const override { return {}; }
    bool SetSize(std::uint32_t) override { return {}; }
    std::uint32_t GetFontColor() const override { return {}; }
    std::uint32_t GetFontBackgroundColor() const override { return {}; }
    void SetFontColor(std::uint32_t, std::uint32_t) override {  }
    int GetAntialiasing() override { return {}; }
    void SetAntialiasing(int) override {  }
    float GetAntialiasingGammaCorrection() override { return {}; }
    void SetAntialiasingGammaCorrection(float) override {  }
    float GetAntialiasingSharpness() override { return {}; }
    void SetAntialiasingSharpness(float) override {  }
    void GetFilePath(cIGZString&) const override {  }
    void SetFilePath(const cIGZString&) override {  }
    bool GetCodePage(int&) const override { return {}; }
    bool SetCodePage(int) override { return {}; }
    bool IsCodePageAvailable(int) override { return {}; }
    bool IsCharEncodingAvailable(int) override { return {}; }
    bool IsCharAvailable(void*, std::uint32_t) override { return {}; }
    bool PreloadChars(void*, std::uint32_t) override { return {}; }
    void GetScaling(float&, float&) const override {  }
    void SetScaling(float, float) override {  }
    void GetAdvanceScaling(float&, float&) const override {  }
    void SetAdvanceScaling(float, float) override {  }
    void GetSpacingAdjustment(int&, int&) const override {  }
    void SetSpacingAdjustment(int, int) override {  }
    int GetLineHeight() override { return {}; }
    int CharWidth(const void*, std::uint32_t) override { return {}; }
    int CharHeight(const void*, std::uint32_t) override { return {}; }
    int GetSizeAboveBaseline() override { return {}; }
    int GetSizeBelowBaseline() override { return {}; }
    int GetAdvanceWidth(const void*, std::uint32_t) override { return {}; }
    std::uint32_t CalculateNumberOfLines(const void*, std::uint32_t, std::uint32_t) override { return {}; }
    std::uint32_t CalculateWidthOfLines(const void*, std::uint32_t, std::uint32_t) override { return {}; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, bool, bool) override { return {}; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, int, int, int) override { return {}; }
    bool TextArea(const void*, std::uint32_t length, cRZRect& area) override { area = {0, 0, int(length) * charWidth, 10}; return true; }
    std::uint32_t CalculateWordsToFitInWidth(const void*, std::uint32_t length, std::uint32_t width, std::uint32_t* fitted, WordWrapMode) override { std::uint32_t n = std::min<std::uint32_t>(length, width / charWidth); if (fitted) *fitted = n; return n; }
    std::uint32_t CalculateCharsToFitInWidth(const void*, std::uint32_t length, std::uint32_t width, std::uint32_t* fitted) override { std::uint32_t n = std::min<std::uint32_t>(length, width / charWidth); if (fitted) *fitted = n; return n; }
    cIGZFontRenderer* GetRenderer() const override { return nullptr; }
    void SetRenderer(cIGZFontRenderer*) override {}
    bool LockSurfaceForFontUse(void*) override { return true; }
    bool UnlockSurfaceForFontUse(void*) override { return true; }
    bool DrawTextLine(void*, int, int, const void*, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, cRZPoint*) override { return true; }
    bool DrawTextParagraph(void*, const cRZRect&, const void*, std::uint32_t, int, std::uint32_t, std::uint32_t, const cRZRect*, cRZPoint*) override { return true; }
    int GetStringWidth(const void*, std::uint32_t) override { return {}; }
    int GetStringVisibleWidth(const void*, std::uint32_t, std::uint32_t&) override { return {}; }
    bool FormatForEllipsis(cIGZString&, const cIGZString&, const cRZRect&, bool) override { return {}; }
};
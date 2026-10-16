// cIGZFont — the font interface (iid unknown here; slots from cGZFont's vtable at 0x100a91ae0, slot n at 8n).
// Text is passed as raw bytes plus a length; drawing targets are cIGZBuffer pointers passed as void*.
#pragma once
#include <cstdint>

#include "gz/StreamOps.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZPoint.h"

class cIGZFontRenderer;

class cIGZFont : public cIGZUnknown {
public:
    enum WordWrapMode : std::int32_t { kWrapWord = 0, kWrapChar = 1 };

    virtual bool Init() = 0;                                                        // [3]
    virtual bool Shutdown() = 0;                                                    // [4]
    virtual void GetName(cIGZString& out) const = 0;                                // [5]
    virtual std::uint32_t GetStyle() const = 0;                                     // [6]
    virtual bool GetStyle(int style) const = 0;                                     // [7]
    virtual bool SetStyle(int style, bool on) = 0;                                  // [8]
    virtual std::uint32_t GetType() const = 0;                                      // [9]
    virtual std::uint32_t GetSize() const = 0;                                      // [10]
    virtual bool SetSize(std::uint32_t size) = 0;                                   // [11]
    virtual std::uint32_t GetFontColor() const = 0;                                 // [12]
    virtual std::uint32_t GetFontBackgroundColor() const = 0;                       // [13]
    virtual void SetFontColor(std::uint32_t color, std::uint32_t background) = 0;   // [14]
    virtual int GetAntialiasing() = 0;                                              // [15]
    virtual void SetAntialiasing(int mode) = 0;                                     // [16]
    virtual float GetAntialiasingGammaCorrection() = 0;                             // [17]
    virtual void SetAntialiasingGammaCorrection(float gamma) = 0;                   // [18]
    virtual float GetAntialiasingSharpness() = 0;                                   // [19]
    virtual void SetAntialiasingSharpness(float sharpness) = 0;                     // [20]
    virtual void GetFilePath(cIGZString& out) const = 0;                            // [21]
    virtual void SetFilePath(const cIGZString& path) = 0;                           // [22]
    virtual bool GetCodePage(int& codePage) const = 0;                              // [23]
    virtual bool SetCodePage(int codePage) = 0;                                     // [24]
    virtual bool IsCodePageAvailable(int codePage) = 0;                             // [25]
    virtual bool IsCharEncodingAvailable(int encoding) = 0;                         // [26]
    virtual bool IsCharAvailable(void* chars, std::uint32_t length) = 0;            // [27]
    virtual bool PreloadChars(void* chars, std::uint32_t length) = 0;               // [28]
    virtual void GetScaling(float& x, float& y) const = 0;                          // [29]
    virtual void SetScaling(float x, float y) = 0;                                  // [30]
    virtual void GetAdvanceScaling(float& x, float& y) const = 0;                   // [31]
    virtual void SetAdvanceScaling(float x, float y) = 0;                           // [32]
    virtual void GetSpacingAdjustment(int& x, int& y) const = 0;                    // [33]
    virtual void SetSpacingAdjustment(int x, int y) = 0;                            // [34]
    virtual int GetLineHeight() = 0;                                                // [35]
    virtual int CharWidth(const void* chars, std::uint32_t length) = 0;             // [36]
    virtual int CharHeight(const void* chars, std::uint32_t length) = 0;            // [37] (0, 0): the font's height
    virtual int GetSizeAboveBaseline() = 0;                                         // [38]
    virtual int GetSizeBelowBaseline() = 0;                                         // [39]
    virtual int GetAdvanceWidth(const void* chars, std::uint32_t length) = 0;       // [40]
    virtual std::uint32_t CalculateWordsToFitInWidth(const void* text, std::uint32_t length, std::uint32_t width,
                                                     std::uint32_t* fitted, WordWrapMode mode) = 0;   // [41]
    virtual std::uint32_t CalculateCharsToFitInWidth(const void* text, std::uint32_t length, std::uint32_t width,
                                                     std::uint32_t* fitted) = 0;                       // [42]
    virtual std::uint32_t CalculateNumberOfLines(const void* text, std::uint32_t length, std::uint32_t width) = 0;   // [43]
    virtual std::uint32_t CalculateWidthOfLines(const void* text, std::uint32_t length, std::uint32_t width) = 0;    // [44]
    virtual bool CalculateTextArea(const void* text, std::uint32_t length, cRZRect& area, bool a, bool b) = 0;        // [45]
    virtual bool CalculateTextArea(const void* text, std::uint32_t length, cRZRect& area, int a, int b, int c) = 0;   // [46]
    virtual bool TextArea(const void* text, std::uint32_t length, cRZRect& area) = 0;                                 // [47]
    virtual int GetStringWidth(const void* text, std::uint32_t length) = 0;                                           // [48]
    virtual int GetStringVisibleWidth(const void* text, std::uint32_t length, std::uint32_t& visible) = 0;           // [49]
    virtual bool FormatForEllipsis(cIGZString& out, const cIGZString& in, const cRZRect& area, bool a) = 0;          // [50]
    virtual bool DrawTextLine(void* surface, int x, int y, const void* text, std::uint32_t length, std::uint32_t color,
                              std::uint32_t background, const cRZRect* clip, cRZPoint* end) = 0;                      // [51]
    virtual bool DrawTextParagraph(void* surface, const cRZRect& area, const void* text, std::uint32_t length,
                                   int alignment, std::uint32_t color, std::uint32_t background, const cRZRect* clip,
                                   cRZPoint* end) = 0;                                                                 // [52]
    virtual cIGZFontRenderer* GetRenderer() const = 0;                              // [53]
    virtual void SetRenderer(cIGZFontRenderer* renderer) = 0;                       // [54]
    virtual bool LockSurfaceForFontUse(void* surface) = 0;                          // [55]
    virtual bool UnlockSurfaceForFontUse(void* surface) = 0;                        // [56]
};

// "use the font's own colour" for the colour arguments of DrawTextParagraph.
constexpr std::uint32_t kFontDefaultColor = 0x8000000;

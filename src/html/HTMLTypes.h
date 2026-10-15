// Shared types of the in-game HTML viewer (cHTML* family, 0x10056e0ea..0x1005c2170 + cHTMLDocument).
// The viewer lays out a small HTML 3.2/4.0 subset into a tree of elements and draws it through the GZ graphics
// system. The services the original reaches through the framework (graphics system, font system, the owning
// window, buffers) are the small interfaces below.
#pragma once
#include <cstdint>
#include <string>

#include "gz/StreamOps.h"   // cRZRect {left, top, right, bottom}
#include "rz/cRZString.h"

namespace html {

using Color = std::uint32_t;

// Object kinds stored at +8 of every element (eHTMLObjectType); only the values the recovered code assigns.
enum eHTMLObjectType : std::int32_t {
    kTypeBody = 0, kTypeTextSegment = 1, kTypeImg = 3, kTypeAnchor = 4, kTypeLink = 0xb, kTypeTable = 0xe,
    kTypeTD = 0x10, kTypeMeta = 0x21,
};

class IHtmlGraphics {          // RZ::GS(): colour factory (vslot 0x118)
public:
    virtual ~IHtmlGraphics() = default;
    virtual Color MakeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;
};

class IHtmlBitmap {            // cIGZBuffer
public:
    virtual ~IHtmlBitmap() = default;
    virtual void AddRef() = 0;
    virtual void Release() = 0;
    virtual int Width() = 0;
    virtual int Height() = 0;
    virtual cRZRect Bounds() = 0;
};

// Surface flags passed to Begin/End by the drawing code.
constexpr std::uint32_t kSurfaceFill = 0x8001, kSurfaceLines = 0x8004, kSurfaceBlit = 0x8010;

class IHtmlSurface {           // cIGZBuffer being painted into
public:
    virtual ~IHtmlSurface() = default;
    virtual bool Begin(std::uint32_t flags) = 0;
    virtual void End(std::uint32_t flags) = 0;
    virtual void FillRect(Color color, const cRZRect& rect, const cRZRect* clip) = 0;
    virtual void DrawLine(int x0, int y0, int x1, int y1, Color color, int width) = 0;
    virtual void Blit(IHtmlBitmap* source, const cRZRect& sourceRect, const cRZRect& destination, const cRZRect* clip) = 0;
    virtual void BlitStretched(IHtmlBitmap* source, const cRZRect& sourceRect, const cRZRect& destination, const cRZRect* clip) = 0;
};

class IHtmlFont {              // cIGZFont (slots used by the text segment)
public:
    virtual ~IHtmlFont() = default;
    virtual void Release() = 0;
    virtual void SetStyle(std::uint32_t style, bool enable) = 0;                    // vslot 0x40
    virtual int LineHeight() = 0;                                                    // 0x118
    virtual int GlyphHeight(int a, int b) = 0;                                       // 0x128
    virtual int Ascent() = 0;                                                        // 0x130
    // number of characters of `text` that fit in maxWidth, breaking at word boundaries (0 when not even a word fits)
    virtual int CharsThatFit(const char* text, int length, int maxWidth, int a, int b) = 0;                // 0x148
    // length of the first word and its pixel width (used when nothing fits)
    virtual int FirstWordLength(const char* text, int length, int maxWidth, int* wordWidth) = 0;           // 0x150
    virtual int StringWidth(const char* text, int length) = 0;                                              // 0x180
    virtual void DrawText(IHtmlSurface* surface, int x, int y, const char* text, int length, Color color,
                          std::uint32_t flags, const cRZRect* clip) = 0;                                    // 0x198
};

struct tFontAttributes {       // layout of the original: +8 face list, +0x28 kind, +0x2c size, +0x30 colour, +0x34 style
    std::string faceNames;     // comma separated
    std::uint32_t kind = 0;    // bit0 variable width, bit1 fixed width
    std::uint32_t size = 0;
    Color color = 0;
    std::uint32_t style = 0;
    bool preformatted = false;     // +0x38
    bool isLink = false;           // +0x39
};

class IHtmlFontSystem {        // cIGZFontSys (service 0x20673960), vslot 0x48
public:
    virtual ~IHtmlFontSystem() = default;
    virtual IHtmlFont* CreateFont(const std::string& faceName, std::uint32_t kind, std::uint32_t size, std::uint32_t style, bool a) = 0;
};

// Receives the picture of a pending image request (cHTMLIMG, cHTMLBODY background).
class IHtmlBitmapReceiver {
public:
    virtual ~IHtmlBitmapReceiver() = default;
    virtual bool SetBitmap(IHtmlBitmap* bitmap) = 0;     // true when the size of the picture changed
};

// What the text segments need from the document that owns them (the original passes cHTMLDocument*).
class IHtmlDocumentServices {
public:
    virtual ~IHtmlDocumentServices() = default;
    virtual IHtmlFontSystem* FontSystem() = 0;
    virtual std::string GetStringForEscape(const cRZString& entityName) = 0;                      // "&name;" -> text
    virtual void GetDefaultVariableWidthFontInfo(std::string& faceName, std::uint32_t& style) = 0;
    virtual void GetDefaultFixedWidthFontInfo(std::string& faceName, std::uint32_t& style) = 0;
};

}  // namespace html

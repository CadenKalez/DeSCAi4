// cHTMLDrawContext — the cursor and margins the HTML elements lay themselves out against (15 functions,
// 0x10056e0ea..0x10056e790; object of 0xa0 bytes, copied by value for table cells).
//
// Layout works in "document" coordinates (scrolling offset removed) while drawing works in "screen"
// coordinates; the two rectangles at +0x18 (screen) and +0x28 (document) map one onto the other. Text and
// images are queued as *inline elements* on the current line; RenderLineElements() measures the line, aligns it
// and lets every queued element place itself, then advances the cursor by the line height.
//
// Original layout: +0 graphics, +8 window, +0x10 surface, +0x18 screen rect, +0x28 document rect, +0x38 line
// height, +0x3c line ascent, +0x40/+0x44 cursor x/y (screen), +0x48 background colour, +0x4c left margin, +0x50
// hanging-indent left margin, +0x54 right margin, +0x58 inline element list, +0x70 left margin stack, +0x88
// right margin stack.
#pragma once
#include <cstdint>
#include <list>
#include <vector>

#include "html/HTMLTypes.h"

namespace html {

class cHTMLBODY;
class cHTMLDrawContext;

// Interface of anything that can sit on a text line (text segments, inline images).
class cHTMLInlineElement {
public:
    virtual ~cHTMLInlineElement() = default;
    virtual int OutputStyle() = 0;                                    // 0 left, 1 centre, 2 right alignment of the line
    virtual void GetHeight(int& ascent, int& height) = 0;
    // How much of the element fits in `width`: stores the used width, returns true when the line is full
    virtual bool CalculateWidthThatFitsInLine(int width, int& used, bool force) = 0;
    virtual bool RenderOneLine(cHTMLDrawContext& context) = 0;        // true when the element is finished
    virtual bool SkipWhiteSpaces() = 0;                               // true when nothing but blanks was left
};

// The window hosting the document (cGZWinHTML): link colours and repainting.
class IHtmlWindow {
public:
    virtual ~IHtmlWindow() = default;
    virtual cHTMLBODY* Body() = 0;                                    // vslot 0x70
    virtual Color DefaultActiveLinkColor() = 0;                       // 0xa0
    virtual Color DefaultVisitedLinkColor() = 0;                      // 0xa8
    virtual void Invalidate() = 0;                                    // 0x98
};

class cHTMLDrawContext {
public:
    cHTMLDrawContext() = default;

    struct tLeftMarginStackItem {          // 16 bytes
        bool autoPop = false;              // +0: pushed by ChangeLeftMargin (floating image), popped once the cursor passes popAtY
        std::int32_t popAtY = 0;           // +4
        std::int32_t oldLeft = 0;          // +8
        std::int32_t oldIndent = 0;        // +0xc
    };
    struct tRightMarginStackItem {         // 8 bytes
        std::int32_t popAtY = 0;
        std::int32_t oldRight = 0;
    };

    void AttachInlineElement(cHTMLInlineElement* element);
    void RenderLineElements(bool force);
    void ChangeLeftMargin(int x, int popAtY);
    void ChangeRightMargin(int x, int popAtY);
    void PushLeftMargin(int x);
    void PopLeftMargin();
    void BeginHangingIndent();
    bool ComputeCurrentLineHeightAndWidth(int& remainingWidth);
    void IncrementCurrentDrawY(int dy);
    void InitForFreshPainting();
    bool IsObjectVisible(const cRZRect& rect, bool strict) const;
    void DocumentRectToScreenRect(const cRZRect& document, cRZRect& screen) const;
    void ScreenRectToDocumentRect(const cRZRect& screen, cRZRect& document) const;
    void DocumentPtToScreenPt(int x, int y, int& screenX, int& screenY) const;

    IHtmlGraphics* graphics = nullptr;      // +0
    IHtmlWindow* window = nullptr;          // +8
    IHtmlSurface* surface = nullptr;        // +0x10
    cRZRect screenRect;                     // +0x18
    cRZRect documentRect;                   // +0x28
    std::int32_t lineHeight = 0;            // +0x38
    std::int32_t lineAscent = 0;            // +0x3c
    std::int32_t drawX = 0, drawY = 0;      // +0x40, +0x44
    Color backgroundColor = 0;              // +0x48
    std::int32_t leftMargin = 0;            // +0x4c
    std::int32_t hangingLeftMargin = 0;     // +0x50
    std::int32_t rightMargin = 0;           // +0x54
    std::list<cHTMLInlineElement*> inlineElements;   // +0x58
    std::vector<tLeftMarginStackItem> leftMargins;   // +0x70
    std::vector<tRightMarginStackItem> rightMargins; // +0x88
};

constexpr std::int32_t kNoPopY = 0x3fffffff;   // "never": margin items that outlive the layout

}  // namespace html

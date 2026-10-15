// The element tree of the HTML viewer (cHTMLElement and its subclasses, ~230 functions, 0x1005ad25a..0x1005c0c4a).
// Every element knows how to lay itself out against a cHTMLDrawContext (Layout / PostLayout), draw itself (Draw),
// answer a mouse pick (Pick) and react to the left button (OnMouseDownL / OnMouseUpL). Containers hold children;
// text and images are *inline* elements that queue themselves on the draw context's current line.
//
// Common layout of the original: +0 vptr, +8 object type, +0xc "has a draw rect", +0xd "laid out", +0x10 draw rect
// (document coordinates). Containers add the child vector at +0x20.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "html/HTMLDrawContext.h"
#include "html/HTMLShared.h"
#include "html/HTMLTypes.h"

namespace html {


// ---- base ----------------------------------------------------------------------------------------------
class cHTMLElement {
public:
    explicit cHTMLElement(std::int32_t type) : type_(type) {}
    virtual ~cHTMLElement() = default;
    cHTMLElement(const cHTMLElement&) = delete;
    cHTMLElement& operator=(const cHTMLElement&) = delete;

    virtual void Insert(cHTMLElement*) {}                           // [2]
    virtual bool IsContainer() { return false; }                    // [3]
    virtual void Layout(cHTMLDrawContext&) {}                       // [4]
    virtual void PostLayout() {}                                    // [5]
    virtual void Draw(cHTMLDrawContext&) {}                         // [6]
    virtual bool Selectable() { return false; }                     // [7]
    // 0 outside, 1 inside a non-selectable element, 2 inside a selectable one (`hit` receives it)
    virtual int Pick(cHTMLElement*& hit, int x, int y);             // [8]
    virtual void OnMouseDownL(cHTMLDrawContext&) {}                 // [9]
    virtual void OnMouseUpL(cHTMLDrawContext&) {}                   // [10]
    virtual void SetLinkColor(Color) {}                             // [11]

    // Unions `rect` into the element's draw rect (empty rectangles are ignored).
    void AddDrawRect(const cRZRect& rect);

    std::int32_t type() const { return type_; }
    bool hasDrawRect() const { return hasDrawRect_; }
    bool laidOut() const { return laidOut_; }
    const cRZRect& drawRect() const { return drawRect_; }

protected:
    std::int32_t type_;
    bool hasDrawRect_ = false;      // +0xc
    bool laidOut_ = false;          // +0xd
    cRZRect drawRect_;              // +0x10
};

// ---- containers ----------------------------------------------------------------------------------------
class cHTMLContainerElement : public cHTMLElement {
public:
    explicit cHTMLContainerElement(std::int32_t type) : cHTMLElement(type) {}
    ~cHTMLContainerElement() override;

    void Insert(cHTMLElement* child) override { children_.push_back(child); }
    bool IsContainer() override { return true; }
    void Layout(cHTMLDrawContext& context) override;
    void PostLayout() override { PostLayoutContainer(); }
    void Draw(cHTMLDrawContext& context) override { DrawContainer(context); }
    int Pick(cHTMLElement*& hit, int x, int y) override;
    void SetLinkColor(Color color) override;

    void DrawContainer(cHTMLDrawContext& context);
    void PostLayoutContainer();
    const std::vector<cHTMLElement*>& children() const { return children_; }

protected:
    std::vector<cHTMLElement*> children_;       // +0x20, owned
};

// <a>: a container with an optional href / absolute href / name; clicking recolours the link text.
class cHTMLAnchor : public cHTMLContainerElement {
public:
    cHTMLAnchor() : cHTMLContainerElement(kTypeAnchor) {}
    void SetHREF(const cRZString& value) { href_ = std::make_unique<cRZString>(value); }
    void SetHREFAbsolute(const cRZString& value) { hrefAbsolute_ = std::make_unique<cRZString>(value); }
    void SetNAME(const cRZString& value) { name_ = std::make_unique<cRZString>(value); }
    const cRZString* href() const { return href_.get(); }
    const cRZString* hrefAbsolute() const { return hrefAbsolute_.get(); }
    const cRZString* name() const { return name_.get(); }
    bool Selectable() override { return href_ != nullptr || hrefAbsolute_ != nullptr; }
    void OnMouseDownL(cHTMLDrawContext& context) override;
    void OnMouseUpL(cHTMLDrawContext& context) override;

private:
    std::unique_ptr<cRZString> href_, hrefAbsolute_, name_;     // +0x38, +0x40, +0x48
};

// <body>: link colours and background picture; lays its children out from a fresh cursor.
class cHTMLBODY : public cHTMLContainerElement, public IHtmlBitmapReceiver {
public:
    cHTMLBODY(Color link, Color visitedLink, Color activeLink);
    ~cHTMLBODY() override;
    void Layout(cHTMLDrawContext& context) override;
    bool SetBitmap(IHtmlBitmap* bitmap) override;
    void SetLINK(const cRZString& value);
    void SetALINK(const cRZString& value);
    void SetVLINK(const cRZString& value);
    Color linkColor = 0, visitedLinkColor = 0, activeLinkColor = 0;     // +0x48, +0x4c, +0x50
    IHtmlBitmap* background() const { return background_; }
    Color backgroundColor = 0;               // BGCOLOR (the original keeps a heap-allocated colour at +0x40)
    bool hasBackgroundColor = false;
    bool fixedBackground = false;            // BGPROPERTIES (+0x60)
    IHtmlGraphics* graphics = nullptr;      // RZ::GS()

private:
    IHtmlBitmap* background_ = nullptr;     // +0x58
};

class cHTMLBLINK : public cHTMLContainerElement {     // visible during the second half of every second
public:
    cHTMLBLINK() : cHTMLContainerElement(kTypeBlink) {}
    void Draw(cHTMLDrawContext& context) override;
    static std::uint32_t (*Clock)();                  // cRZTime::GetMillisecondSystemTime
private:
    static constexpr std::int32_t kTypeBlink = 0x20;
};

// Block containers that only differ in the spacing they add around their children.
class cHTMLBlockQuote : public cHTMLContainerElement {
public:
    cHTMLBlockQuote() : cHTMLContainerElement(0x14) {}
    void Layout(cHTMLDrawContext& context) override;
    void Draw(cHTMLDrawContext& context) override;
};
class cHTMLDIV : public cHTMLContainerElement {
public:
    cHTMLDIV() : cHTMLContainerElement(0x13) {}
    void Layout(cHTMLDrawContext& context) override;
    void Draw(cHTMLDrawContext& context) override;
};
class cHTMLP : public cHTMLContainerElement {
public:
    cHTMLP() : cHTMLContainerElement(0x16) {}
    void Layout(cHTMLDrawContext& context) override;
    void Draw(cHTMLDrawContext& context) override;
};
class cHTMLHeading : public cHTMLContainerElement {
public:
    explicit cHTMLHeading(std::int32_t type = 0x17) : cHTMLContainerElement(type) {}
    void Layout(cHTMLDrawContext& context) override;
    void Draw(cHTMLDrawContext& context) override;
};
class cHTMLDD : public cHTMLContainerElement {          // definition: indented one step
public:
    cHTMLDD() : cHTMLContainerElement(0x18) {}
    void Layout(cHTMLDrawContext& context) override;
    void Draw(cHTMLDrawContext& context) override;
};
class cHTMLDT : public cHTMLContainerElement {
public:
    cHTMLDT() : cHTMLContainerElement(0x19) {}
    void Layout(cHTMLDrawContext& context) override;
    void Draw(cHTMLDrawContext& context) override;
};
class cHTMLList : public cHTMLContainerElement {         // <ul>/<ol>/<dl> body: indent by one step
public:
    cHTMLList(std::int32_t type, std::int32_t style) : cHTMLContainerElement(type), listStyle(style) {}
    void Layout(cHTMLDrawContext& context) override;
    void Draw(cHTMLDrawContext& context) override;
    std::int32_t listStyle;          // +0x38
};
class cHTMLListItem : public cHTMLContainerElement {
public:
    cHTMLListItem() : cHTMLContainerElement(0xc) {}
    void Layout(cHTMLDrawContext& context) override;
    void Draw(cHTMLDrawContext& context) override;
};
class cHTMLListItemHangingIndent : public cHTMLContainerElement {   // hanging indent starts after the bullet
public:
    cHTMLListItemHangingIndent() : cHTMLContainerElement(0xc) {}
    void Layout(cHTMLDrawContext& context) override { context.BeginHangingIndent(); }
    void Draw(cHTMLDrawContext&) override {}
};
// Containers without behaviour of their own.
class cHTMLSPAN : public cHTMLContainerElement { public: cHTMLSPAN() : cHTMLContainerElement(0x23) {} };
class cHTMLAREA : public cHTMLContainerElement {      // image map region; shape: 1 default, 2 rect, 3 circle, 4 poly
public:
    cHTMLAREA() : cHTMLContainerElement(0x18) {}
    std::int32_t shape = 0;                        // +0x38
};
class cHTMLFORM : public cHTMLContainerElement { public: cHTMLFORM() : cHTMLContainerElement(0x1f) {} };
class cHTMLMAP : public cHTMLContainerElement {
public:
    cHTMLMAP() : cHTMLContainerElement(0x17) {}
    std::unique_ptr<cRZString> name;               // +0x38
};
class cHTMLOPTION : public cHTMLContainerElement { public: cHTMLOPTION() : cHTMLContainerElement(0x23) {} };
class cHTMLSELECT : public cHTMLContainerElement { public: cHTMLSELECT() : cHTMLContainerElement(0x1e) {} };
class cHTMLTH : public cHTMLContainerElement { public: cHTMLTH() : cHTMLContainerElement(0x11) {} };

// ---- leaf elements -----------------------------------------------------------------------------------------
class cHTMLBR : public cHTMLElement {
public:
    cHTMLBR() : cHTMLElement(0x16) {}
    void Layout(cHTMLDrawContext& context) override;
    void Draw(cHTMLDrawContext& context) override { if (!laidOut_) Layout(context); }
};
class cHTMLHR : public cHTMLElement {
public:
    cHTMLHR() : cHTMLElement(5) {}
    void Layout(cHTMLDrawContext& context) override;
    void Draw(cHTMLDrawContext& context) override;
    std::int32_t align = 3;          // +0x20
    std::int32_t size = 1;           // +0x24 thickness
    std::int32_t width = 100;        // +0x28
    bool widthIsPercent = true;      // +0x2c
};
class cHTMLINPUT : public cHTMLElement { public: cHTMLINPUT() : cHTMLElement(0x1d) {} };
class cHTMLLink : public cHTMLElement {               // <link>: remembers rel / href / type
public:
    cHTMLLink() : cHTMLElement(kTypeLink) {}
    std::unique_ptr<cRZString> rel, href, type;       // +0x20, +0x30, +0x38
    bool flag = true;                                 // +0x28
};
class cHTMLMETA : public cHTMLElement {
public:
    cHTMLMETA() : cHTMLElement(kTypeMeta) {}
    std::unique_ptr<cRZString> httpEquiv, name, content;    // +0x20, +0x28, +0x30
};

// <img>: a bitmap placed inline (or floated left / right). align: 0 left, 1 right, 2 top, 3 middle, 4 bottom.
class cHTMLIMG : public cHTMLElement, public cHTMLInlineElement, public IHtmlBitmapReceiver {
public:
    cHTMLIMG();
    ~cHTMLIMG() override;
    void Layout(cHTMLDrawContext& context) override;
    void Draw(cHTMLDrawContext& context) override;
    void DrawGivenScreenRect(const cRZRect& screenRect, cHTMLDrawContext& context);

    // IHtmlBitmapReceiver: true when the picture size changed
    bool SetBitmap(IHtmlBitmap* bitmap) override;
    // cHTMLInlineElement
    int OutputStyle() override { return outputStyle; }
    void GetHeight(int& ascent, int& height) override;
    bool CalculateWidthThatFitsInLine(int width, int& used, bool force) override;
    bool RenderOneLine(cHTMLDrawContext& context) override;
    bool SkipWhiteSpaces() override { return false; }

    IHtmlBitmap* bitmap = nullptr;           // +0x30
    std::unique_ptr<cRZString> source;       // +0x38
    std::int32_t width = 0, height = 0;      // +0x40, +0x44
    std::int32_t align = 4;                  // +0x48
    std::int32_t outputStyle = 0;            // +0x4c (the line alignment of the surrounding text)
};

// Pieces of text with one font; wraps over as many lines as needed.
class cHTMLTextSegment : public cHTMLElement, public cHTMLInlineElement {
public:
    struct tRenderAreaInfo {                 // one rendered line: where, and which characters
        cRZRect area;
        std::int32_t start = 0;
        std::int32_t length = 0;
    };
    cHTMLTextSegment(IHtmlDocumentServices* document, const cRZString& text, const tFontAttributes& attributes, int outputStyle);
    ~cHTMLTextSegment() override;

    void Layout(cHTMLDrawContext& context) override;
    void Draw(cHTMLDrawContext& context) override;
    int Pick(cHTMLElement*& hit, int x, int y) override;
    void SetLinkColor(Color color) override { if (isLink_) color_ = color; }

    int OutputStyle() override { return outputStyle_; }
    void GetHeight(int& ascent, int& height) override;
    bool CalculateWidthThatFitsInLine(int width, int& used, bool force) override;
    bool RenderOneLine(cHTMLDrawContext& context) override;
    bool SkipWhiteSpaces() override;

    // Whitespace runs collapse to one blank (or, preformatted, CR / LF / CRLF become '\n').
    static void FixTextSegmentWhitespace(std::string& text, bool preformatted);
    const std::string& text() const { return text_; }
    const std::vector<tRenderAreaInfo>& renderAreas() const { return renderAreas_; }

private:
    static IHtmlFont* GetFont(IHtmlDocumentServices* document, const tFontAttributes& attributes);

    std::string text_;               // +0x30
    IHtmlFont* font_ = nullptr;      // +0x50
    Color color_ = 0;                // +0x58
    bool preformatted_ = false;      // +0x5c
    bool isLink_ = false;            // +0x5d
    int outputStyle_ = 0;            // +0x60
    int cursor_ = 0;                 // +0x64 next character to render
    int fitting_ = 0;                // +0x68 characters that fit on the current line
    std::vector<tRenderAreaInfo> renderAreas_;   // +0x70
};

// ---- tables ----------------------------------------------------------------------------------------------
class cHTMLTABLE;

class cHTMLTD : public cHTMLContainerElement {
public:
    cHTMLTD() : cHTMLContainerElement(kTypeTD) {}
    void Layout(cHTMLDrawContext& context) override;                         // measuring pass
    void Layout(cHTMLDrawContext& context, const cRZRect& cell);             // final pass into `cell`
    void PostLayout() override {}
    void Draw(cHTMLDrawContext& context) override;
    void ExpandDrawRect(cHTMLDrawContext& context);
    void SetBGCOLOR(const cRZString& value, IHtmlGraphics* graphics);
    void SetNOWRAP(const cRZString&) { noWrap = true; }

    std::int32_t valign = 0;                 // +0x38: 1 top, 2 middle, 3 bottom, 4 baseline
    tLength width{tLength::kUnset, 0};   // +0x3c unit, +0x40 value
    Color backgroundColor = 0;       // +0x44
    bool hasBackground = false;      // +0x48
    bool noWrap = false;             // +0x49
    IHtmlGraphics* graphics = nullptr;
    tLength widthAttr() const { return width; }
    cHTMLTD(const cHTMLTD&) = delete;
};

class cHTMLTR : public cHTMLContainerElement {
public:
    cHTMLTR() : cHTMLContainerElement(0xf) {}
    void Layout(cHTMLDrawContext&) override {}
    void Layout(cHTMLDrawContext& context, cHTMLTABLE& table, int row, bool placeCells, bool finalPass);
    void Draw(cHTMLDrawContext& context) override { DrawContainer(context); }
    void SetBGCOLOR(const cRZString& value, IHtmlGraphics* graphics);
    Color backgroundColor = 0;       // +0x50
    bool hasBackground = false;      // +0x54
    std::vector<cHTMLTD*> cells;     // +0x38 (also in children_)
};

class cHTMLTABLE : public cHTMLContainerElement {
public:
    struct ColumnInfo {              // 0x1c bytes
        std::int32_t minWidth = 0;       // +0  widest unbreakable content
        std::int32_t maxWidth = 0;       // +4  widest content
        std::int32_t fixedMin = 0;       // +8  widest fixed (pixel) cell
        std::int32_t availableMax = 0;   // +0xc narrowest fixed cell / available width
        std::int32_t finalWidth = 0;     // +0x10
        std::int32_t x = 0;              // +0x14 left edge of the column
        std::int32_t spread = 0;         // +0x18 max - min (how much the column may grow)
    };
    cHTMLTABLE() : cHTMLContainerElement(kTypeTable) {}
    void Layout(cHTMLDrawContext& context) override;
    void Draw(cHTMLDrawContext& context) override;
    void SetBGCOLOR(const cRZString& value, IHtmlGraphics* graphics);
    void SetWIDTH(const cRZString& value) { cHTMLShared::SetLength(value, widthAttr); }

    std::int32_t border = 0;                  // +0x38
    tLength widthAttr{tLength::kUnset, 0};    // +0x3c
    tLength heightAttr{tLength::kUnset, 0};   // +0x44
    tLength spacingAttr{tLength::kUnset, 0};  // +0x4c
    Color backgroundColor = 0;                // +0x54
    bool hasBackground = false;               // +0x58
    std::vector<cHTMLTR*> rows;               // +0x60
    std::vector<ColumnInfo> columns;          // +0xa8
    std::vector<std::int32_t> rowHeights;     // +0xc0
    std::vector<std::int32_t> rowTops;        // +0xd8

    cHTMLTABLE(const cHTMLTABLE&) = delete;
    void AddRow(cHTMLTR* row) { rows.push_back(row); }
};

}  // namespace html

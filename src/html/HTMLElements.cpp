#include "html/HTMLElements.h"

#include <algorithm>
#include <cstdlib>

#include "rz/TextEncoding.h"

namespace html {

namespace {
// Code page 0x6faf (28591) is ISO 8859-1: every byte value is the Unicode code point of the same number.
void AppendLatin1AsUtf8(std::string& out, unsigned code) {
    code &= 0xff;
    if (code < 0x80) {
        out.push_back(static_cast<char>(code));
    } else {
        out.push_back(static_cast<char>(0xc0 | (code >> 6)));
        out.push_back(static_cast<char>(0x80 | (code & 0x3f)));
    }
}

Color ColorFromAttribute(const cRZString& value, IHtmlGraphics* graphics) {
    Color color = 0xff000000;
    cHTMLShared::SetColor(value, color);
    return graphics->MakeColor(static_cast<std::uint8_t>(color >> 16), static_cast<std::uint8_t>(color >> 8), static_cast<std::uint8_t>(color));
}

// HTMLIsSpace(char const*, int): the framework code page is UTF-8, where the blanks are the ASCII ones.
bool HTMLIsSpace(const std::string& text, int index) {
    if (index < 0 || index >= static_cast<int>(text.size())) return false;
    const unsigned char c = static_cast<unsigned char>(text[static_cast<std::size_t>(index)]);
    return c == ' ' || c == '\t' || c == '\n' || c == '\f' || c == '\r';
}
int HTMLAdvance(const std::string& text, int index) {
    const auto lead = static_cast<unsigned char>(text[static_cast<std::size_t>(index)]);
    return index + 1 + UTF8GetFollowByteCount(lead);
}
}  // namespace

// ---- cHTMLElement -------------------------------------------------------------------------------------------

void cHTMLElement::AddDrawRect(const cRZRect& rect) {
    if (!(rect.left < rect.right && rect.top < rect.bottom)) return;
    if (!hasDrawRect_) {
        drawRect_ = rect;
        hasDrawRect_ = true;
        return;
    }
    drawRect_.left = std::min(drawRect_.left, rect.left);
    drawRect_.top = std::min(drawRect_.top, rect.top);
    drawRect_.right = std::max(drawRect_.right, rect.right);
    drawRect_.bottom = std::max(drawRect_.bottom, rect.bottom);
}

int cHTMLElement::Pick(cHTMLElement*& hit, int x, int y) {
    if (!(drawRect_.left <= x && drawRect_.top <= y && x < drawRect_.right && y < drawRect_.bottom)) return 0;
    if (!Selectable()) return 1;
    hit = this;
    return 2;
}

// ---- containers ----------------------------------------------------------------------------------------------

cHTMLContainerElement::~cHTMLContainerElement() {
    for (cHTMLElement* child : children_) delete child;
}

void cHTMLContainerElement::Layout(cHTMLDrawContext& context) {
    hasDrawRect_ = false;
    for (cHTMLElement* child : children_) child->Layout(context);
}

void cHTMLContainerElement::PostLayoutContainer() {
    for (cHTMLElement* child : children_) {
        child->PostLayout();
        if (child->laidOut()) AddDrawRect(child->drawRect());
    }
    laidOut_ = true;
}

void cHTMLContainerElement::DrawContainer(cHTMLDrawContext& context) {
    for (cHTMLElement* child : children_) {
        if (context.IsObjectVisible(child->drawRect(), false)) child->Draw(context);
    }
}

int cHTMLContainerElement::Pick(cHTMLElement*& hit, int x, int y) {
    if (!(drawRect_.left <= x && drawRect_.top <= y && x < drawRect_.right && y < drawRect_.bottom)) return 0;
    for (cHTMLElement* child : children_) {
        const int result = child->Pick(hit, x, y);
        if (result == 2) return 2;
        if (result == 1) {
            hit = this;
            return Selectable() ? 2 : 1;
        }
    }
    return 0;
}

void cHTMLContainerElement::SetLinkColor(Color color) {
    for (cHTMLElement* child : children_) child->SetLinkColor(color);
}

// ---- anchor ------------------------------------------------------------------------------------------------------

void cHTMLAnchor::OnMouseDownL(cHTMLDrawContext& context) {
    Color color = 0;
    if (context.window) {
        cHTMLBODY* body = context.window->Body();
        color = body ? body->activeLinkColor : context.window->DefaultActiveLinkColor();
    }
    SetLinkColor(color);
    if (context.window) context.window->Invalidate();
}

void cHTMLAnchor::OnMouseUpL(cHTMLDrawContext& context) {
    Color color = 0;
    if (context.window) {
        cHTMLBODY* body = context.window->Body();
        color = body ? body->visitedLinkColor : context.window->DefaultVisitedLinkColor();
    }
    SetLinkColor(color);
    if (context.window) context.window->Invalidate();
}

// ---- body -------------------------------------------------------------------------------------------------------

cHTMLBODY::cHTMLBODY(Color link, Color visitedLink, Color activeLink)
    : cHTMLContainerElement(kTypeBody), linkColor(link), visitedLinkColor(visitedLink), activeLinkColor(activeLink) {}

cHTMLBODY::~cHTMLBODY() {
    if (background_) background_->Release();
}

bool cHTMLBODY::SetBitmap(IHtmlBitmap* bitmap) {
    background_ = bitmap;
    if (bitmap) bitmap->AddRef();
    return false;
}

void cHTMLBODY::SetLINK(const cRZString& value) { linkColor = ColorFromAttribute(value, graphics); }
void cHTMLBODY::SetALINK(const cRZString& value) { activeLinkColor = ColorFromAttribute(value, graphics); }
void cHTMLBODY::SetVLINK(const cRZString& value) { visitedLinkColor = ColorFromAttribute(value, graphics); }

void cHTMLBODY::Layout(cHTMLDrawContext& context) {
    context.InitForFreshPainting();
    hasDrawRect_ = false;
    for (cHTMLElement* child : children_) child->Layout(context);
    context.RenderLineElements(true);
}

std::uint32_t (*cHTMLBLINK::Clock)() = nullptr;

void cHTMLBLINK::Draw(cHTMLDrawContext& context) {
    const std::uint32_t now = Clock ? Clock() : 0;
    if (now % 1000 > 500) DrawContainer(context);
}

// ---- block containers --------------------------------------------------------------------------------------------

#define HTML_BLOCK_DRAW(Class)                                   \
    void Class::Draw(cHTMLDrawContext& context) {                \
        if (!laidOut_) Layout(context);                          \
        DrawContainer(context);                                  \
    }

void cHTMLBlockQuote::Layout(cHTMLDrawContext& context) {
    context.RenderLineElements(true);
    context.IncrementCurrentDrawY(12);
    context.PushLeftMargin(context.leftMargin + 20);
    context.drawX = context.leftMargin;
    hasDrawRect_ = false;
    for (cHTMLElement* child : children_) child->Layout(context);
    context.RenderLineElements(true);
    context.PopLeftMargin();
    context.IncrementCurrentDrawY(12);
}
HTML_BLOCK_DRAW(cHTMLBlockQuote)

void cHTMLDIV::Layout(cHTMLDrawContext& context) {
    context.RenderLineElements(true);
    hasDrawRect_ = false;
    for (cHTMLElement* child : children_) child->Layout(context);
    context.RenderLineElements(true);
}
HTML_BLOCK_DRAW(cHTMLDIV)

void cHTMLP::Layout(cHTMLDrawContext& context) {
    context.RenderLineElements(true);
    context.IncrementCurrentDrawY(6);
    hasDrawRect_ = false;
    for (cHTMLElement* child : children_) child->Layout(context);
    context.RenderLineElements(true);
    context.IncrementCurrentDrawY(6);
}
HTML_BLOCK_DRAW(cHTMLP)

void cHTMLHeading::Layout(cHTMLDrawContext& context) {
    context.RenderLineElements(true);
    hasDrawRect_ = false;
    for (cHTMLElement* child : children_) child->Layout(context);
    context.RenderLineElements(true);
}
HTML_BLOCK_DRAW(cHTMLHeading)

void cHTMLDD::Layout(cHTMLDrawContext& context) {
    context.RenderLineElements(true);
    context.PushLeftMargin(context.leftMargin + 20);
    context.drawX = context.leftMargin;
    hasDrawRect_ = false;
    for (cHTMLElement* child : children_) child->Layout(context);
    context.RenderLineElements(true);
    context.PopLeftMargin();
}
HTML_BLOCK_DRAW(cHTMLDD)

void cHTMLDT::Layout(cHTMLDrawContext& context) {
    context.RenderLineElements(true);
    hasDrawRect_ = false;
    for (cHTMLElement* child : children_) child->Layout(context);
    context.RenderLineElements(true);
}
HTML_BLOCK_DRAW(cHTMLDT)

void cHTMLList::Layout(cHTMLDrawContext& context) {
    context.RenderLineElements(true);
    context.PushLeftMargin(context.leftMargin + 20);
    context.drawX = context.leftMargin;
    hasDrawRect_ = false;
    for (cHTMLElement* child : children_) child->Layout(context);
    context.RenderLineElements(true);
    context.PopLeftMargin();
}
HTML_BLOCK_DRAW(cHTMLList)

void cHTMLListItem::Layout(cHTMLDrawContext& context) {
    context.RenderLineElements(true);
    context.PushLeftMargin(context.leftMargin);
    hasDrawRect_ = false;
    for (cHTMLElement* child : children_) child->Layout(context);
    context.RenderLineElements(true);
    context.PopLeftMargin();
}
HTML_BLOCK_DRAW(cHTMLListItem)
#undef HTML_BLOCK_DRAW

// ---- leaves ---------------------------------------------------------------------------------------------------------

void cHTMLBR::Layout(cHTMLDrawContext& context) {
    context.RenderLineElements(true);
    cRZRect screen{context.drawX, context.drawY, context.drawX, context.drawY};
    context.ScreenRectToDocumentRect(screen, drawRect_);
    laidOut_ = true;
}

void cHTMLHR::Layout(cHTMLDrawContext& context) {
    context.RenderLineElements(true);
    if (size < 1) size = 1;
    if (width < 0) width = 0;
    int length = width;
    if (widthIsPercent) {
        if (width > 100) width = 100;
        length = (((context.rightMargin - 10) - context.leftMargin) * width) / 100;
    }
    cRZRect screen{context.drawX + 5, context.drawY + 5, length + 5 + context.drawX, size + 5 + context.drawY};
    context.ScreenRectToDocumentRect(screen, drawRect_);
    laidOut_ = true;
    context.IncrementCurrentDrawY(size + 10);
}

void cHTMLHR::Draw(cHTMLDrawContext& context) {
    const Color light = context.graphics->MakeColor(0xa0, 0xa0, 0xa0);
    const Color dark = context.graphics->MakeColor(0x40, 0x40, 0x40);
    if (!laidOut_) Layout(context);
    if (!context.surface->Begin(kSurfaceLines)) return;
    cRZRect r;
    context.DocumentRectToScreenRect(drawRect_, r);
    context.surface->DrawLine(r.left, r.top, r.right, r.top, dark, 1);
    if (size > 1) {
        context.surface->DrawLine(r.left, r.top, r.left, r.bottom, dark, 1);
        context.surface->DrawLine(r.left, r.bottom, r.right, r.bottom, light, 1);
        context.surface->DrawLine(r.right, r.top, r.right, r.bottom, light, 1);
    }
    context.surface->End(kSurfaceLines);
}

// ---- image -----------------------------------------------------------------------------------------------------------

cHTMLIMG::cHTMLIMG() : cHTMLElement(kTypeImg) {}

cHTMLIMG::~cHTMLIMG() {
    if (bitmap) bitmap->Release();
}

bool cHTMLIMG::SetBitmap(IHtmlBitmap* newBitmap) {
    const int oldWidth = width, oldHeight = height;
    bitmap = newBitmap;
    if (newBitmap) {
        newBitmap->AddRef();
        if (width == 0) width = bitmap->Width();
        if (height == 0) height = bitmap->Height();
    }
    return oldHeight != height || oldWidth != width;
}

void cHTMLIMG::GetHeight(int& ascent, int& totalHeight) {
    totalHeight = height;
    ascent = align == 2 ? 0 : (align == 3 ? static_cast<int>(static_cast<unsigned>(height) >> 1) : height);
}

bool cHTMLIMG::CalculateWidthThatFitsInLine(int available, int& used, bool force) {
    if (static_cast<unsigned>(width) <= static_cast<unsigned>(available) || force) {
        used = width;
        return width == available;
    }
    used = 0;
    return true;
}

void cHTMLIMG::Layout(cHTMLDrawContext& context) {
    if (width == 0 || height == 0) {
        laidOut_ = true;
        return;
    }
    if (align == 1) {            // floats to the right margin
        context.RenderLineElements(true);
        cRZRect screen;
        screen.left = std::max(context.rightMargin - width, context.leftMargin);
        screen.top = context.drawY;
        screen.right = width + screen.left;
        screen.bottom = height + screen.top;
        context.ScreenRectToDocumentRect(screen, drawRect_);
        laidOut_ = true;
        context.ChangeRightMargin(screen.left, screen.bottom);
    } else if (align == 0) {     // floats to the left margin
        context.RenderLineElements(true);
        cRZRect screen;
        screen.top = context.drawY;
        screen.left = context.leftMargin;
        screen.right = width + screen.left;
        screen.bottom = height + screen.top;
        context.ScreenRectToDocumentRect(screen, drawRect_);
        laidOut_ = true;
        context.drawX = screen.right;
        context.ChangeLeftMargin(screen.right, screen.bottom);
    } else {
        context.AttachInlineElement(this);
    }
}

void cHTMLIMG::Draw(cHTMLDrawContext& context) {
    if (!laidOut_) Layout(context);
    if (!context.IsObjectVisible(drawRect_, false)) return;
    cRZRect screen;
    context.DocumentRectToScreenRect(drawRect_, screen);
    DrawGivenScreenRect(screen, context);
}

void cHTMLIMG::DrawGivenScreenRect(const cRZRect& screen, cHTMLDrawContext& context) {
    if (!context.surface->Begin(kSurfaceBlit)) return;
    if (!bitmap) {
        context.surface->FillRect(context.backgroundColor, screen, &context.screenRect);
    } else if (width == bitmap->Width() && height == bitmap->Height()) {
        context.surface->Blit(bitmap, bitmap->Bounds(), screen, &context.screenRect);
    } else {
        context.surface->BlitStretched(bitmap, bitmap->Bounds(), screen, &context.screenRect);
    }
    context.surface->End(kSurfaceBlit);
}

bool cHTMLIMG::RenderOneLine(cHTMLDrawContext& context) {
    cRZRect screen;
    screen.left = context.drawX;
    int bottomOffset = height;
    if (align == 2) {
        screen.top = context.drawY;
    } else if (align == 3) {
        screen.top = (context.lineAscent + context.drawY) - static_cast<int>(static_cast<unsigned>(height) >> 1);
    } else {
        screen.top = (context.lineAscent + context.drawY) - height;
    }
    screen.right = screen.left + width;
    screen.bottom = bottomOffset + screen.top;
    context.ScreenRectToDocumentRect(screen, drawRect_);
    laidOut_ = true;
    context.drawX += width;
    return true;
}

// ---- text segment -----------------------------------------------------------------------------------------------------

cHTMLTextSegment::cHTMLTextSegment(IHtmlDocumentServices* document, const cRZString& source,
                                   const tFontAttributes& attributes, int outputStyle)
    : cHTMLElement(kTypeTextSegment), color_(attributes.color), preformatted_(attributes.preformatted),
      isLink_(attributes.isLink), outputStyle_(outputStyle) {
    // character references: "&name;" through the document's entity table, "&#NN;" / "&#xNN;" numerically
    std::string rest = source.str();
    std::string result;
    std::size_t amp = rest.find('&');
    while (amp != std::string::npos) {
        if (amp != 0) {
            result.append(rest, 0, amp);
            rest.erase(0, amp);
        }
        const std::size_t semi = rest.find(';');
        if (semi == std::string::npos) break;
        const std::string name = rest.substr(1, semi - 1);
        std::string replacement;
        if (!cHTMLShared::StringLeftEquals(cRZString(name.c_str()), '#')) {
            replacement = document->GetStringForEscape(cRZString(name.c_str()));
        } else {
            std::size_t skip;
            int base;
            if (cHTMLShared::StringLeftEquals(cRZString(name.c_str()), 2, "#x")) {
                skip = 2;
                base = 16;
            } else {
                const bool upper = cHTMLShared::StringLeftEquals(cRZString(name.c_str()), 2, "#X");
                skip = upper ? 2 : 1;
                base = upper ? 16 : 10;
            }
            AppendLatin1AsUtf8(replacement, static_cast<unsigned>(std::strtoul(name.c_str() + skip, nullptr, base)));
        }
        result += replacement;
        rest.erase(0, semi + 1);
        amp = rest.find('&');
    }
    result += rest;
    text_ = result;
    FixTextSegmentWhitespace(text_, attributes.preformatted);
    font_ = GetFont(document, attributes);
    if (font_) {
        font_->SetStyle(0xffffffffu, false);
        font_->SetStyle(attributes.style, true);
    }
}

cHTMLTextSegment::~cHTMLTextSegment() {
    if (font_) font_->Release();
}

IHtmlFont* cHTMLTextSegment::GetFont(IHtmlDocumentServices* document, const tFontAttributes& attributes) {
    IHtmlFontSystem* fonts = document->FontSystem();
    const std::string& names = attributes.faceNames;
    if (!names.empty()) {
        std::size_t start = 0;
        std::size_t comma = names.find(',');
        for (;;) {
            const std::size_t end = comma < names.size() ? comma : names.size();
            if (IHtmlFont* font = fonts->CreateFont(names.substr(start, end - start), 0, attributes.size, attributes.style, true)) return font;
            if (names.size() <= comma) break;
            comma = names.find(',', end + 1);
            start = end + 1;
        }
        return fonts->CreateFont("", attributes.kind, attributes.size, attributes.style, true);
    }
    std::string face;
    std::uint32_t style = 0;
    if (attributes.kind & 1) {
        document->GetDefaultVariableWidthFontInfo(face, style);
        return fonts->CreateFont(face, 0, attributes.size, attributes.style, true);
    }
    if (attributes.kind & 2) {
        document->GetDefaultFixedWidthFontInfo(face, style);
        return fonts->CreateFont(face, 0, attributes.size, attributes.style, true);
    }
    return fonts->CreateFont("", attributes.kind, attributes.size, attributes.style, true);
}

void cHTMLTextSegment::FixTextSegmentWhitespace(std::string& text, bool preformatted) {
    const std::string source = text;
    const int length = static_cast<int>(source.size());
    text.clear();
    if (!preformatted) {
        int i = 0;
        while (i < length) {
            int next;
            if (!HTMLIsSpace(source, i)) {
                next = HTMLAdvance(source, i);
                while (next < length && !HTMLIsSpace(source, next)) next = HTMLAdvance(source, next);
                text.append(source, static_cast<std::size_t>(i), static_cast<std::size_t>(next - i));
            } else {
                next = HTMLAdvance(source, i);
                while (next < length && HTMLIsSpace(source, next)) next = HTMLAdvance(source, next);
                text.push_back(' ');
            }
            i = next;
        }
        return;
    }
    // preformatted: CR, LF and CRLF all become '\n'
    int start = 0, i = 0;
    while (i < length) {
        const char c = source[static_cast<std::size_t>(i)];
        if (c == '\n') {
            text.append(source, static_cast<std::size_t>(start), static_cast<std::size_t>(i - start));
            text.push_back('\n');
            start = ++i;
        } else if (c == '\r') {
            text.append(source, static_cast<std::size_t>(start), static_cast<std::size_t>(i - start));
            text.push_back('\n');
            int after = i + 1;
            if (after < length && source[static_cast<std::size_t>(after)] == '\n') ++after;
            start = i = after;
        } else {
            ++i;
        }
    }
    text.append(source, static_cast<std::size_t>(start), static_cast<std::size_t>(i - start));
}

int cHTMLTextSegment::Pick(cHTMLElement*& hit, int x, int y) {
    if (!(drawRect_.left <= x && drawRect_.top <= y && x < drawRect_.right && y < drawRect_.bottom)) return 0;
    for (const tRenderAreaInfo& info : renderAreas_) {
        if (info.area.left <= x && info.area.top <= y && x < info.area.right && y < info.area.bottom) {
            if (!Selectable()) return 1;
            hit = this;
            return 2;
        }
    }
    return 0;
}

void cHTMLTextSegment::GetHeight(int& ascent, int& height) {
    height = std::max(font_->LineHeight(), font_->GlyphHeight(0, 0));
    ascent = std::min(height, font_->Ascent());
}

bool cHTMLTextSegment::CalculateWidthThatFitsInLine(int width, int& used, bool force) {
    const int length = static_cast<int>(text_.size());
    const int remaining = length - cursor_;
    const char* start = text_.c_str() + cursor_;
    if (!preformatted_) {
        used = 0;
        fitting_ = font_->CharsThatFit(start, remaining, width, 0, 0);
        if (fitting_ == 0) {
            if (!force) return fitting_ < remaining;
            for (;;) {      // nothing fits: widen until the first word does
                int wordWidth = 0;
                fitting_ = font_->FirstWordLength(start, remaining, width, &wordWidth);
                if (fitting_ != 0) {
                    used = std::min(wordWidth, width);
                    return fitting_ < remaining;
                }
                ++width;
                fitting_ = font_->CharsThatFit(start, remaining, width, 0, 0);
                if (fitting_ != 0) break;
            }
        }
        used = std::min(font_->StringWidth(start, fitting_), width);
    } else {
        // preformatted text breaks only at line feeds
        const std::size_t newline = text_.find('\n', static_cast<std::size_t>(cursor_));
        const int end = newline < text_.size() ? static_cast<int>(newline) + 1 : length;
        used = width;
        fitting_ = end - cursor_;
    }
    return fitting_ < remaining;
}

void cHTMLTextSegment::Layout(cHTMLDrawContext& context) {
    renderAreas_.clear();
    cursor_ = 0;
    hasDrawRect_ = false;
    laidOut_ = false;
    context.AttachInlineElement(this);
}

void cHTMLTextSegment::Draw(cHTMLDrawContext& context) {
    if (!laidOut_) Layout(context);
    for (const tRenderAreaInfo& info : renderAreas_) {
        int x = 0, y = 0;
        context.DocumentPtToScreenPt(info.area.left, info.area.top, x, y);
        font_->DrawText(context.surface, x, y, text_.c_str() + info.start, info.length, color_, 0x8000000, &context.screenRect);
    }
}

bool cHTMLTextSegment::RenderOneLine(cHTMLDrawContext& context) {
    const int length = static_cast<int>(text_.size());
    const int lineAscent = context.lineAscent;
    const int fontAscent = font_->Ascent();
    const int startX = context.drawX;
    const char* line = text_.c_str() + cursor_;
    int trimmed = fitting_;
    // trailing blanks stay out of the drawn rectangle (the previous-character lookup indexes the whole string, like the original)
    while (trimmed > 0) {
        const int previous = trimmed - static_cast<int>(GetPreviousCharLength(text_.c_str() + trimmed, static_cast<unsigned>(trimmed), -2));
        if (line[previous] != ' ') break;
        trimmed = previous;
        if (previous <= 0) break;
    }
    const int visibleWidth = font_->StringWidth(line, trimmed);
    int advance = visibleWidth;
    if (trimmed != fitting_) advance = font_->StringWidth(line, fitting_);
    context.drawX += advance;

    cRZRect screen;
    screen.left = startX;
    screen.top = (lineAscent - fontAscent) + context.drawY;
    screen.right = visibleWidth + startX;
    screen.bottom = context.drawY + context.lineHeight;
    tRenderAreaInfo info;
    context.ScreenRectToDocumentRect(screen, info.area);
    AddDrawRect(info.area);
    info.start = cursor_;
    info.length = fitting_;
    renderAreas_.push_back(info);
    cursor_ += fitting_;
    if (cursor_ < length) return false;
    laidOut_ = true;
    return true;
}

bool cHTMLTextSegment::SkipWhiteSpaces() {
    if (preformatted_) return false;
    const int length = static_cast<int>(text_.size());
    for (int i = cursor_; i < length; ++i) {
        if (text_[static_cast<std::size_t>(i)] != ' ') return false;
        cursor_ = i + 1;
    }
    laidOut_ = true;
    return true;
}

// ---- tables ----------------------------------------------------------------------------------------------------------------

void cHTMLTD::SetBGCOLOR(const cRZString& value, IHtmlGraphics* g) {
    backgroundColor = ColorFromAttribute(value, g);
    hasBackground = true;
}

void cHTMLTD::ExpandDrawRect(cHTMLDrawContext& context) {
    if (width.unit != tLength::kPixels) return;
    if (!hasDrawRect_) {
        cRZRect screen{context.drawX, context.drawY, context.drawX + 1, context.drawY + 1};
        context.ScreenRectToDocumentRect(screen, drawRect_);
        hasDrawRect_ = true;
    }
    if (static_cast<std::uint32_t>(drawRect_.right - drawRect_.left) < width.value)
        drawRect_.right = static_cast<std::int32_t>(width.value) + drawRect_.left;
}

void cHTMLTD::Layout(cHTMLDrawContext& outer) {
    cHTMLDrawContext context = outer;     // a cell lays out on a private copy of the cursor and margins
    hasDrawRect_ = false;
    for (cHTMLElement* child : children_) child->Layout(context);
    context.RenderLineElements(true);
    PostLayoutContainer();
    hasDrawRect_ = false;
    laidOut_ = false;
    drawRect_ = cRZRect{};
    for (cHTMLElement* child : children_)
        if (child->laidOut()) AddDrawRect(child->drawRect());
    ExpandDrawRect(context);
    laidOut_ = true;
}

void cHTMLTD::Layout(cHTMLDrawContext& context, const cRZRect& cell) {
    hasDrawRect_ = false;
    for (cHTMLElement* child : children_) child->Layout(context);
    context.RenderLineElements(true);
    PostLayoutContainer();
    hasDrawRect_ = true;
    context.ScreenRectToDocumentRect(cell, drawRect_);
    ExpandDrawRect(context);
    laidOut_ = true;
}

void cHTMLTD::Draw(cHTMLDrawContext& outer) {
    cRZRect screen;
    outer.DocumentRectToScreenRect(drawRect_, screen);
    if (hasBackground && outer.surface->Begin(kSurfaceFill)) {
        outer.surface->FillRect(backgroundColor, screen, nullptr);
        outer.surface->End(kSurfaceFill);
    }
    cHTMLDrawContext context = outer;
    context.ChangeLeftMargin(screen.left, kNoPopY);
    context.ChangeRightMargin(screen.right, kNoPopY);
    DrawContainer(context);
    context.RenderLineElements(true);
}

void cHTMLTR::SetBGCOLOR(const cRZString& value, IHtmlGraphics* g) {
    backgroundColor = ColorFromAttribute(value, g);
    hasBackground = true;
}

// Row layout. First pass (placeCells == false): every cell lays out on the full table width and the column
// widths are measured (min = widest unbreakable content, max = widest content, fixed pixel widths tracked
// separately). Later passes (placeCells == true): cells are laid out inside their columns; with `finalPass` they
// are fitted to the row rectangle.
void cHTMLTR::Layout(cHTMLDrawContext& context, cHTMLTABLE& table, int row, bool placeCells, bool finalPass) {
    if (placeCells) {
        const int originX = context.screenRect.left - context.documentRect.left;
        const int rowTop = table.rowTops[static_cast<std::size_t>(row)];
        const int rowBottomScreen = (context.screenRect.top - context.documentRect.top) + table.rowTops[static_cast<std::size_t>(row) + 1];
        hasDrawRect_ = false;
        const int rowTopScreen = (rowTop + context.screenRect.top) - context.documentRect.top;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            cHTMLTD* cell = cells[i];
            const int x0 = table.columns[i].x + originX;
            const int x1 = table.columns[i + 1].x + originX;
            cHTMLDrawContext cellContext = context;
            cellContext.ChangeLeftMargin(x0, kNoPopY);
            cellContext.ChangeRightMargin(x1, kNoPopY);
            cellContext.drawX = x0;
            cellContext.drawY = rowTopScreen;
            if (!finalPass) {
                cell->Layout(cellContext);
            } else {
                cell->Layout(cellContext, cRZRect{x0, rowTopScreen, x1, rowBottomScreen});
            }
            std::int32_t& height = table.rowHeights[static_cast<std::size_t>(row)];
            height = std::max(height, cell->drawRect().bottom - cell->drawRect().top);
            const int cellWidth = cell->drawRect().right - cell->drawRect().left;
            if (x1 - x0 < cellWidth) {
                cHTMLTABLE::ColumnInfo& column = table.columns[i];
                column.minWidth = std::max(column.minWidth, cellWidth);
                column.maxWidth = std::max(column.maxWidth, column.minWidth);
            }
            AddDrawRect(cell->drawRect());
        }
        laidOut_ = true;
        return;
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
        cHTMLTD* cell = cells[i];
        cHTMLDrawContext cellContext = context;
        cHTMLTABLE::ColumnInfo& column = table.columns[i];
        if (cell->width.unit == tLength::kPixels) {
            const int pixels = static_cast<int>(cell->width.value);
            const int originX = context.screenRect.left - context.documentRect.left;
            column.fixedMin = std::max(column.fixedMin, pixels);
            column.availableMax = std::min(column.availableMax, pixels);
            cellContext.ChangeLeftMargin(originX, kNoPopY);
            cellContext.ChangeRightMargin(pixels + originX, kNoPopY);
            cellContext.drawX = originX;
        }
        cell->Layout(cellContext);
        std::int32_t& height = table.rowHeights[static_cast<std::size_t>(row)];
        height = std::max(height, cell->drawRect().bottom - cell->drawRect().top);
        const int cellWidth = cell->drawRect().right - cell->drawRect().left;
        column.maxWidth = std::max(column.maxWidth, cellWidth);
        column.maxWidth = std::max(column.maxWidth, column.minWidth);
        if (cell->width.unit == tLength::kPixels) {
            column.minWidth = std::max(column.minWidth, cellWidth);
            column.maxWidth = std::max(column.maxWidth, column.minWidth);
        }
    }
}

void cHTMLTABLE::SetBGCOLOR(const cRZString& value, IHtmlGraphics* g) {
    backgroundColor = ColorFromAttribute(value, g);
    hasBackground = true;
}

void cHTMLTABLE::Draw(cHTMLDrawContext& context) {
    if (!laidOut_) Layout(context);
    DrawContainer(context);
}

void cHTMLTABLE::Layout(cHTMLDrawContext& context) {
    context.RenderLineElements(true);
    const std::size_t rowCount = rows.size();
    std::size_t columnCount = 0;
    for (cHTMLTR* row : rows) columnCount = std::max(columnCount, row->cells.size());
    const int originScreenX = context.screenRect.left;
    const int originDocumentX = context.documentRect.left;
    const int startX = context.drawX;
    const int top = (context.documentRect.top + context.drawY) - context.screenRect.top;

    rowHeights.assign(rowCount, 0);
    rowTops.assign(rowCount + 1, 0);
    columns.assign(columnCount + 1, ColumnInfo{});
    for (std::size_t c = 0; c < columnCount; ++c) {
        columns[c] = ColumnInfo{};
        columns[c].availableMax = context.rightMargin - context.leftMargin;
    }
    for (std::size_t r = 0; r < rowCount; ++r) rows[r]->Layout(context, *this, static_cast<int>(r), false, false);

    const int firstColumnX = (originDocumentX + startX) - originScreenX;
    for (;;) {
        int x = firstColumnX;
        int available = context.rightMargin - context.leftMargin;
        int totalMin = 0, totalSpread = 0;
        for (std::size_t c = 0; c < columnCount; ++c) {
            ColumnInfo& column = columns[c];
            const int spread = column.maxWidth - column.minWidth;
            totalSpread += spread;
            totalMin += column.minWidth;
            column.spread = spread;
            column.finalWidth = column.minWidth;
        }
        if (columnCount > 0) {
            // hand the free width out to the columns in proportion to how much they may grow
            int extra = std::max(available - totalMin, 0);
            int remainingSpread = totalSpread;
            for (std::size_t c = 0; c < columnCount; ++c) {
                ColumnInfo& column = columns[c];
                int share = 0;
                if (column.spread != 0) {
                    share = (remainingSpread - 1 + column.spread * extra) / remainingSpread;
                    remainingSpread -= column.spread;
                    extra -= share;
                }
                column.x = x;
                x += share + column.minWidth;
            }
        }
        columns[columnCount].x = x;
        rowTops[0] = top;
        for (std::size_t r = 0; r < rowCount; ++r) rowTops[r + 1] = rowTops[r] + rowHeights[r];
        if (totalSpread == 0 || available < totalMin) break;
        for (std::size_t r = 0; r < rowCount; ++r) rows[r]->Layout(context, *this, static_cast<int>(r), true, false);
        if (columnCount < 1) break;
        bool stable = true;     // a column that grew beyond its allotted width needs another round
        for (std::size_t c = 0; c < columnCount; ++c) {
            if (!(columns[c].minWidth <= columns[c].finalWidth)) { stable = false; break; }
        }
        if (stable) break;
    }

    rowHeights.assign(rowCount, 0);
    for (std::size_t r = 0; r < rowCount; ++r) rows[r]->Layout(context, *this, static_cast<int>(r), true, false);
    rowTops.clear();
    int y = top;
    for (std::size_t r = 0; r <= rowCount; ++r) {
        rowTops.push_back(y);
        if (r < rowCount) y += rowHeights[r];
    }
    for (std::size_t r = 0; r < rowCount; ++r) {
        context.drawY = (rowTops[r] - context.documentRect.top) + context.screenRect.top;
        rows[r]->Layout(context, *this, static_cast<int>(r), true, true);
    }
    context.drawY = (rowTops[rowCount] - context.documentRect.top) + context.screenRect.top;
    hasDrawRect_ = true;
    drawRect_.left = columns[0].x;
    drawRect_.top = rowTops[0];
    drawRect_.right = columns[columnCount].x;
    drawRect_.bottom = rowTops[rowCount];
    laidOut_ = true;
}

}  // namespace html

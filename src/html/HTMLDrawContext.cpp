#include "html/HTMLDrawContext.h"

#include <algorithm>

namespace html {

void cHTMLDrawContext::AttachInlineElement(cHTMLInlineElement* element) {
    // a change of alignment style starts a new line
    if (!inlineElements.empty() && inlineElements.back()->OutputStyle() != element->OutputStyle()) RenderLineElements(true);
    inlineElements.push_back(element);
    RenderLineElements(false);
}

void cHTMLDrawContext::RenderLineElements(bool force) {
    while (!inlineElements.empty()) {
        // blanks at the start of a line are dropped
        auto it = inlineElements.begin();
        while (it != inlineElements.end() && (*it)->SkipWhiteSpaces()) it = inlineElements.erase(it);
        if (inlineElements.empty()) return;

        int remaining = 0;
        if (!ComputeCurrentLineHeightAndWidth(remaining) && !force) return;
        int slack = remaining < 0 ? 0 : remaining;
        // the line's style is the last queued element's
        switch (inlineElements.back()->OutputStyle()) {
            case 2: drawX = slack + leftMargin; break;
            case 1: drawX = (slack >> 1) + leftMargin; break;
            case 0: drawX = leftMargin; break;
            default: break;
        }
        for (it = inlineElements.begin(); it != inlineElements.end();) {
            const bool finished = (*it)->RenderOneLine(*this);
            it = finished ? inlineElements.erase(it) : std::next(it);
            if (!(drawX < rightMargin)) break;
        }
        if (lineHeight > 0) {
            IncrementCurrentDrawY(lineHeight);
            drawX = leftMargin;
            lineHeight = 0;
            lineAscent = 0;
        }
    }
}

// Measures the elements queued for the line: tallest ascent / height, and how much of the line width is left.
// True when an element is too wide for the rest of the line (the line is complete).
bool cHTMLDrawContext::ComputeCurrentLineHeightAndWidth(int& remainingWidth) {
    remainingWidth = rightMargin - leftMargin;
    for (auto it = inlineElements.begin(); it != inlineElements.end(); ++it) {
        cHTMLInlineElement* element = *it;
        int ascent = 0, height = 0;
        element->GetHeight(ascent, height);
        lineAscent = std::max(lineAscent, ascent);
        lineHeight = std::max(lineHeight, height);
        int used = 0;
        bool full = element->CalculateWidthThatFitsInLine(remainingWidth, used, false);
        const int before = remainingWidth;
        remainingWidth -= used;
        // Nothing fits on a fresh line (no floating image beside it): force the first piece onto it.
        if (used == 0 && full && (leftMargins.empty() || leftMargins.back().popAtY > 0x3ffffffe) &&
            (rightMargins.empty() || rightMargins.back().popAtY > 0x3ffffffe) && it == inlineElements.begin()) {
            full = element->CalculateWidthThatFitsInLine(before - used, used, true);
            remainingWidth -= used;
        }
        if (full) return true;
    }
    return false;
}

void cHTMLDrawContext::ChangeLeftMargin(int x, int popAtY) {
    leftMargins.push_back({true, popAtY, leftMargin, hangingLeftMargin});
    leftMargin = x;
    hangingLeftMargin = x;
}

void cHTMLDrawContext::ChangeRightMargin(int x, int popAtY) {
    rightMargins.push_back({popAtY, rightMargin});
    rightMargin = x;
}

void cHTMLDrawContext::PushLeftMargin(int x) {
    leftMargins.push_back({false, 0, leftMargin, hangingLeftMargin});
    leftMargin = x;
    hangingLeftMargin = x;
}

void cHTMLDrawContext::PopLeftMargin() {
    if (leftMargins.empty()) return;
    leftMargin = leftMargins.back().oldLeft;
    hangingLeftMargin = leftMargins.back().oldIndent;
    leftMargins.pop_back();
}

void cHTMLDrawContext::BeginHangingIndent() {
    int width = 0;
    ComputeCurrentLineHeightAndWidth(width);
    hangingLeftMargin = rightMargin - width;
}

void cHTMLDrawContext::IncrementCurrentDrawY(int dy) {
    drawY += dy;
    lineHeight = 0;
    lineAscent = 0;
    leftMargin = hangingLeftMargin;
    while (!leftMargins.empty() && leftMargins.back().autoPop && leftMargins.back().popAtY <= drawY) {
        leftMargin = leftMargins.back().oldLeft;
        hangingLeftMargin = leftMargins.back().oldIndent;
        leftMargins.pop_back();
    }
    while (!rightMargins.empty() && rightMargins.back().popAtY <= drawY) {
        rightMargin = rightMargins.back().oldRight;
        rightMargins.pop_back();
    }
}

void cHTMLDrawContext::InitForFreshPainting() {
    lineHeight = 0;
    lineAscent = 0;
    drawX = screenRect.left;
    drawY = screenRect.top - documentRect.top;
    leftMargin = hangingLeftMargin = screenRect.left - documentRect.left;
    rightMargin = screenRect.right - documentRect.left;
}

// Does the rectangle (document coordinates) intersect the viewport? With `strict` it must lie completely inside
// it, or cover it entirely.
bool cHTMLDrawContext::IsObjectVisible(const cRZRect& rect, bool strict) const {
    const cRZRect& view = documentRect;
    if (!(view.left < rect.right && rect.left < view.right && view.top < rect.bottom && rect.top < view.bottom)) return false;
    cRZRect clipped;
    clipped.left = std::max(view.left, rect.left);
    clipped.top = std::max(view.top, rect.top);
    clipped.right = std::min(view.right, rect.right);
    clipped.bottom = std::min(view.bottom, rect.bottom);
    if (!strict) return true;
    if (clipped.left == rect.left && clipped.top == rect.top && clipped.right == rect.right && clipped.bottom == rect.bottom) return true;
    return clipped.left == view.left && clipped.top == view.top && clipped.right == view.right && clipped.bottom == view.bottom;
}

void cHTMLDrawContext::DocumentRectToScreenRect(const cRZRect& document, cRZRect& screen) const {
    screen.left = document.left - documentRect.left + screenRect.left;
    screen.right = document.right - documentRect.left + screenRect.left;
    screen.top = document.top - documentRect.top + screenRect.top;
    screen.bottom = document.bottom - documentRect.top + screenRect.top;
}

void cHTMLDrawContext::ScreenRectToDocumentRect(const cRZRect& screen, cRZRect& document) const {
    document.left = screen.left - screenRect.left + documentRect.left;
    document.right = screen.right - screenRect.left + documentRect.left;
    document.top = screen.top - screenRect.top + documentRect.top;
    document.bottom = screen.bottom - screenRect.top + documentRect.top;
}

void cHTMLDrawContext::DocumentPtToScreenPt(int x, int y, int& screenX, int& screenY) const {
    screenX = x - documentRect.left + screenRect.left;
    screenY = y - documentRect.top + screenRect.top;
}

}  // namespace html

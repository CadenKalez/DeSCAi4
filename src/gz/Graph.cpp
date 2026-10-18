#include "gz/Graph.h"

#include <cmath>
#include <cstdio>

namespace gz {

namespace {
IGraphHost* g_host = nullptr;
constexpr std::uint32_t kLockFill = 0x8001, kLockLine = 0x8004, kLockOutline = 0x8008, kLockBlt = 0x8010,
                        kLockPixelsWrite = 0x8020, kLockPixelsRead = 0x8040;
}  // namespace

IGraphHost* GraphHost() { return g_host; }
void SetGraphHost(IGraphHost* host) { g_host = host; }

// ---------------------------------------------------------------------------------------------------------------
// The free outline helper and the overlay objects

bool DrawRectangleOutline(cIGZBuffer* buffer, const cRZRect& r, std::uint32_t color, std::uint32_t thickness) {   // @ 0x1002b3deb
    const int t = static_cast<int>(thickness);
    if (buffer->DrawLine(r.left, r.top, r.right - 1, r.top, color, thickness) &&
        buffer->DrawLine(r.left, r.bottom - t, r.right - 1, r.bottom - t, color, thickness) &&
        buffer->DrawLine(r.right - t, r.top, r.right - t, r.bottom - 1, color, thickness)) {
        return buffer->DrawLine(r.left, r.top, r.left, r.bottom - 1, color, thickness);
    }
    return false;
}

void MiscGraphLine::DrawSelf(cIGZGraph* graph, cIGZBuffer* buffer) {
    if (buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockLine))) {
        graph->DrawLine(buffer, x0, y0, x1, y1, static_cast<int>(color), thickness);
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockLine));
    }
}

void MiscGraphRect::DrawSelf(cIGZGraph*, cIGZBuffer* buffer) {
    if (fill && buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockFill))) {
        buffer->Fill(fillColor, &rect);
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockFill));
    }
    if (outline && buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockOutline))) {
        gz::DrawRectangleOutline(buffer, rect, outlineColor, outlineThickness);
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockOutline));
    }
}

void MiscGraphArrow::DrawSelf(cIGZGraph* graph, cIGZBuffer* buffer) {
    if (buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockLine))) {
        graph->DrawLine(buffer, x0, y0, x1, y1, static_cast<int>(color), thickness);
        // a degenerate arrow is stretched to one pixel afterwards (the head drawing was never finished)
        if ((startHead || endHead) && x0 == x1 && y0 == y1) x1 = x0 + 1;
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockLine));
    }
}

MiscGraphPicture::~MiscGraphPicture() {
    if (picture) { picture->Release(); picture = nullptr; }
}

void MiscGraphPicture::SetBuffer(cIGZBuffer* buffer) {
    if (buffer) buffer->AddRef();
    if (picture) picture->Release();
    picture = buffer;
}

void MiscGraphPicture::DrawSelf(cIGZGraph* graph, cIGZBuffer* buffer) {
    const cRZRect* bounds = picture->GetBufferArea();
    const cRZRect source = *bounds;
    cRZRect destination{x, y, (source.right + x) - source.left, (source.bottom + y) - source.top};
    if (graph != nullptr && relativeToGraphArea) {
        cRZRect area;
        graph->GetGraphArea(area);
        destination.left += area.left; destination.top += area.top;
        destination.right += area.left; destination.bottom += area.top;
    }
    if (buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockBlt))) {
        graph->Blt(picture, buffer, source, destination);
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockBlt));
    }
}

MiscGraphTextInfo::~MiscGraphTextInfo() {
    if (font) { font->Release(); font = nullptr; }
}

bool MiscGraphTextInfo::Init() {
    if (font == nullptr) {
        if (IGraphHost* host = GraphHost(); host && host->FontsAvailable()) {
            font = host->CreateFont("arial", 12);
            if (font == nullptr) font = host->CreateFont("helvetica", 12);
        }
    }
    return font != nullptr;
}

void MiscGraphTextInfo::SetFont(cIGZFont* newFont, std::uint32_t color) {
    if (font != newFont) {
        if (newFont) newFont->AddRef();
        if (font) font->Release();
        font = newFont;
    }
    if (fontColor != kAutoColor) fontColor = color;   // sic: the stored colour is only replaced when it is not "auto"
}

void MiscGraphTextInfo::DrawSelf(cIGZGraph*, cIGZBuffer* buffer) {
    if (fill && buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockFill))) {
        buffer->Fill(fillColor, &rect);
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockFill));
    }
    if (outline && buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockOutline))) {
        gz::DrawRectangleOutline(buffer, rect, outlineColor, 1);
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockOutline));
    }
    if (font) {
        const std::uint32_t color = fontColor == kAutoColor ? kFontDefaultColor : fontColor;
        font->DrawTextParagraph(buffer, rect, text.Data(), text.Strlen(), static_cast<int>(textAlignment), color,
                                kFontDefaultColor, nullptr, nullptr);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// cGZGraph

cGZGraph::cGZGraph(cIGZWinMgr* mgr) : cGZWin(mgr) {   // @ 0x1003780d0
    if (IGraphHost* host = GraphHost()) keyColor_ = host->MakeNativeColor(0xff, 0, 0xff);
}

cGZGraph::~cGZGraph() {   // @ 0x1003783c6
    cGZGraph::Shutdown();
    dynamicObjects_.clear();
}

bool cGZGraph::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x100378678
    if (iid == kGZIID_cIGZGraph) {
        *out = static_cast<cIGZGraph*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cGZWinAlt || iid == 1 || iid == kGZIID_cIGZWin) {
        *out = static_cast<cIGZWin*>(this);
        AddRef();
        return true;
    }
    return false;
}

bool cGZGraph::Init() {   // @ 0x1003784e2
    if (get_init_flag()) return true;
    SetFlag(kWinPrivateBuffer, true);
    return cGZWin::Init();
}

void cGZGraph::ReplaceFont(cIGZFont*& slot, cIGZFont* font) {
    if (slot != font) {
        if (font) font->AddRef();
        if (slot) slot->Release();
        slot = font;
    }
}

void cGZGraph::ReplacePattern(cIGZBuffer*& slot, cIGZBuffer* pattern) {
    if (pattern) pattern->AddRef();
    if (slot) slot->Release();
    slot = pattern;
}

bool cGZGraph::Shutdown() {   // @ 0x100378522
    if (!dynamicObjects_.empty()) {
        for (DynamicGraphObject* object : dynamicObjects_) delete object;
        dynamicObjects_.clear();
    }
    auto drop = [](auto*& slot) { if (slot) { slot->Release(); slot = nullptr; } };
    drop(titleFont_);
    drop(backgroundPattern_);
    drop(areaPattern_);
    for (int axis = 0; axis < 2; ++axis) {
        drop(labelFont_[axis]);
        drop(tickFont_[axis]);
    }
    delete[] hitData_;
    hitData_ = nullptr;
    fontsLoaded_ = false;
    return cGZWin::Shutdown();
}

void cGZGraph::LoadDefaultFonts() {   // @ 0x1003786c8
    IGraphHost* host = GraphHost();
    auto make = [host](std::uint32_t size) -> cIGZFont* {
        cIGZFont* font = host->CreateFont("arial", size);
        if (font == nullptr) font = host->CreateFont("helvetica", size);
        return font;
    };
    if (titleFont_ == nullptr && host && host->FontsAvailable()) {
        if (cIGZFont* font = make(24)) {
            SetTitleFont(font, kAutoColor);
            font->Release();
        }
    }
    for (int axis = 0; axis < 2; ++axis) {
        if (labelFont_[axis] == nullptr && host && host->FontsAvailable()) {
            if (cIGZFont* font = make(16)) {
                SetAxisLabelFont(static_cast<Axis>(axis), font, kAutoColor);
                font->Release();
            }
        }
        if (tickFont_[axis] == nullptr && host && host->FontsAvailable()) {
            if (cIGZFont* font = make(14)) {
                SetAxisTickFont(static_cast<Axis>(axis), font, kAutoColor);
                font->Release();
            }
        }
    }
    fontsLoaded_ = true;
}

bool cGZGraph::GZPaint() { CompleteRedraw(nullptr); return true; }   // @ 0x100378a7c

bool cGZGraph::GZOnMouseDownL(int, int, std::uint32_t) { GZSetCapture(); return true; }   // @ 0x100378a90

bool cGZGraph::GZOnMouseUpL(int, int, std::uint32_t) {   // @ 0x100378a9e
    if (GZAmCapturing()) GZReleaseCapture();
    return true;
}

bool cGZGraph::GZOnMouseMove(int x, int y, std::uint32_t) {   // @ 0x100378ac2
    if (GZAmCapturing()) IsPointInWindowWindowCoordinates(x, y);   // result unused in the binary
    return true;
}

void cGZGraph::SetArea(int l, int t, int r, int b) {   // @ 0x100378afe
    cGZWin::SetArea(l, t, r, b);
    InvalidateSelf();
}

void cGZGraph::SetBackgroundPattern(cIGZBuffer* pattern) { ReplacePattern(backgroundPattern_, pattern); }
void cGZGraph::SetGraphBackgroundPattern(cIGZBuffer* pattern) { ReplacePattern(areaPattern_, pattern); }

void cGZGraph::SetAxisLabel(Axis axis, const cRZString& label, bool autoPosition) {   // @ 0x100378d80
    axisLabel_[axis].AssignFromIGZString(label);
    if (autoPosition) labelArea_[axis].left = kAutoRect;
}

void cGZGraph::SetAxisLabelFont(Axis axis, cIGZFont* font, std::uint32_t color) {   // @ 0x100378e60
    ReplaceFont(labelFont_[axis], font);
    if (color != kAutoColor) labelColor_[axis] = color;
}

void cGZGraph::SetAxisTickFont(Axis axis, cIGZFont* font, std::uint32_t color) {   // @ 0x1003790d2
    ReplaceFont(tickFont_[axis], font);
    if (color != kAutoColor) tickFontColor_[axis] = color;
}

void cGZGraph::SetTitleFont(cIGZFont* font, std::uint32_t color) {   // @ 0x100379366
    ReplaceFont(titleFont_, font);
    if (color != kAutoColor) titleColor_ = color;
}

void cGZGraph::SetTitle(const cRZString& title, bool autoPosition) {   // @ 0x1003792cc
    title_.AssignFromIGZString(title);
    if (autoPosition) titleArea_.left = kAutoRect;
}

void cGZGraph::SetMajorTickDecimalPlaces(Axis axis, int places) {   // @ 0x10037900e
    std::uint8_t value = static_cast<std::uint8_t>(places);
    if (places > 8) value = 9;
    if (places < 1) value = 0;
    decimals_[axis] = value;
}

void cGZGraph::SetMajorTickDivisions(Axis axis, std::uint32_t divisions, bool minPixels) {   // @ 0x100378f5e
    float spacing = (max_[axis] - min_[axis]) / static_cast<float>(divisions);
    if (minPixels) {
        const int pixels = ValueAmountToPixelAmount(spacing, axis);
        spacing = PixelAmountToValueAmount(axis, pixels < 4 ? 4 : pixels);
    }
    majorSpacing_[axis] = spacing;
}

void cGZGraph::SetMinorTickDivisions(Axis axis, std::uint32_t divisions, bool minPixels) {   // @ 0x100379198
    float spacing = majorSpacing_[axis] / static_cast<float>(divisions);
    if (minPixels && ValueAmountToPixelAmount(spacing, axis) < 2) {
        spacing = PixelAmountToValueAmount(axis, 2);
        if (majorSpacing_[axis] * 0.5f < spacing) minorTicks_[axis] = false;
    }
    minorSpacing_[axis] = spacing;
}

int cGZGraph::GetMaxTickLengthForAxis(Axis axis) {   // @ 0x10037906e
    int length = 0;
    if (majorTicks_[axis] && majorLength_[axis] >= 0) length = majorLength_[axis];
    if (minorTicks_[axis] && length < minorLength_[axis]) length = minorLength_[axis];
    return length;
}

MiscGraphTextInfo* cGZGraph::AddMiscGraphTextInfo() {   // @ 0x10037957a
    auto* object = new MiscGraphTextInfo();
    AddDynamicGraphObject(object);
    return object;
}
MiscGraphLine* cGZGraph::AddMiscGraphLine() {   // @ 0x100379610
    auto* object = new MiscGraphLine();
    AddDynamicGraphObject(object);
    return object;
}
MiscGraphRect* cGZGraph::AddMiscGraphRect() {   // @ 0x1003796b2
    auto* object = new MiscGraphRect();
    AddDynamicGraphObject(object);
    return object;
}
MiscGraphArrow* cGZGraph::AddMiscGraphArrow() {   // @ 0x100379762
    auto* object = new MiscGraphArrow();
    AddDynamicGraphObject(object);
    return object;
}
MiscGraphPicture* cGZGraph::AddMiscGraphPicture() {   // @ 0x100379820
    auto* object = new MiscGraphPicture();
    AddDynamicGraphObject(object);
    return object;
}

bool cGZGraph::AllocateHitTestData(std::uint32_t size) {   // @ 0x10037bab0
    // sic: the old buffer is only freed when it was too small, but a new one is always allocated
    if (hitDataSize_ < size && hitData_) delete[] hitData_;
    hitDataSize_ = size;
    hitData_ = new std::uint8_t[size];
    return true;
}

bool cGZGraph::DrawLine(cIGZBuffer* buffer, int x0, int y0, int x1, int y1, int color, std::uint32_t thickness) {   // @ 0x10037bb06
    bool lockedHere = false;
    if (!buffer->IsLocked()) {
        if (!buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockLine))) return false;
        lockedHere = true;
    }
    const bool result = buffer->DrawLine(x0, y0, x1, y1, color, thickness);
    if (lockedHere) buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockLine));
    return result;
}

void cGZGraph::DrawRectangleOutline(cIGZBuffer* buffer, const cRZRect& rect, std::uint32_t color, std::uint32_t thickness) {   // @ 0x10037bba8
    gz::DrawRectangleOutline(buffer, rect, color, thickness);
}

bool cGZGraph::Fill(cIGZBuffer* buffer, std::uint32_t color, const cRZRect& rect, float) {   // @ 0x10037bbd4
    bool lockedHere = false;
    if (!buffer->IsLocked()) {
        if (!buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockFill))) return false;
        lockedHere = true;
    }
    const cRZRect copy = rect;
    const bool result = buffer->Fill(color, copy, nullptr);
    if (lockedHere) buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockFill));
    return result;
}

bool cGZGraph::Blt(cIGZBuffer* source, cIGZBuffer* destination, const cRZRect& sourceRect, const cRZRect& destRect) {   // @ 0x10037bc68
    bool lockedHere = false;
    if (!destination->IsLocked()) {
        if (!destination->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockBlt))) return false;
        lockedHere = true;
    }
    const bool result = destination->Blt(source, sourceRect, destRect, nullptr);
    if (lockedHere) destination->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockBlt));
    return result;
}

// ---------------------------------------------------------------------------------------------------------------
// Value <-> pixel conversion

std::int32_t cGZGraph::TruncToInt(float value) {
    // cvttss2si: out of range and NaN give INT_MIN
    if (!(value > -2147483904.0f && value < 2147483648.0f)) return INT_MIN;
    return static_cast<std::int32_t>(value);
}

int cGZGraph::ValueToPixel(float value, Axis axis, bool clamp) {   // @ 0x10037b83c
    int pixel, low, high;
    if (axis == kAxisVertical) {
        low = graphArea_.top; high = graphArea_.bottom;
        pixel = zeroPosition_[1] - TruncToInt(value / ((max_[1] - min_[1]) / static_cast<float>(high - low)));
    } else if (axis == kAxisHorizontal) {
        low = graphArea_.left; high = graphArea_.right;
        pixel = TruncToInt(value / ((max_[0] - min_[0]) / static_cast<float>(high - low))) + zeroPosition_[0];
    } else {
        return 0;
    }
    if (clamp) {
        if (pixel < low) pixel = low;
        if (high < pixel) pixel = high;
    }
    return pixel;
}

float cGZGraph::PixelToValue(Axis axis, int pixel, bool clamp) {   // @ 0x10037b8cc
    float high, low, scale;
    if (axis == kAxisVertical) {
        high = max_[1]; low = min_[1];
        scale = (high - low) / static_cast<float>(graphArea_.bottom - graphArea_.top);
        pixel = zeroPosition_[1] - pixel;
    } else if (axis == kAxisHorizontal) {
        high = max_[0]; low = min_[0];
        scale = (high - low) / static_cast<float>(graphArea_.right - graphArea_.left);
        pixel = pixel - zeroPosition_[0];
    } else {
        return 0.0f;
    }
    float value = static_cast<float>(pixel) * scale;
    if (clamp) {
        if (!(low <= value)) value = low;
        if (high < value) value = high;
    }
    return value;
}

int cGZGraph::ValueAmountToPixelAmount(float amount, Axis axis) {   // @ 0x10037b970
    float scale = 0.0f;
    if (axis == kAxisVertical) scale = (max_[1] - min_[1]) / static_cast<float>(graphArea_.bottom - graphArea_.top);
    else if (axis == kAxisHorizontal) scale = (max_[0] - min_[0]) / static_cast<float>(graphArea_.right - graphArea_.left);
    return TruncToInt(amount / scale);
}

float cGZGraph::PixelAmountToValueAmount(Axis axis, int pixels) {   // @ 0x10037b9d4
    float scale = 0.0f;
    if (axis == kAxisVertical) scale = (max_[1] - min_[1]) / static_cast<float>(graphArea_.bottom - graphArea_.top);
    else if (axis == kAxisHorizontal) scale = (max_[0] - min_[0]) / static_cast<float>(graphArea_.right - graphArea_.left);
    return static_cast<float>(pixels) * scale;
}

int cGZGraph::WindowPixelToGraphPixel(Axis axis, int pixel) {   // @ 0x10037ba54
    if (axis == kAxisVertical) return pixel - graphArea_.top;
    if (axis == kAxisHorizontal) return pixel - graphArea_.left;
    return 0;
}

int cGZGraph::GraphPixelToWindowPixel(Axis axis, int pixel) {   // @ 0x10037ba7a
    if (axis == kAxisVertical) return pixel + graphArea_.top;
    if (axis == kAxisHorizontal) return pixel + graphArea_.left;
    return 0;
}

void cGZGraph::CalculateAxisZeroPosition(Axis axis) {   // @ 0x10037a14c
    int position;
    if (min_[axis] == 0.0f) {
        position = axis == kAxisHorizontal ? graphArea_.left : graphArea_.bottom;
    } else if (max_[axis] == 0.0f) {
        position = axis == kAxisHorizontal ? graphArea_.right : graphArea_.top;
    } else {
        // the zero line lies `max` units from the high end of the axis
        const int pixels = ValueAmountToPixelAmount(max_[axis], axis);
        position = axis == kAxisHorizontal ? graphArea_.right - pixels : pixels + graphArea_.top;
    }
    zeroPosition_[axis] = position;
}

// ---------------------------------------------------------------------------------------------------------------
// Automatic layout

std::string cGZGraph::FormatTick(float value, std::uint8_t decimals) {
    const char format[5] = {'%', '.', static_cast<char>(decimals + '0'), 'f', 0};
    char text[512];
    std::snprintf(text, sizeof(text), format, static_cast<double>(value));
    return text;
}

void cGZGraph::CommitDefaultValues() {   // @ 0x1003798c2
    for (int axis = 0; axis < 2; ++axis) {
        bool minWasAuto = false;
        if (min_[axis] == kAutoValue) {
            GetAutomaticAxisMinValue(static_cast<Axis>(axis), min_[axis]);
            minWasAuto = true;
        }
        float high = max_[axis];
        if (high == kAutoValue) {
            GetAutomaticAxisMaxValue(static_cast<Axis>(axis), max_[axis]);
            high = max_[axis];
            if (minWasAuto && min_[axis] == high) {
                high = high != 0.0f ? high / 0.9f : 1.0f;
                max_[axis] = high;
            }
        }
        if (decimals_[axis] == kAutoDecimals) {
            const float range = high - min_[axis];
            std::uint8_t places;
            if (range >= 10.0f) places = 0;
            else if (range < 0.0001f) places = 5;
            else if (range < 0.001f) places = 4;
            else if (range < 0.01f) places = 3;
            else places = range < 0.1f ? 2 : 1;
            decimals_[axis] = places;
        }
    }
    if (graphArea_.left == kAutoRect) {
        cRZRect area;
        GetAutomaticGraphAreaRect(area);
        SetGraphArea(area);
    }
    if (titleArea_.left == kAutoRect) GetAutomaticTitleRect(titleArea_);
    for (int axis = 0; axis < 2; ++axis) {
        if (labelArea_[axis].left == kAutoRect) GetAutomaticAxisLabelRect(static_cast<Axis>(axis), labelArea_[axis]);
        if (majorSpacing_[axis] == kAutoValue) GetAutomaticAxisMajorTickSpacing(static_cast<Axis>(axis), majorSpacing_[axis]);
        if (minorSpacing_[axis] == kAutoValue) GetAutomaticAxisMinorTickSpacing(static_cast<Axis>(axis), minorSpacing_[axis]);
        CalculateAxisZeroPosition(static_cast<Axis>(axis));
    }
}

void cGZGraph::GetAutomaticTitleRect(cRZRect& out) {   // @ 0x100379b48
    out = cRZRect{4, 4, GetW() - 4, titleHeight_ + 4};
}

void cGZGraph::GetAutomaticGraphAreaRect(cRZRect& out) {   // @ 0x100379b84
    int tickLength[2], textExtent[2], labelExtent[2];
    for (int axis = 0; axis < 2; ++axis) {
        tickLength[axis] = majorTicks_[axis] ? majorLength_[axis] : 0;
        if (!tickText_[axis]) {
            textExtent[axis] = 0;
        } else if (tickFont_[axis] == nullptr) {
            textExtent[axis] = 16;
        } else if (axis == 0) {
            textExtent[axis] = tickFont_[0]->CharHeight(nullptr, 0);
        } else {
            const std::string text = FormatTick(max_[axis], decimals_[axis]);
            textExtent[axis] = tickFont_[axis]->GetStringWidth(text.data(), static_cast<std::uint32_t>(text.size()));
        }
        if (axisLabel_[axis].Strlen() == 0) labelExtent[axis] = 0;
        else labelExtent[axis] = labelFont_[axis] == nullptr ? 16 : labelFont_[axis]->CharHeight(nullptr, 0);
    }
    out = cRZRect{16, 16, GetW() - 16, GetH() - 16};
    if (titleArea_.left == kAutoRect) out.top = titleHeight_ + 4;
    if (labelArea_[0].left == kAutoRect) out.bottom = (((GetH() - 4) - tickLength[0]) - textExtent[0]) - labelExtent[0];
    if (labelArea_[1].left == kAutoRect) out.left = labelExtent[1] + 4 + tickLength[1] + textExtent[1];
}

void cGZGraph::GetAutomaticAxisLabelRect(Axis axis, cRZRect& out) {   // @ 0x100379df8
    int tickLength = majorTicks_[axis] ? majorLength_[axis] : 0;
    int textExtent = 0;
    if (tickText_[axis]) {
        if (tickFont_[axis] == nullptr) textExtent = 16;
        else if (axis == 0) textExtent = tickFont_[0]->CharHeight(nullptr, 0);
        else {
            const std::string text = FormatTick(max_[axis], decimals_[axis]);
            textExtent = tickFont_[axis]->GetStringWidth(text.data(), static_cast<std::uint32_t>(text.size()));
        }
    }
    int labelHeight = 0, labelWidth = 0;
    if (axisLabel_[axis].Strlen() != 0) {
        if (labelFont_[axis] == nullptr) {
            labelWidth = graphArea_.right - graphArea_.left;
            labelHeight = 16;
        } else {
            labelHeight = labelFont_[axis]->CharHeight(nullptr, 0);
            labelWidth = labelFont_[axis]->GetStringWidth(axisLabel_[axis].Data(), axisLabel_[axis].Strlen());
        }
    }
    if (axis == kAxisVertical) {
        const int right = (graphArea_.left - tickLength) - textExtent;
        const int centre = (graphArea_.bottom - graphArea_.top) / 2 + graphArea_.top;
        if ((labelRotation_[1] | 2) == 2) {
            out = cRZRect{right - labelWidth, centre - labelHeight / 2, right, labelHeight / 2 + 1 + centre};
        } else {   // rotated a quarter turn: the width and height swap
            out = cRZRect{right - labelHeight, centre - labelWidth / 2, right, labelWidth / 2 + 1 + centre};
        }
    } else if (axis == kAxisHorizontal) {
        const int centre = (graphArea_.right - graphArea_.left) / 2 + graphArea_.left;
        const int top = textExtent + tickLength + graphArea_.bottom;
        out = cRZRect{centre - labelWidth / 2, top, labelWidth / 2 + 1 + centre, labelHeight + top};
    }
}

void cGZGraph::GetAutomaticAxisMinValue(Axis axis, float& out) {   // @ 0x10037a0e8
    const float low = GetLowestAxisValue(axis);
    out = low < 0.0f ? low / 0.9f : 0.0f;
}

void cGZGraph::GetAutomaticAxisMaxValue(Axis axis, float& out) {   // @ 0x10037a118
    const float high = GetHighestAxisValue(axis);
    out = high > 0.0f ? high / 0.9f : 0.0f;
}

void cGZGraph::GetAutomaticAxisMajorTickSpacing(Axis axis, float& out) {   // @ 0x10037a1e8
    out = (max_[axis] - min_[axis]) * 0.125f;
}

void cGZGraph::GetAutomaticAxisMinorTickSpacing(Axis axis, float& out) {   // @ 0x10037a210
    out = majorSpacing_[axis] * 0.5f;
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing

void cGZGraph::CompleteRedraw(cIGZBuffer* buffer) {   // @ 0x10037a22e
    if (buffer == nullptr) {
        IGraphHost* host = GraphHost();
        buffer = host ? host->WindowBuffer(drawBuffer_) : nullptr;
        if (buffer == nullptr) return;
    }
    if (!fontsLoaded_) LoadDefaultFonts();
    CommitDefaultValues();
    for (int axis = 0; axis < 2; ++axis) CalculateAxisZeroPosition(static_cast<Axis>(axis));
    DrawBackground(buffer);
    DrawGraphAreaBackground(buffer);
    DrawAxes(buffer);
    DrawTicks(buffer, 7);
    DrawTickText(buffer, 7);
    DrawGraphData(buffer);
    DrawDynamicObjects(buffer);
    DrawTitles(buffer);
}

void cGZGraph::DrawBackground(cIGZBuffer* buffer) {   // @ 0x10037a338
    const cRZRect rect{0, 0, drawArea_.right - drawArea_.left, drawArea_.bottom - drawArea_.top};
    if (backgroundPattern_ == nullptr) {
        if (backgroundFill_ && buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockFill))) {
            buffer->Fill(backgroundFillColor_, &rect);
            buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockFill));
        }
        if (!backgroundOutline_) return;
        if (!buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockOutline))) return;
        DrawRectangleOutline(buffer, rect, backgroundOutlineColor_, 1);
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockOutline));
    } else {
        if (!buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockBlt))) return;
        Blt(backgroundPattern_, buffer, rect, rect);
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockBlt));
    }
}

void cGZGraph::DrawGraphAreaBackground(cIGZBuffer* buffer) {   // @ 0x10037a456
    if (areaPattern_ == nullptr) {
        if (areaFill_ && buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockFill))) {
            buffer->Fill(areaFillColor_, &graphArea_);
            buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockFill));
        }
        if (!areaOutline_) return;
        if (!buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockOutline))) return;
        DrawRectangleOutline(buffer, graphArea_, areaOutlineColor_, 1);
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockOutline));
    } else {
        const cRZRect source{0, 0, graphArea_.right - graphArea_.left, graphArea_.bottom - graphArea_.top};
        if (!buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockBlt))) return;
        Blt(areaPattern_, buffer, source, graphArea_);
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockBlt));
    }
}

void cGZGraph::DrawAxes(cIGZBuffer* buffer) {   // @ 0x10037a576
    if (!drawAxes_) return;
    if (!buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockLine))) return;
    DrawLine(buffer, graphArea_.left, graphArea_.top, graphArea_.left, graphArea_.bottom, static_cast<int>(axisColor_), 1);
    DrawLine(buffer, graphArea_.left, graphArea_.bottom, graphArea_.right, graphArea_.bottom, static_cast<int>(axisColor_), 1);
    buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockLine));
}

void cGZGraph::DrawTicks(cIGZBuffer* buffer, std::uint32_t axisMask) {   // @ 0x10037a62a
    if (!(majorTicks_[0] || majorTicks_[1])) return;
    if (!buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockLine))) return;
    const int color = static_cast<int>(tickColor_);
    if ((axisMask & 1) && majorTicks_[0]) {
        const int bottom = graphArea_.bottom;
        const int length = majorLength_[0];
        const int gridEnd = majorGrid_[0] ? graphArea_.top : graphArea_.bottom;
        int minorBottom = 0, minorTop = 0;
        if (minorTicks_[0]) {
            minorBottom = bottom + minorLength_[0];
            minorTop = minorGrid_[0] ? graphArea_.top : graphArea_.bottom;
        }
        for (float value = min_[0]; value <= majorSpacing_[0] / 100.0f + max_[0]; value += majorSpacing_[0]) {
            const int x = ValueToPixel(value, kAxisHorizontal, false);
            DrawLine(buffer, x, gridEnd, x, length + bottom, color, 1);
            if (minorTicks_[0]) {
                for (float minor = minorSpacing_[0] + value; minor < value + majorSpacing_[0]; minor += minorSpacing_[0]) {
                    const int mx = ValueToPixel(minor, kAxisHorizontal, false);
                    DrawLine(buffer, mx, minorTop, mx, minorBottom, color, 1);
                }
            }
        }
    }
    if ((axisMask & 2) && majorTicks_[1]) {
        const int left = graphArea_.left;
        const int length = majorLength_[1];
        const int gridEnd = majorGrid_[1] ? graphArea_.right : graphArea_.left;
        int minorLeft = 0, minorRight = 0;
        if (minorTicks_[1]) {
            minorLeft = left - minorLength_[1];
            minorRight = minorGrid_[1] ? graphArea_.right : graphArea_.left;
        }
        for (float value = min_[1]; value <= majorSpacing_[1] / 100.0f + max_[1]; value += majorSpacing_[1]) {
            const int y = ValueToPixel(value, kAxisVertical, false);
            DrawLine(buffer, left - length, y, gridEnd, y, color, 1);
            if (minorTicks_[1]) {
                for (float minor = minorSpacing_[1] + value; minor < value + majorSpacing_[1]; minor += minorSpacing_[1]) {
                    const int my = ValueToPixel(minor, kAxisVertical, false);
                    DrawLine(buffer, minorLeft, my, minorRight, my, color, 1);
                }
            }
        }
    }
    buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockLine));
}

void cGZGraph::DrawTickText(cIGZBuffer* buffer, std::uint32_t axisMask) {   // @ 0x10037aa2c
    auto colorOf = [](std::uint32_t c) { return c == kAutoColor ? kFontDefaultColor : c; };
    if ((axisMask & 1) && tickText_[0] && tickFont_[0]) {
        cIGZFont* font = tickFont_[0];
        const int height = font->CharHeight(nullptr, 0);
        const int top = graphArea_.bottom + 2 + majorLength_[0];
        const int bottom = height + 4 + graphArea_.bottom + majorLength_[0];
        for (float value = min_[0]; value <= majorSpacing_[0] * 0.25f + max_[0]; value += majorSpacing_[0]) {
            const std::string text = FormatTick(value, decimals_[0]);
            const std::uint32_t length = static_cast<std::uint32_t>(text.size());
            const int width = font->GetStringWidth(text.data(), length);
            const int x = ValueToPixel(value, kAxisHorizontal, false);
            cRZRect rect;
            if (tickTextAlignment_[0] == 1) { rect.left = x + ~(width / 2); rect.right = static_cast<int>(width / 2 + 1U + x); }
            else if (tickTextAlignment_[0] == 0) { rect.left = x; rect.right = width + 2 + x; }
            else { rect.left = (-2 - width) + x; rect.right = x; }
            rect.top = top; rect.bottom = bottom;
            font->DrawTextParagraph(buffer, rect, text.data(), length, tickTextAlignment_[0], colorOf(tickFontColor_[0]),
                                    kFontDefaultColor, nullptr, nullptr);
        }
    }
    if ((axisMask & 2) && tickText_[1] && tickFont_[1]) {
        cIGZFont* font = tickFont_[1];
        const int height = font->CharHeight(nullptr, 0);
        const int right = (graphArea_.left - 2) - majorLength_[1];
        for (float value = min_[1]; value <= majorSpacing_[1] / 100.0f + max_[1]; value += majorSpacing_[1]) {
            const std::string text = FormatTick(value, decimals_[1]);
            const std::uint32_t length = static_cast<std::uint32_t>(text.size());
            cRZRect rect;
            rect.left = right - font->GetStringWidth(text.data(), length);
            const int y = ValueToPixel(value, kAxisVertical, false);
            rect.bottom = height / 2 + 2 + y;
            rect.top = y - height / 2;
            rect.right = right;
            font->DrawTextParagraph(buffer, rect, text.data(), length, tickTextAlignment_[1], colorOf(tickFontColor_[1]),
                                    kFontDefaultColor, nullptr, nullptr);
        }
    }
}

void cGZGraph::DrawDynamicObjects(cIGZBuffer* buffer) {   // @ 0x10037ae3a
    for (DynamicGraphObject* object : dynamicObjects_) object->DrawSelf(this, buffer);
}

void cGZGraph::DrawRotatedAxisLabel(cIGZBuffer* buffer) {
    IGraphHost* host = GraphHost();
    const cRZRect& area = labelArea_[1];
    const GraphRotation rotation = static_cast<GraphRotation>(labelRotation_[1]);
    int width, height;
    if (rotation == kRotate180) {   // (rotation | 2) == 2 and not zero
        width = (area.right + 4) - area.left;
        height = (area.bottom - area.top) + 4;
    } else {
        width = (area.bottom + 4) - area.top;
        height = (area.right - area.left) + 4;
    }
    const cRZRect scratchRect{0, 0, width, height};
    cIGZBuffer* scratch = host->CreateBuffer();
    scratch->Init(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), cGZBufferColorType{kBufferColorDefault, 0x10});
    if (scratch->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockFill))) {
        scratch->Fill(keyColor_, scratchRect, nullptr);
        scratch->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockFill));
    }
    if (cIGZFont* font = labelFont_[1]) {
        const std::uint32_t color = labelColor_[1] == kAutoColor ? kFontDefaultColor : labelColor_[1];
        font->DrawTextParagraph(scratch, scratchRect, axisLabel_[1].Data(), axisLabel_[1].Strlen(), labelAlignment_[1],
                                color, kFontDefaultColor, nullptr, nullptr);
    }
    scratch->SetTransparency(keyColor_);
    if (buffer->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockPixelsWrite))) {
        const int angle = rotation == kRotate90 ? 90 : (rotation == kRotate180 ? 180 : 270);
        if (scratch->Lock(static_cast<cIGZBuffer::eLockFlags>(kLockPixelsRead))) {
            const std::uint32_t bits = buffer->GetBitsPerPixel();
            if (bits == 8 || bits == 16) {
                std::uint32_t key = 0;
                const bool keyed = scratch->GetTransparency() && scratch->GetTransparentColor(key);
                const std::uint32_t mask = bits == 8 ? 0xffu : 0xffffu;
                const int left = area.left, top = area.top;
                for (int sy = 0; sy < height; ++sy) {
                    for (int sx = 0; sx < width; ++sx) {
                        const std::uint32_t pixel = scratch->GetPixel(static_cast<std::uint32_t>(sx), static_cast<std::uint32_t>(sy));
                        if (keyed && (pixel & mask) == (key & mask)) continue;
                        int dx, dy;
                        if (angle == 180) { dx = left + width - 1 - sx; dy = top + height - 1 - sy; }
                        else if (angle == 90) { dx = left + sy; dy = top + width - 1 - sx; }
                        else { dx = left + height - 1 - sy; dy = top + sx; }
                        buffer->SetPixel(static_cast<std::uint32_t>(dx), static_cast<std::uint32_t>(dy), pixel);
                    }
                }
            }
            scratch->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockPixelsRead));
        }
        buffer->Unlock(static_cast<cIGZBuffer::eLockFlags>(kLockPixelsWrite));
    }
    scratch->Release();
}

void cGZGraph::DrawTitles(cIGZBuffer* buffer) {   // @ 0x10037ae94
    auto colorOf = [](std::uint32_t c) { return c == kAutoColor ? kFontDefaultColor : c; };
    if (axisLabel_[0].Strlen() != 0 && labelFont_[0]) {
        labelFont_[0]->DrawTextParagraph(buffer, labelArea_[0], axisLabel_[0].Data(), axisLabel_[0].Strlen(),
                                         labelAlignment_[0], colorOf(labelColor_[0]), kFontDefaultColor, nullptr, nullptr);
    }
    if (axisLabel_[1].Strlen() != 0 && labelFont_[1]) {
        if (labelRotation_[1] == kRotate0) {
            labelFont_[1]->DrawTextParagraph(buffer, labelArea_[1], axisLabel_[1].Data(), axisLabel_[1].Strlen(),
                                             labelAlignment_[1], colorOf(labelColor_[1]), kFontDefaultColor, nullptr, nullptr);
        } else {
            DrawRotatedAxisLabel(buffer);
        }
    }
    if (title_.Strlen() != 0 && titleFont_) {
        titleFont_->DrawTextParagraph(buffer, titleArea_, title_.Data(), title_.Strlen(), titleAlignment_,
                                      colorOf(titleColor_), kFontDefaultColor, nullptr, nullptr);
    }
}

}  // namespace gz

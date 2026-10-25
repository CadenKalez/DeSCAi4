#include "app/NativeGraphics.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "gz/DrawContextRenderToBuffer.h"

namespace native {

namespace {

// The recovered hardware engine with its hardware half attached. The original is one object (the SimGL class derives from the
// engine); the port keeps them as two and ties them here.
//
// GAP, not original behaviour: cGZDrawContext::DirectDraw drops the Tesselate fallback (the context's tesselator is not
// recovered), and the hardware engine answers Tesselate for points and lines. Until it is, those two primitives are drawn here as
// one-pixel rectangles - the same stop-gap the software engine below needs.
class HardwareEngine final : public cGZDrawContextRenderToHardware {
public:
    explicit HardwareEngine(ISimGLDrawServices* services) : simgl_(this, services) { SetHardwareRenderHost(&simgl_); }

    gz::eEngineDrawResult Draw(gz::eDrawingOp op, const float* p, int count) override {
        const gz::eEngineDrawResult how = cGZDrawContextRenderToHardware::Draw(op, p, count);
        if (how != gz::eEngineDrawResult::Tesselate) return how;
        rects_.clear();
        if (op == gz::eDrawingOp::Point) {
            for (int i = 0; i < count; ++i) PushPixel(int(std::floor(p[i * 2])), int(std::floor(p[i * 2 + 1])));
        } else if (op == gz::eDrawingOp::Line) {
            for (int i = 0; i + 1 < count; i += 2)
                PushLine(int(std::floor(p[i * 2])), int(std::floor(p[i * 2 + 1])), int(std::floor(p[i * 2 + 2])), int(std::floor(p[i * 2 + 3])));
        } else {
            return how;
        }
        if (!rects_.empty()) cGZDrawContextRenderToHardware::Draw(gz::eDrawingOp::FillRect, rects_.data(), int(rects_.size() / 2));
        return gz::eEngineDrawResult::Done;
    }

private:
    void PushPixel(int x, int y) {
        rects_.push_back(float(x)); rects_.push_back(float(y)); rects_.push_back(float(x + 1)); rects_.push_back(float(y + 1));
    }
    void PushLine(int x0, int y0, int x1, int y1) {
        const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            PushPixel(x0, y0);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    cGZDrawContextRenderToSimGL simgl_;
    std::vector<float> rects_;
};

// The software engine for private buffers. Same GAP as above: the context's Rasterize fallback is not recovered, and the
// software engine answers Rasterize for every non-antialiased primitive, so rectangles, points and lines are turned into spans
// here and handed to DrawPreclippedSpanList.
class SoftwareEngine final : public gz::cIGZDrawEngine {
public:
    explicit SoftwareEngine(cGZDrawContextRenderToBuffer* inner) : inner_(inner) {}
    ~SoftwareEngine() { inner_->Release(); }
    bool QueryInterface(std::uint32_t iid, void** out) override { return inner_->QueryInterface(iid, out); }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override { if (--refs_ == 0) { delete this; return 0; } return refs_; }
    bool Init(gz::IDrawTargetBuffer* t) override { return inner_->Init(t); }
    void Shutdown() override { inner_->Shutdown(); }
    void GetCanvasArea(cRZRect& o) override { inner_->GetCanvasArea(o); }
    void SetClipRect(const cRZRect& c) override { clip_ = c; inner_->SetClipRect(c); }
    void SetColorTable(int a, int b, const std::uint32_t* c) override { inner_->SetColorTable(a, b, c); }
    void SetAlphaBlend(int b) override { inner_->SetAlphaBlend(b); }
    void SetBltModulate(bool o) override { inner_->SetBltModulate(o); }
    void SetAntialiasing(bool o) override { inner_->SetAntialiasing(o); }
    void SetLineThickness(float t) override { inner_->SetLineThickness(t); }
    void SetGradientTransform(const float (*m)[4], float a, float b) override { inner_->SetGradientTransform(m, a, b); }
    bool SetBltSource(cIGZUnknown* s, float x, float y, float sx, float sy, cRZRect& c) override { return inner_->SetBltSource(s, x, y, sx, sy, c); }
    void ClearBltTiling() override { inner_->ClearBltTiling(); }
    void SetBltTiling(const cRZRect& t) override { inner_->SetBltTiling(t); }
    void Flush() override { inner_->Flush(); }
    void Suspend() override { inner_->Suspend(); }
    void Resume() override { inner_->Resume(); }
    bool LockTarget(gz::cGZBufferSurfaceDesc& d, bool w) override { return inner_->LockTarget(d, w); }
    void UnlockTarget() override { inner_->UnlockTarget(); }
    void DrawPreclippedSpanList(const std::uint32_t* s, int c) override { inner_->DrawPreclippedSpanList(s, c); }

    gz::eEngineDrawResult Draw(gz::eDrawingOp op, const float* p, int count) override {
        const gz::eEngineDrawResult how = inner_->Draw(op, p, count);
        if (how != gz::eEngineDrawResult::Rasterize) return how;
        spans_.clear();
        switch (op) {
        case gz::eDrawingOp::FillRect:
            Rect(int(std::lround(p[0])), int(std::lround(p[1])), int(std::lround(p[2])), int(std::lround(p[3])));
            break;
        case gz::eDrawingOp::Point:
            for (int i = 0; i < count; ++i) Rect(int(std::floor(p[i * 2])), int(std::floor(p[i * 2 + 1])), int(std::floor(p[i * 2])) + 1, int(std::floor(p[i * 2 + 1])) + 1);
            break;
        case gz::eDrawingOp::Line:
            for (int i = 0; i + 1 < count; i += 2) Line(int(std::floor(p[i * 2])), int(std::floor(p[i * 2 + 1])), int(std::floor(p[i * 2 + 2])), int(std::floor(p[i * 2 + 3])));
            break;
        default: break;
        }
        if (!spans_.empty()) inner_->DrawPreclippedSpanList(spans_.data(), int(spans_.size() / 2));
        return gz::eEngineDrawResult::Done;
    }

private:
    void Rect(int l, int t, int r, int b) {
        l = std::max(l, clip_.left); r = std::min(r, clip_.right); t = std::max(t, clip_.top); b = std::min(b, clip_.bottom);
        if (l >= r || t >= b) return;
        for (int y = t; y < b; ++y) { spans_.push_back(std::uint32_t(y) << 16 | std::uint32_t(l)); spans_.push_back(std::uint32_t(y) << 16 | std::uint32_t(r)); }
    }
    void Line(int x0, int y0, int x1, int y1) {
        const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        for (;;) {
            Rect(x0, y0, x0 + 1, y0 + 1);
            if (x0 == x1 && y0 == y1) break;
            const int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
    cGZDrawContextRenderToBuffer* inner_;
    std::uint32_t refs_ = 1;
    cRZRect clip_{};
    std::vector<std::uint32_t> spans_;
};

// The screen buffer's pixels (its context asks for the engine lazily, when it first needs a target, so the choice is made by
// the target rather than by when the request arrives).
gz::IDrawTargetBuffer* g_screenTarget = nullptr;
std::vector<Buffer*> g_buffers;

}  // namespace

const std::vector<Buffer*>& Buffer::All() { return g_buffers; }

Buffer::Buffer(int width, int height, bool onScreen) : onScreen_(onScreen) {
    g_buffers.push_back(this);
    buffer_ = new cGZBuffer();
    buffer_->AddRef();   // this object's own reference (the draw-context wrappers hold theirs)
    if (onScreen) g_screenTarget = buffer_;
    // The screen is only a size and a colour space (the driver holds the pixels). A private buffer is shown by uploading it as a
    // texture, and cGZBufferSimGLCache only knows the formats the shipped game used: ARGB8888 for 32-bit, 16-bit otherwise.
    buffer_->Init(std::uint32_t(width), std::uint32_t(height), cGZBufferColorType{onScreen ? kBufferColorXRGB8888 : kBufferColorARGB8888, 32});
    buffer_->Fill(0, nullptr);
    context_ = new gz::cGZDrawContext();
    context_->AddRef();
    context_->SetTarget(buffer_);   // asks the draw-context host for the engine
    asBuffer_ = new gz::cGZDrawContextAsBuffer();
    asBuffer_->AddRef();
    asBuffer_->Init(context_, buffer_);
}

Buffer::~Buffer() {
    g_buffers.erase(std::remove(g_buffers.begin(), g_buffers.end(), this), g_buffers.end());
    if (asBuffer_) asBuffer_->Release();
    if (context_) context_->Release();
    if (buffer_) buffer_->Release();
}

// On the screen a fill is a context operation (it reaches the driver); in a private buffer it writes pixels.
bool Buffer::Fill(std::uint32_t color, const cRZRect& rect) {
    return onScreen_ ? asBuffer_->Fill(color, rect, nullptr) : buffer_->Fill(color, rect, nullptr);
}

bool Buffer::Blt(IWinBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect) {
    cGZBuffer* from = static_cast<Buffer*>(source)->buffer_;
    return onScreen_ ? asBuffer_->Blt(from, sourceRect, destRect, nullptr) : buffer_->Blt(from, sourceRect, destRect, nullptr);
}

Graphics::Graphics(gd::cMetalGDriver* driver, int width, int height) : driver_(driver), services_(driver) {
    SetWinGraphics(this);
    gz::SetDrawContextHost(this);
    SetBufferHost(this);
    primary_.reset(new Buffer(width, height, true));   // the screen: its context gets the hardware engine
}
Graphics::~Graphics() = default;

gz::cIGZDrawEngine* Graphics::CreateEngine(gz::IDrawTargetBuffer* target) {
    if (target == g_screenTarget) {
        auto* engine = new HardwareEngine(&services_);
        engine->AddRef();
        engine->Init(target);
        return engine;   // one counted reference, as the context expects
    }
    auto* inner = new cGZDrawContextRenderToBuffer();
    inner->AddRef();
    inner->Init(target);
    return new SoftwareEngine(inner);
}

}  // namespace native

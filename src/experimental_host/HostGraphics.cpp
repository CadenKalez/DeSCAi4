// EARLY TEST / EXPERIMENTAL HOST — not recovered game code. See README_EARLY_TEST.md.
#include "experimental_host/HostGraphics.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "rz/SpanList.h"

namespace exphost {

namespace {
// FINDING (early test): cGZDrawContext::DirectDraw drops the Tesselate / Rasterize fallbacks (not recovered), and the
// software engine answers "Rasterize" for every non-antialiased primitive, so nothing reaches the screen. This
// decorator performs the missing rasterization for the primitives the window tree uses (points, lines, filled
// rectangles) and hands the clipped spans to the recovered engine's DrawPreclippedSpanList.
class RasterizingEngine final : public gz::cIGZDrawEngine {
public:
    explicit RasterizingEngine(cGZDrawContextRenderToBuffer* inner) : inner_(inner) {}
    ~RasterizingEngine() { inner_->Release(); }
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
        default: break;   // ellipses, beziers, polygons: not needed by the flat-rect window tree
        }
        if (!spans_.empty()) inner_->DrawPreclippedSpanList(spans_.data(), int(spans_.size() / 2));
        return gz::eEngineDrawResult::Done;
    }

private:
    void Rect(int l, int t, int r, int b) {
        l = std::max(l, clip_.left); r = std::min(r, clip_.right); t = std::max(t, clip_.top); b = std::min(b, clip_.bottom);
        if (l >= r || t >= b) return;
        for (int y = t; y < b; ++y) { spans_.push_back(std::uint32_t(y) << 16 | std::uint32_t(l)); spans_.push_back(std::uint32_t(y) << 16 | std::uint32_t(r)); }   // (start, end) pairs: y << 16 | x
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
}  // namespace

HostBuffer::HostBuffer(int width, int height) {
    buffer_ = new cGZBuffer();
    buffer_->Init(std::uint32_t(width), std::uint32_t(height), cGZBufferColorType{kBufferColorXRGB8888, 32});
    buffer_->Fill(0, nullptr);   // a fresh buffer is not cleared by the allocation
    context_ = new gz::cGZDrawContext();
    context_->AddRef();   // the window plot code AddRef/Releases around every paint; the buffer owns one reference
    context_->SetTarget(buffer_);
    asBuffer_ = new gz::cGZDrawContextAsBuffer();
    asBuffer_->AddRef();
    asBuffer_->Init(context_, buffer_);
}

HostBuffer::~HostBuffer() {
    if (asBuffer_) asBuffer_->Release();
    if (context_) context_->Release();
    if (buffer_) buffer_->Release();
}

HostGraphics::HostGraphics(int width, int height) {
    Install();   // the draw context asks the host for its engine while the primary buffer is being built
    primary_.reset(new HostBuffer(width, height));
}
HostGraphics::~HostGraphics() = default;

void HostGraphics::Install() {
    SetWinGraphics(this);
    gz::SetDrawContextHost(this);
    SetBufferHost(this);
}

gz::cIGZDrawEngine* HostGraphics::CreateEngine(gz::IDrawTargetBuffer* target) {
    auto* inner = new cGZDrawContextRenderToBuffer();
    inner->AddRef();
    inner->Init(target);
    return new RasterizingEngine(inner);   // one counted reference, as the context expects
}

}  // namespace exphost

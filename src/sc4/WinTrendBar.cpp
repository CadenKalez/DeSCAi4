#include "sc4/WinTrendBar.h"

#include <cmath>
#include <vector>

#include "gz/Buffer.h"

namespace sc4 {

namespace {
ITrendBarHost* g_host = nullptr;

// (x + (x >>> 31)) >> 1: halving that rounds towards zero.
int Half(int x) { return (x + static_cast<int>(static_cast<std::uint32_t>(x) >> 31)) >> 1; }

float Clamp01(float v) {
    float low = 0.0f;
    if (0.0f <= v) low = v;
    float out = 1.0f;
    if (low <= 1.0f) out = low;
    return out;
}
}  // namespace

void SetTrendBarHost(ITrendBarHost* host) { g_host = host; }

cSC4WinTrendBar::cSC4WinTrendBar(cIGZWinMgr* mgr) : cGZWin(mgr) {}

cSC4WinTrendBar::~cSC4WinTrendBar() {
    if (marker_) marker_->Release();
    if (fill_) fill_->Release();
}

bool cSC4WinTrendBar::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1004655ba
    if (iid == 1 || iid == kGZIID_cISC4WinTrendBar) {
        *out = static_cast<cISC4WinTrendBar*>(this);
    } else if (iid == 0x22ba0121) {
        *out = static_cast<cIGZWin*>(this);
    } else if (iid == 0x452294aa) {
        *out = static_cast<cIGZMessageTarget2*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

bool cSC4WinTrendBar::Init() {   // @ 0x100465682
    if (!get_init_flag()) {
        cGZWin::Init();   // the answer is ignored
        if (g_host) {
            if (cIGZMessageServer2* server = g_host->MessageServer()) {
                server->AddNotification(static_cast<cIGZMessageTarget2*>(this), kTrendBarStoreMessage);
                server->AddNotification(static_cast<cIGZMessageTarget2*>(this), kTrendBarDayMessage);
            }
        }
    }
    return true;
}

bool cSC4WinTrendBar::Shutdown() {   // @ 0x100465704
    if (!get_init_flag()) return true;
    if (g_host) {
        if (cIGZMessageServer2* server = g_host->MessageServer()) {
            server->RemoveNotification(static_cast<cIGZMessageTarget2*>(this), kTrendBarStoreMessage);
            server->RemoveNotification(static_cast<cIGZMessageTarget2*>(this), kTrendBarDayMessage);
        }
    }
    return cGZWin::Shutdown();
}

void cSC4WinTrendBar::SetArtwork(cIGZBuffer* fill, cIGZBuffer* marker) {   // @ 0x100465786
    if (fill_ != fill) {
        if (fill) fill->AddRef();
        cIGZBuffer* old = fill_;
        fill_ = fill;
        if (old) old->Release();
    }
    if (marker_ != marker) {
        if (marker) marker->AddRef();
        cIGZBuffer* old = marker_;
        marker_ = marker;
        if (old) old->Release();
        marker = marker_;
    }
    if (marker) {
        if (IWinGraphics* graphics = WinGraphics()) marker->SetTransparency(graphics->MakeNativeColor(0xff, 0, 0xff));
    }
    InvalidateSelfAndParents();
}

void cSC4WinTrendBar::RefreshFromHistory() {
    if (!g_host) return;
    const cGZDate date(g_host->SimulatorDayNumber());
    float value = 0;
    if (g_host->HistoryPoint(series_, date, value)) UpdateValue(value);
}

void cSC4WinTrendBar::SetDataSource(std::uint32_t series, float low, float high) {   // @ 0x100465852
    series_ = series;
    current_ = 0.5f;
    std::vector<SC4Point> points;
    points.push_back({low, 0.0f});
    points.push_back({high, 1.0f});
    curve_.SetResponseCurveData(points);
    RefreshFromHistory();
    InvalidateSelfAndParents();
}

void cSC4WinTrendBar::SetDataSource(std::uint32_t series, const cSC4ResponseCurve& curve) {   // @ 0x100465a4e
    series_ = series;
    current_ = 0.5f;
    curve_ = curve;
    RefreshFromHistory();
    InvalidateSelfAndParents();
}

void cSC4WinTrendBar::RepaintIfVisible() {
    if (IsVisible()) InvalidateSelfAndParents();
}

void cSC4WinTrendBar::UpdateValue(float value) {   // @ 0x1004659b6
    const float oldCurrent = current_, oldDelta = delta_;
    current_ = Clamp01(curve_.GetOutputValue(value));
    delta_ = current_ - oldCurrent;
    if (current_ == oldCurrent && delta_ == oldDelta) return;   // NaN counts as changed
    RepaintIfVisible();
}

void cSC4WinTrendBar::UpdateTrend(float value) {   // @ 0x100465bda
    const float oldCurrent = current_, oldDelta = delta_;
    target_ = Clamp01(curve_.GetOutputValue(value));
    delta_ = target_ - current_;
    InterpolateValue(kTrendBarGlide);
    if (current_ == oldCurrent && delta_ == oldDelta) return;
    RepaintIfVisible();
}

void cSC4WinTrendBar::InterpolateValue(float step) {   // @ 0x100465c92
    const float old = current_;
    float next = target_;
    if (std::fabs(step * delta_) < std::fabs(old - target_)) next = Clamp01(step * delta_ + old);
    current_ = next;
    if (next != old) RepaintIfVisible();   // NaN counts as changed
}

bool cSC4WinTrendBar::DoMessage(cIGZMessage2* message) {   // @ 0x100465b18
    const std::uint32_t type = message->GetType();
    if (type == kTrendBarDayMessage) {
        InterpolateValue(kTrendBarGlide);
    } else if (type == kTrendBarStoreMessage) {
        auto* standard = static_cast<cIGZMessage2Standard*>(message);
        if (standard->GetData1() == series_) {   // vslot 0x48
            if (g_host) {
                // The original builds the simulator's date here and never uses it.
                const cGZDate unused(g_host->SimulatorDayNumber());
                (void)unused;
            }
            UpdateTrend(static_cast<float>(static_cast<int>(standard->GetData2())));   // vslot 0x70
        }
    }
    return false;
}

bool cSC4WinTrendBar::GZPaint() {   // @ 0x100465d38
    if (!fill_ || !marker_) return false;
    if (!g_host || !drawBuffer_) return true;   // the original has no such checks
    const int fillHeight = static_cast<int>(fill_->Height());
    const std::uint32_t markerWidth = marker_->Width();
    const int markerHeight = static_cast<int>(marker_->Height());

    const float d = delta_;
    int rise, fall;
    if (d > 0.02f) rise = 3; else if (d > 0.01f) rise = 2; else if (d > 0.00001f) rise = 1; else rise = 0;
    if (-0.02f > d) fall = 3; else if (-0.01f > d) fall = 2; else if (-0.00001f > d) fall = 1; else fall = 0;

    const int lastRow = fillHeight - 1;
    const float position = static_cast<float>(lastRow) * current_;
    IWinBuffer* target = drawBuffer_;
    if (!target->Lock(kTrendBarLockFlags)) return true;

    const int column = static_cast<int>(markerWidth / 6);   // one sixth of the marker image
    const int left = drawArea_.left, top = drawArea_.top, right = drawArea_.right, bottom = drawArea_.bottom;
    const int centred = Half(bottom - (top + markerHeight)) + top;
    const int shown = static_cast<int>(static_cast<double>(position) + 0.5);
    if (fall != 0) {
        const cRZRect source{0, 0, fall * column, markerHeight};
        const cRZRect destination{(fall ^ 3) * column + left, centred, 3 * column + left, markerHeight + centred};
        g_host->Blt(target, marker_, source, destination);
    }
    const int below = lastRow - shown;
    if (rise != 0) {
        const cRZRect source{3 * column, 0, (rise + 3) * column, markerHeight};
        const cRZRect destination{right - 3 * column, centred, right - (rise ^ 3) * column, markerHeight + centred};
        g_host->Blt(target, marker_, source, destination);
    }
    const int fillWidth = static_cast<int>(fill_->Width());
    const int x = static_cast<int>(static_cast<std::uint32_t>((right - left) - fillWidth) >> 1) + left;
    const cRZRect source{0, below, fillWidth, below + 1};
    const cRZRect destination{x, top, fillWidth + x, bottom};
    g_host->BltTiled(target, fill_, source, destination);
    target->Unlock(kTrendBarLockFlags);
    return true;
}

}  // namespace sc4

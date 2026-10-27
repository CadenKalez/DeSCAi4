// cSC4WinTrendBar: the value glide, the message handling and the blits the paint makes. Expected rectangles are worked out
// by hand from the original's arithmetic (see the comments at each check).
#include <cstdio>
#include <string>
#include <vector>

#include "fake_winmgr.h"
#include "gz/Buffer.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/WinTrendBar.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
std::string R(const cRZRect& r) { return std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," + std::to_string(r.bottom); }

struct Server final : cIGZMessageServer2 {
    std::vector<std::string> log;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool MessageSend(cIGZMessage2*) override { return true; }
    bool MessagePost(cIGZMessage2*, bool) override { return true; }
    bool AddNotification(cIGZMessageTarget2*, std::uint32_t t) override { log.push_back("add " + std::to_string(t)); return true; }
    bool RemoveNotification(cIGZMessageTarget2*, std::uint32_t t) override { log.push_back("remove " + std::to_string(t)); return true; }
    bool GeneralMessagePostToTarget(cIGZMessage2*, cIGZMessageTarget2*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return 1; }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }
};

struct Canvas final : IWinBuffer {
    std::vector<std::string> log;
    bool lockResult = true;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    const cRZRect* GetBounds() const override { return nullptr; }
    std::uint32_t MakeNativeColor(std::uint8_t, std::uint8_t, std::uint8_t) override { return 0; }
    void NativeToRGB(std::uint32_t, std::uint8_t&, std::uint8_t&, std::uint8_t&) override {}
    void* GetDrawContext() override { return nullptr; }
    bool SetFadeAlpha(std::uint8_t) override { return false; }
    bool Lock(std::uint32_t f) override { log.push_back("lock " + std::to_string(f)); return lockResult; }
    void Unlock(std::uint32_t f) override { log.push_back("unlock " + std::to_string(f)); }
};

struct Host final : ITrendBarHost {
    Server* server = nullptr;
    Canvas* canvas = nullptr;
    float history = 0; bool haveHistory = true; std::uint32_t day = 0, askedSeries = 0, askedDay = 0;
    cIGZMessageServer2* MessageServer() override { return server; }
    bool HistoryPoint(std::uint32_t series, const cIGZDate& date, float& value) override {
        askedSeries = series; askedDay = date.DayNumber(); value = history; return haveHistory;
    }
    std::uint32_t SimulatorDayNumber() override { return day; }
    bool Blt(IWinBuffer* target, cIGZBuffer*, const cRZRect& s, const cRZRect& d) override {
        canvas->log.push_back("blt " + R(s) + ">" + R(d)); return target == canvas;
    }
    bool BltTiled(IWinBuffer* target, cIGZBuffer*, const cRZRect& s, const cRZRect& d) override {
        canvas->log.push_back("tiled " + R(s) + ">" + R(d)); return target == canvas;
    }
};

struct Bar final : cSC4WinTrendBar {
    using cSC4WinTrendBar::cSC4WinTrendBar;
    void SetCanvas(IWinBuffer* b) { drawBuffer_ = b; }
    void ClearInvalid() { invalid_ = false; }
    bool Invalid() const { return invalid_; }
};
}  // namespace

int main() {
    FakeMgr mgr; FakeWinGraphics graphics;
    SetWinGraphics(&graphics);
    Server server; Canvas canvas; Host host;
    host.server = &server; host.canvas = &canvas;
    SetTrendBarHost(&host);

    auto* fill = new cGZBuffer();   fill->AddRef();   fill->Init(20, 50, {kBufferColorARGB8888, 32});
    auto* marker = new cGZBuffer(); marker->AddRef(); marker->Init(60, 12, {kBufferColorARGB8888, 32});

    auto* bar = new Bar(&mgr);
    bar->AddRef();
    bar->SetArea(0, 0, 40, 100);

    // ---- views
    void* p = nullptr;
    CHECK(bar->QueryInterface(1, &p) && p == static_cast<cISC4WinTrendBar*>(bar)); bar->Release();
    CHECK(bar->QueryInterface(kGZIID_cISC4WinTrendBar, &p) && p == static_cast<cISC4WinTrendBar*>(bar)); bar->Release();
    CHECK(bar->QueryInterface(0x22ba0121, &p) && p == static_cast<cIGZWin*>(bar)); bar->Release();
    CHECK(bar->QueryInterface(0x452294aa, &p) && p == static_cast<cIGZMessageTarget2*>(bar)); bar->Release();
    CHECK(!bar->QueryInterface(0x1234, &p));

    // ---- Init / Shutdown: both messages
    CHECK(bar->Init());
    CHECK(server.log.size() == 2 && server.log[0] == "add " + std::to_string(kTrendBarStoreMessage) && server.log[1] == "add " + std::to_string(kTrendBarDayMessage));
    bar->SetCanvas(&canvas);

    // ---- artwork: no paint without both images
    bar->SetArtwork(fill, nullptr);
    CHECK(!bar->GZPaint() && canvas.log.empty());
    bar->SetArtwork(fill, marker);

    // ---- data source: curve 0..100 -> 0..1, the first history point (30) gives 0.3 and delta -0.2
    host.history = 30; host.day = 7000;
    bar->SetDataSource(0x20, 0.0f, 100.0f);
    CHECK(bar->Series() == 0x20 && host.askedSeries == 0x20 && host.askedDay == 7000);
    CHECK(bar->Current() > 0.2999f && bar->Current() < 0.3001f);
    CHECK(bar->Delta() < -0.1999f && bar->Delta() > -0.2001f);

    // fall level 3: marker columns are 10 wide, centred row (100 - 12)/2 = 44; fill row 49 - int(49*0.3+0.5) = 34
    canvas.log.clear();
    CHECK(bar->GZPaint());
    CHECK(canvas.log.size() == 4);
    if (canvas.log.size() == 4) {
        CHECK(canvas.log[0] == "lock 32784");
        CHECK(canvas.log[1] == "blt 0,0,30,12>0,44,30,56");
        CHECK(canvas.log[2] == "tiled 0,34,20,35>10,0,30,100");
        CHECK(canvas.log[3] == "unlock 32784");
    }
    for (auto& l : canvas.log) std::printf("  %s\n", l.c_str());

    // ---- a store message for another series is ignored; for ours it sets the target and glides one step
    cRZMessage2Standard message;
    message.SetType(kTrendBarStoreMessage);
    message.SetData1(0x21); message.SetData2(80);
    CHECK(!bar->DoMessage(&message) && bar->Target() == 0.0f);
    message.SetData1(0x20);
    CHECK(!bar->DoMessage(&message));
    CHECK(bar->Target() > 0.7999f && bar->Target() < 0.8001f);
    CHECK(bar->Current() > 0.3049f && bar->Current() < 0.3051f);   // 0.3 + 0.01 * 0.5
    CHECK(bar->Delta() > 0.4999f && bar->Delta() < 0.5001f);

    // rise level 3 (delta 0.5): columns 3..6 of the marker, ending at the right edge
    canvas.log.clear();
    CHECK(bar->GZPaint());
    CHECK(canvas.log.size() == 4 && canvas.log[1] == "blt 30,0,60,12>10,44,40,56");
    CHECK(canvas.log.size() == 4 && canvas.log[2] == "tiled 0,34,20,35>10,0,30,100");

    // ---- the day tick glides 1% of the delta, and snaps to the target when it is within one step
    float before = bar->Current();
    message.SetType(kTrendBarDayMessage);
    CHECK(!bar->DoMessage(&message));
    CHECK(bar->Current() > before + 0.0049f && bar->Current() < before + 0.0051f);
    bar->InterpolateValue(1.0f);
    CHECK(bar->Current() == bar->Target());

    // ---- UpdateValue(30.5): delta +0.005 -> rise level 1
    bar->SetDataSource(0x20, 0.0f, 100.0f);   // current 0.3 again
    bar->UpdateValue(30.5f);
    CHECK(bar->Delta() > 0.00499f && bar->Delta() < 0.00501f);
    canvas.log.clear();
    CHECK(bar->GZPaint());
    CHECK(canvas.log.size() == 4 && canvas.log[1] == "blt 30,0,40,12>10,44,20,56");

    // ---- a change repaints (window visible), no change does not
    bar->ClearInvalid();
    bar->UpdateValue(30.5f);   // same value: current and delta change (delta 0), so it repaints
    CHECK(bar->Invalid());
    bar->ClearInvalid();
    bar->UpdateValue(30.5f);   // now identical to the last call
    CHECK(!bar->Invalid());

    // ---- no lock: nothing drawn, still true
    canvas.lockResult = false; canvas.log.clear();
    CHECK(bar->GZPaint() && canvas.log.size() == 1);
    canvas.lockResult = true;

    // ---- history without a point leaves the value
    host.haveHistory = false;
    float keep = bar->Current();
    bar->SetDataSource(0x20, 0.0f, 100.0f);
    CHECK(bar->Current() == 0.5f && keep != 0.5f);
    host.haveHistory = true;

    // ---- Shutdown stops listening to both messages
    server.log.clear();
    CHECK(bar->Shutdown());
    CHECK(server.log.size() == 2 && server.log[0] == "remove " + std::to_string(kTrendBarStoreMessage));

    SetTrendBarHost(nullptr);
    bar->Release();
    fill->Release(); marker->Release();
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("sc4_wintrendbar: ok\n");
    return 0;
}

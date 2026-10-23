// cSC4WinTertiaryMenuFrame tests: COM lifetime through the two bases, Init's flag set-up, SetBackgroundImage's
// reference handling and nine-slice metrics, PositionFrame's clamping and content rectangle, the two
// IsPointInWindow* quirks, and GZPaint's compose / blit stages (four tiles, arrow offsets for none / hovered /
// held), the auto-repeat timer (300 ms then 100 ms, -1 / +1 scroll message), capture on press / release on up.
//
// Buffers are recording fakes; RZDrawBltTiled runs for real against them (that is the recovered helper the frame
// calls), so the right-edge tile shows up as several partial blits.
#include <cstdio>
#include <cstdint>
#include <set>
#include <vector>

#include <SDL.h>

#include "gz/Buffer.h"
#include "sc4/WinTertiaryMenuFrame.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
#define CHECK_EQ(a, b) do { const long long va = (a), vb = (b); if (va != vb) { \
    std::printf("FAIL line %d: %s == %s (%lld vs %lld)\n", __LINE__, #a, #b, va, vb); ++failures; } } while (0)
#define CHECK_RECT(r, l, t, rr, b) do { if ((r).left != (l) || (r).top != (t) || (r).right != (rr) || (r).bottom != (b)) { \
    std::printf("FAIL line %d: %s = {%d,%d,%d,%d}\n", __LINE__, #r, (r).left, (r).top, (r).right, (r).bottom); \
    ++failures; } } while (0)

using namespace sc4;

namespace {

// A recording cIGZBuffer.  Release only counts, so the test still owns the object and deletes it at the end.
struct FakeFrameBuffer final : cIGZBuffer {
    int refs = 0;
    int addRefs = 0;
    int releases = 0;
    int inits = 0;
    int heightCalls = 0;
    int widthCalls = 0;
    cGZBufferColorType initType{};
    std::uint32_t width = 0, height = 0;
    cRZRect area{0, 0, 0, 0};
    struct BltRecord { cIGZBuffer* source; cRZRect src; cRZRect dst; };
    std::vector<BltRecord> blts;
    std::vector<std::uint32_t> lockFlags;
    std::vector<std::uint32_t> unlockFlags;
    std::vector<std::uint32_t> fillColors;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { ++addRefs; return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { ++releases; return static_cast<std::uint32_t>(--refs); }
    bool Init(std::uint32_t w, std::uint32_t h, cGZBufferColorType type) override {
        ++inits;
        initType = type;
        width = w;
        height = h;
        area = {0, 0, static_cast<int>(w), static_cast<int>(h)};
        return true;
    }
    bool Shutdown() override { return true; }
    bool Uninitialize() override { return true; }
    bool Lock(eLockFlags flags) override { lockFlags.push_back(static_cast<std::uint32_t>(flags)); return true; }
    bool Unlock(eLockFlags flags) override { unlockFlags.push_back(static_cast<std::uint32_t>(flags)); return true; }
    bool IsLocked() const override { return false; }
    std::uint32_t Width() const override { const_cast<FakeFrameBuffer*>(this)->widthCalls++; return width; }
    std::uint32_t Height() const override { const_cast<FakeFrameBuffer*>(this)->heightCalls++; return height; }
    const cRZRect* GetBufferArea() const override { return &area; }
    void GetBufferArea(cRZRect& out) const override { out = area; }
    std::uint32_t GetMemoryUsage() const override { return 0; }
    cGZBufferColorType GetColorType() const override { return initType; }
    void GetColorType(cGZBufferColorType& out) const override { out = initType; }
    std::uint32_t GetBitsPerPixel() const override { return 0; }
    std::uint32_t GetBytesPerPixel() const override { return 0; }
    bool Fill(std::uint32_t color, const cRZRect&, const cRZRect*) override { fillColors.push_back(color); return true; }
    bool Fill(std::uint32_t, int, int, int, int, const cRZRect*) override { return true; }
    bool Fill(std::uint32_t color, const cRZRect* clip) override {
        (void)clip;
        fillColors.push_back(color);
        return true;
    }
    std::uint32_t GetPixel(std::uint32_t, std::uint32_t) const override { return 0; }
    void SetPixel(std::uint32_t, std::uint32_t, std::uint32_t) override {}
    void SetTransparency(std::uint32_t) override {}
    bool GetTransparency() const override { return false; }
    bool IsPixelTransparent(std::uint32_t, std::uint32_t) override { return false; }
    bool GetTransparentColor(std::uint32_t&) const override { return false; }
    void RemoveTransparency() override {}
    bool DrawLine(int, int, int, int, int, std::uint32_t) override { return true; }
    bool Blt(cIGZBuffer* source, const cRZRect& src, const cRZRect& dst, const cRZRect*) override {
        blts.push_back({source, src, dst});
        return true;
    }
    std::uint32_t ConvertRGBValueToNative(std::uint8_t, std::uint8_t, std::uint8_t) override { return 0; }
    bool ConvertNativeValueToRGB(std::uint32_t, std::uint8_t&, std::uint8_t&, std::uint8_t&) override { return false; }
    std::uint32_t ConvertRGBAValueToNative(std::uint8_t, std::uint8_t, std::uint8_t, std::uint8_t) override { return 0; }
    bool ConvertNativeValueToRGBA(std::uint32_t, std::uint8_t&, std::uint8_t&, std::uint8_t&, std::uint8_t&) override {
        return false;
    }
    void* GetColorSurfaceBits() override { return nullptr; }
    std::uint32_t GetColorSurfaceStride() override { return 0; }
    std::uint32_t GetChangeCounter() override { return 0; }
    cIGZBufferHardwareCache* GetHardwareCache() override { return nullptr; }
    bool SetHardwareCache(cIGZBufferHardwareCache*) override { return false; }
    bool IsReady() override { return true; }

    bool HasBlt(cIGZBuffer* source, const cRZRect& src, const cRZRect& dst) const {
        for (const BltRecord& b : blts) {
            if (b.source == source && SameRect(b.src, src) && SameRect(b.dst, dst)) return true;
        }
        return false;
    }
    void Clear() {
        blts.clear();
        lockFlags.clear();
        unlockFlags.clear();
        fillColors.clear();
    }
    static bool SameRect(const cRZRect& a, const cRZRect& b) {
        return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
    }
};

// A window draw buffer, enough for the final blit's Lock / Blt / Unlock (through the host).
struct FakeWindowBuffer final : IWinBuffer {
    int refs = 1;
    cRZRect bounds{0, 0, 0, 0};
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    const cRZRect* GetBounds() const override { return &bounds; }
    std::uint32_t MakeNativeColor(std::uint8_t, std::uint8_t, std::uint8_t) override { return 0; }
    void NativeToRGB(std::uint32_t, std::uint8_t&, std::uint8_t&, std::uint8_t&) override {}
    void* GetDrawContext() override { return nullptr; }
    bool SetFadeAlpha(std::uint8_t) override { return true; }
};

struct FakeFrameHost final : IWinTertiaryMenuFrameHost {
    std::vector<FakeFrameBuffer*> owned;
    FakeFrameBuffer* lastCreated = nullptr;
    IWinBuffer* lastWindowBuffer = nullptr;
    FakeFrameBuffer* windowBuffer = nullptr;
    int createCalls = 0;
    int windowBufferCalls = 0;
    bool createOk = true;
    cIGZBuffer* CreateBuffer() override {
        ++createCalls;
        if (!createOk) return nullptr;
        auto* buffer = new FakeFrameBuffer;
        buffer->refs = 1;
        owned.push_back(buffer);
        lastCreated = buffer;
        return buffer;
    }
    cIGZBuffer* WindowBuffer(IWinBuffer* buffer) override {
        ++windowBufferCalls;
        lastWindowBuffer = buffer;
        return windowBuffer;
    }
    ~FakeFrameHost() override {
        for (FakeFrameBuffer* buffer : owned) delete buffer;
    }
};

// A cursor manager that loads nothing (the frame only asks for it through cGZWin's manager hook).
struct NoCursors final : cIGZCursorManager {
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    cIGZCursor* LoadCursor(std::uint32_t) override { return nullptr; }
    void SetCursor(cIGZCursor*) override {}
};

// The graphics system the paint path gates the buffer factory on (RZ::GS() must exist); no screen buffer.
struct FakeWinGraphics final : IWinGraphics {
    std::uint32_t MakeNativeColor(std::uint8_t, std::uint8_t, std::uint8_t) override { return 0; }
    void NativeToRGB(std::uint32_t, std::uint8_t&, std::uint8_t&, std::uint8_t&) override {}
    IWinBuffer* PrimaryBuffer() override { return nullptr; }
    bool DrawSystemActive() override { return false; }
};

// A window manager that records where (and what) the frame posts, plus capture.
struct RecordingMgr final : cIGZWinMgr {
    std::set<cIGZWin*> valid;
    cIGZWin* capture = nullptr;
    cIGZWin* mainWin = nullptr;
    struct Routed { cIGZWin* target; cGZMessage message; };
    std::vector<Routed> routed;
    NoCursors cursors;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    cIGZWin* GetMainWindow() override { return mainWin; }
    void AppendToPlotStrategy(cIGZWin*) override {}
    void Plot() override {}
    void PostMessageToTarget(cIGZWin* target, const cGZMessage& message) override { routed.push_back({target, message}); }
    void AddWindowToValidList(cIGZWin* window) override { valid.insert(window); }
    void RemoveWindowFromValidList(cIGZWin* window) override { valid.erase(window); }
    void CleanUpWindowReferences(cIGZWin* window) override { if (capture == window) capture = nullptr; }
    bool DestroyWindow(cIGZWin* window) override { return window == nullptr ? false : (window->GetParentWin() ? window->GetParentWin()->ChildRemove(window) : true); }
    bool IsWindowValid(cIGZWin* window) override { return valid.count(window) != 0; }
    cIGZWin* GetWindowFromPoint(int, int) override { return nullptr; }
    void SetCurrentMouseWin(cIGZWin*) override {}
    cIGZWin* GZGetCapture() override { return capture; }
    void GZSetCapture(cIGZWin* window) override { capture = window; }
    void GZReleaseCapture(cIGZWin* window) override { if (capture == window) capture = nullptr; }
    cIGZCursorManager* GetCursorManager() override { return &cursors; }
    cIGZWin* GZGetFocus() override { return nullptr; }
    void GZSetFocus(cIGZWin*) override {}
    cIGZKeyboard* GetKeyboard() override { return nullptr; }
};

// Exposes cGZWin's protected draw target so the frame can be painted without a real window tree.
struct TestFrame final : cSC4WinTertiaryMenuFrame {
    using cSC4WinTertiaryMenuFrame::cSC4WinTertiaryMenuFrame;
    void SetDrawBufferForTest(IWinBuffer* buffer) { drawBuffer_ = buffer; }
    const cRZRect& DrawAreaForTest() const { return drawArea_; }
};

std::uint32_t Refs(cSC4WinTertiaryMenuFrame* frame) {
    const std::uint32_t value = frame->AddRef();
    frame->Release();
    return value - 1;
}

}  // namespace

int main() {
    RecordingMgr mgr;
    FakeWinGraphics graphics;
    SetWinGraphics(&graphics);

    // ---- 1. identity, reference counting and the interface split -------------------------------------------
    {
        auto* frame = new TestFrame(&mgr);
        mgr.AddWindowToValidList(frame);
        CHECK_EQ(frame->AddRef(), 1u);
        CHECK_EQ(frame->AddRef(), 2u);
        CHECK_EQ(frame->Release(), 1u);

        cISC4WinTertiaryMenuFrame* iface = frame;
        cGZWin* window = static_cast<cGZWin*>(frame);
        CHECK(iface->AsIGZWin() == window);
        CHECK(frame->AsIGZWin() == window);

        void* out = nullptr;
        CHECK(frame->QueryInterface(kGZIID_cISC4WinTertiaryMenuFrame, &out));
        CHECK(out == static_cast<cISC4WinTertiaryMenuFrame*>(frame));
        CHECK_EQ(frame->Release(), 1u);                       // the query's reference
        out = nullptr;
        CHECK(window->QueryInterface(kGZIID_cISC4WinTertiaryMenuFrame, &out));   // the same answer through the cGZWin half
        CHECK(out == static_cast<cISC4WinTertiaryMenuFrame*>(frame));
        CHECK_EQ(frame->Release(), 1u);
        out = nullptr;
        CHECK(window->QueryInterface(kGZIID_cIGZWin, &out));
        CHECK(out == window);
        CHECK_EQ(frame->Release(), 1u);
        out = nullptr;
        CHECK(!frame->QueryInterface(0xdeadbeef, &out));

        // Init: cGZWin's init flag plus the frame's two flag writes; no id is chosen.
        CHECK(frame->Init());
        CHECK(frame->GetFlag(kWinFlag4));
        CHECK(!frame->GetFlag(kWinPrivateBuffer));
        CHECK(frame->GetFlag(kWinInitialized));
        CHECK_EQ(frame->GetID(), 0u);
        CHECK(frame->Init());

        frame->Release();
    }

    // ---- 2. SetBackgroundImage / PositionFrame / GetContentRect -------------------------------------------
    {
        auto* frame = new TestFrame(&mgr);
        mgr.AddWindowToValidList(frame);
        frame->AddRef();
        cISC4WinTertiaryMenuFrame* iface = frame;

        auto* background = new FakeFrameBuffer;
        background->refs = 1;
        background->width = 200;
        background->height = 100;
        iface->SetBackgroundImage(background, 5, 7, 11, 13, 12, 19, 23);
        CHECK_EQ(background->refs, 2);
        CHECK_EQ(background->addRefs, 1);
        CHECK_EQ(background->heightCalls, 1);                 // the inner height is measured from the image

        // A second image replaces (and releases) the first; the same pointer again must not AddRef.
        auto* other = new FakeFrameBuffer;
        other->refs = 1;
        other->width = 80;
        other->height = 40;
        iface->SetBackgroundImage(other, 6, 8, 12, 14, 18, 20, 24);
        CHECK_EQ(background->refs, 1);
        CHECK_EQ(other->refs, 2);
        const int otherAddRefs = other->addRefs;
        iface->SetBackgroundImage(other, 6, 8, 12, 14, 18, 20, 24);
        CHECK_EQ(other->addRefs, otherAddRefs);
        CHECK_EQ(other->refs, 2);
        iface->SetBackgroundImage(background, 5, 7, 11, 13, 12, 19, 23);
        CHECK_EQ(background->refs, 2);
        CHECK_EQ(other->refs, 1);

        // Content height below the body tile height: the frame sits at the anchor and is one span tall.
        iface->PositionFrame(2, 3, 50, 100, 10, 200);
        CHECK_RECT(frame->GetArea(), 31, 70, 35, 97);
        cRZRect content{-999, -999, -999, -999};
        iface->GetContentRect(content);
        CHECK_RECT(content, 0, 12, 2, 15);

        // Content height above the body tile height and a minTop above the anchor-derived top: minTop wins.
        iface->PositionFrame(2, 40, 50, 60, 30, 200);
        CHECK_RECT(frame->GetArea(), 31, 30, 35, 84);
        iface->GetContentRect(content);
        CHECK_RECT(content, 0, 7, 2, 47);

        // Back to the first configuration for the point and paint tests.
        iface->PositionFrame(2, 3, 50, 100, 10, 200);
        CHECK_RECT(frame->GetArea(), 31, 70, 35, 97);

        // The frame's own IsPointInWindowWindowCoordinates asks for the arrow column, not the window (quirk).
        frame->SetArea(0, 0, 200, 100);
        frame->CalcAbsoluteArea();
        CHECK(iface->IsPointInWindowWindowCoordinates(199, 50));
        CHECK(iface->IsPointInWindowWindowCoordinates(195, 50));
        CHECK(!iface->IsPointInWindowWindowCoordinates(194, 50));
        CHECK(!iface->IsPointInWindowWindowCoordinates(10, 10));   // inside the window, outside the arrow column
        CHECK(!iface->IsPointInWindowWindowCoordinates(-1, 50));
        CHECK(!iface->IsPointInWindowWindowCoordinates(200, 50));
        CHECK(!iface->IsPointInWindowWindowCoordinates(10, 100));

        // Screen coordinates: converted by cGZWin to window coordinates first.
        frame->SetArea(10, 20, 210, 120);
        frame->CalcAbsoluteArea();
        CHECK(iface->IsPointInWindowScreenCoordinates(209, 50));    // window (199, 30)
        CHECK(!iface->IsPointInWindowScreenCoordinates(204, 50));   // window (194, 30)

        frame->Release();
        delete background;
        delete other;
    }

    // ---- 3. GZPaint: four tiles, arrow offsets, hover and hold ---------------------------------------------
    {
        FakeFrameHost host;
        SetWinTertiaryMenuFrameHost(&host);
        auto* frame = new TestFrame(&mgr);
        mgr.AddWindowToValidList(frame);
        frame->AddRef();
        cISC4WinTertiaryMenuFrame* iface = frame;

        CHECK(frame->Init());
        auto* background = new FakeFrameBuffer;
        background->refs = 1;
        background->width = 200;
        background->height = 100;
        // The metrics the game itself passes at 0x10078d46c: margins 53/25, body tile 80x53, right column 4,
        // anchor offsets 27/29 (the frame ends up 129 wide).
        iface->SetBackgroundImage(background, 53, 25, 80, 53, 4, 27, 29);
        iface->PositionFrame(2, 3, 50, 100, 10, 200);
        CHECK_RECT(frame->GetArea(), 23, 46, 152, 149);   // 129 x 103, inner height 50
        CHECK_EQ(frame->GetW(), 129);
        CHECK_EQ(frame->GetH(), 103);

        FakeWindowBuffer windowBuffer;      // what cGZWin holds at +0x98
        FakeFrameBuffer drawTarget;         // the same buffer as the cIGZBuffer the host returns
        frame->SetDrawBufferForTest(&windowBuffer);
        host.windowBuffer = &drawTarget;
        CHECK_RECT(frame->DrawAreaForTest(), 23, 46, 152, 149);

        // 3a. First paint: the scratch buffer is created and initialised at the window size, cleared, then the
        //     four tiles are composed; the finished buffer is blitted to the window's draw buffer.
        CHECK(frame->GZPaint());
        CHECK_EQ(host.createCalls, 1);
        FakeFrameBuffer* scratch = host.lastCreated;
        CHECK(scratch != nullptr);
        CHECK_EQ(scratch->inits, 1);
        CHECK_EQ(scratch->initType.type, static_cast<int>(kBufferColorDisplay));
        CHECK_EQ(scratch->initType.bitsPerPixel, 0u);
        CHECK_EQ(scratch->width, 129u);
        CHECK_EQ(scratch->height, 103u);
        CHECK_EQ(scratch->lockFlags.size(), 2u);
        CHECK_EQ(scratch->lockFlags[0], 0x8001u);   // clear pass: read | write
        CHECK_EQ(scratch->lockFlags[1], 0x8010u);
        CHECK_EQ(scratch->fillColors.size(), 1u);
        CHECK_EQ(scratch->fillColors[0], 0u);
        CHECK_EQ(scratch->unlockFlags.size(), 2u);
        CHECK_EQ(scratch->unlockFlags[0], 0x8001u);
        CHECK_EQ(scratch->unlockFlags[1], 0x8010u);
        // Arrows disabled: the source column starts at the body tile width.
        CHECK(scratch->HasBlt(background, cRZRect{80, 0, 133, 25}, cRZRect{76, 0, 129, 25}));
        CHECK(scratch->HasBlt(background, cRZRect{80, 75, 133, 100}, cRZRect{76, 78, 129, 103}));
        CHECK(scratch->HasBlt(background, cRZRect{0, 0, 80, 53}, cRZRect{0, 25, 80, 78}));
        CHECK_EQ(scratch->blts.size() >= 5u, true);   // 3 explicit tiles + the edge tile's two rows
        CHECK_EQ(scratch->blts.front().src.left, 80);   // top corner first
        CHECK_EQ(scratch->blts.back().src.left, 0);     // body last
        CHECK_EQ(scratch->blts.back().dst.top, 25);     // at the content inset
        for (const auto& blt : scratch->blts) {
            if (blt.source == background && blt.src.left == 80 && blt.src.top == 25) {
                CHECK_EQ(blt.src.right, 133);           // the right-edge source column
                CHECK_EQ(blt.dst.left, 76);
            }
        }
        CHECK_EQ(host.windowBufferCalls, 1);
        CHECK(host.lastWindowBuffer == &windowBuffer);
        CHECK_EQ(drawTarget.blts.size(), 1u);
        CHECK(drawTarget.blts[0].source == scratch);
        CHECK_RECT(drawTarget.blts[0].src, 0, 0, 129, 103);   // the scratch buffer's area
        CHECK_RECT(drawTarget.blts[0].dst, 23, 46, 152, 149); // cGZWin's draw area
        CHECK_EQ(drawTarget.lockFlags.size(), 1u);
        CHECK_EQ(drawTarget.lockFlags[0], 0x8010u);

        // 3b. A paint without the repaint flag only blits the composed buffer again.
        scratch->Clear();
        drawTarget.blts.clear();
        drawTarget.lockFlags.clear();
        CHECK(frame->GZPaint());
        CHECK_EQ(host.createCalls, 1);
        CHECK_EQ(scratch->inits, 1);
        CHECK_EQ(scratch->lockFlags.size(), 0u);
        CHECK_EQ(scratch->blts.size(), 0u);
        CHECK_EQ(drawTarget.blts.size(), 1u);

        // 3c. Enabling the arrows moves the tile source columns right by one margin (53).
        iface->EnableScrollArrow(true, true);
        scratch->Clear();
        drawTarget.blts.clear();
        CHECK(frame->GZPaint());
        CHECK(scratch->HasBlt(background, cRZRect{133, 0, 186, 25}, cRZRect{76, 0, 129, 25}));
        CHECK(scratch->HasBlt(background, cRZRect{133, 75, 186, 100}, cRZRect{76, 78, 129, 103}));

        // The same call must not force another compose.
        iface->EnableScrollArrow(true, true);
        scratch->Clear();
        CHECK(frame->GZPaint());
        CHECK_EQ(scratch->lockFlags.size(), 0u);

        // 3d. Hovering the up arrow adds two more margins to its source column.
        CHECK(frame->GZOnMouseMove(100, 10, 0));   // arrow column, above the content (content top is 50)
        scratch->Clear();
        CHECK(frame->GZPaint());
        CHECK(scratch->HasBlt(background, cRZRect{239, 0, 292, 25}, cRZRect{76, 0, 129, 25}));
        CHECK(scratch->HasBlt(background, cRZRect{133, 75, 186, 100}, cRZRect{76, 78, 129, 103}));

        // A move inside the same arrow does not repaint.
        scratch->Clear();
        CHECK(frame->GZOnMouseMove(101, 10, 0));
        CHECK(frame->GZPaint());
        CHECK_EQ(scratch->lockFlags.size(), 0u);

        // 3e. Pressing the up arrow: held state suppresses the extra hover strip (one margin less), posts -1,
        //     takes capture and starts the 300 ms auto-repeat timer.
        cGZWin target(&mgr);
        frame->SetNotificationTarget(&target);
        const std::uint32_t refsBefore = Refs(frame);
        CHECK(frame->GZOnMouseDownL(100, 10, 0));
        CHECK(mgr.capture == static_cast<cGZWin*>(frame));
        CHECK_EQ(mgr.routed.size(), 1u);
        CHECK(mgr.routed[0].target == &target);
        CHECK_EQ(mgr.routed[0].message.type, kWinTertiaryMenuScrollMsg);
        CHECK_EQ(static_cast<std::int64_t>(mgr.routed[0].message.param1), -1);
        CHECK(g_timerCallback != nullptr);
        CHECK_EQ(g_timerInterval, kWinTertiaryMenuInitialScrollPeriodMS);
        CHECK_EQ(Refs(frame), refsBefore + 1);   // the timer holds the window

        scratch->Clear();
        CHECK(frame->GZPaint());
        CHECK(scratch->HasBlt(background, cRZRect{186, 0, 239, 25}, cRZRect{76, 0, 129, 25}));

        // The timer fires: the callback posts -1 again and switches the period to 100 ms.
        CHECK(g_timerCallback != nullptr);
        g_timerCallback(g_timerInterval, g_timerParam);
        CHECK_EQ(mgr.routed.size(), 2u);
        CHECK_EQ(mgr.routed[1].message.type, kWinTertiaryMenuScrollMsg);
        CHECK_EQ(static_cast<std::int64_t>(mgr.routed[1].message.param1), -1);
        CHECK_EQ(g_timerInterval, kWinTertiaryMenuScrollRepeatPeriodMS);
        CHECK_EQ(Refs(frame), refsBefore + 1);

        // 3f. Release: capture is dropped, the held side clears, the timer stops and lets the window go.
        CHECK(frame->GZOnMouseUpL(100, 10, 0));
        CHECK(mgr.capture == nullptr);
        CHECK(g_timerCallback == nullptr);
        CHECK_EQ(Refs(frame), refsBefore);
        scratch->Clear();
        CHECK(frame->GZPaint());   // the mouse-up repaint recomposes; hover is still the up arrow
        CHECK(scratch->HasBlt(background, cRZRect{239, 0, 292, 25}, cRZRect{76, 0, 129, 25}));

        // 3g. Mouse move / exit: leaving the arrow column and leaving the window both clear the hover.
        CHECK(frame->GZOnMouseMove(10, 10, 0));
        scratch->Clear();
        CHECK(frame->GZPaint());
        CHECK(scratch->HasBlt(background, cRZRect{133, 0, 186, 25}, cRZRect{76, 0, 129, 25}));
        CHECK(frame->GZOnMouseMove(100, 100, 0));   // below the content: the down arrow
        CHECK(frame->GZOnMouseExit(nullptr));
        scratch->Clear();
        CHECK(frame->GZPaint());
        CHECK(scratch->HasBlt(background, cRZRect{133, 0, 186, 25}, cRZRect{76, 0, 129, 25}));
        CHECK(scratch->HasBlt(background, cRZRect{133, 75, 186, 100}, cRZRect{76, 78, 129, 103}));

        // The strip between the two arrows (content top 50 .. content bottom 53) clears the hover as well.
        CHECK(frame->GZOnMouseMove(100, 10, 0));    // up
        CHECK(frame->GZOnMouseMove(100, 51, 0));    // neither arrow
        scratch->Clear();
        CHECK(frame->GZPaint());
        CHECK(scratch->HasBlt(background, cRZRect{133, 0, 186, 25}, cRZRect{76, 0, 129, 25}));

        // 3g2. Pressing the down arrow posts +1 and uses its own source column (held = one margin less).
        CHECK(frame->GZOnMouseMove(100, 100, 0));
        CHECK(frame->GZOnMouseDownL(100, 100, 0));
        CHECK(mgr.capture == static_cast<cGZWin*>(frame));
        CHECK_EQ(mgr.routed.size(), 3u);
        CHECK_EQ(mgr.routed[2].message.type, kWinTertiaryMenuScrollMsg);
        CHECK_EQ(static_cast<std::int64_t>(mgr.routed[2].message.param1), 1);
        CHECK(g_timerCallback != nullptr);
        scratch->Clear();
        CHECK(frame->GZPaint());
        CHECK(scratch->HasBlt(background, cRZRect{133, 0, 186, 25}, cRZRect{76, 0, 129, 25}));
        CHECK(scratch->HasBlt(background, cRZRect{186, 75, 239, 100}, cRZRect{76, 78, 129, 103}));
        CHECK(frame->GZOnMouseUpL(100, 100, 0));
        CHECK(mgr.capture == nullptr);
        CHECK(g_timerCallback == nullptr);

        // 3h. A size change (with a repaint) rebuilds the scratch buffer at the new size.
        frame->SetArea(0, 0, 130, 104);
        iface->EnableScrollArrow(false, true);
        scratch->Clear();
        CHECK(frame->GZPaint());
        CHECK_EQ(host.createCalls, 2);
        FakeFrameBuffer* resized = host.lastCreated;
        CHECK(resized != scratch);
        CHECK_EQ(scratch->refs, 0);   // the old scratch buffer was released by the factory branch
        CHECK_EQ(resized->inits, 1);
        CHECK_EQ(resized->width, 130u);
        CHECK_EQ(resized->height, 104u);
        scratch = resized;

        // 3i. Shutdown releases both buffers and stops the timer; the background keeps only the test's reference.
        CHECK(frame->Shutdown());
        CHECK(!frame->GetFlag(kWinInitialized));
        CHECK_EQ(background->refs, 1);
        CHECK_EQ(scratch->refs, 0);
        CHECK(frame->Shutdown());
        frame->Release();
        delete background;
        SetWinTertiaryMenuFrameHost(nullptr);
    }

    // ---- 4. GZPaint without a graphics system, and a failing buffer factory -------------------------------
    {
        FakeFrameHost host;
        SetWinTertiaryMenuFrameHost(&host);
        auto* frame = new TestFrame(&mgr);
        mgr.AddWindowToValidList(frame);
        frame->AddRef();
        cISC4WinTertiaryMenuFrame* iface = frame;

        CHECK(frame->Init());
        auto* background = new FakeFrameBuffer;
        background->refs = 1;
        background->width = 200;
        background->height = 100;
        iface->SetBackgroundImage(background, 53, 25, 80, 53, 4, 27, 29);
        iface->PositionFrame(2, 3, 50, 100, 10, 200);
        FakeWindowBuffer windowBuffer;      // what cGZWin holds at +0x98
        FakeFrameBuffer drawTarget;         // the same buffer as the cIGZBuffer the host returns
        frame->SetDrawBufferForTest(&windowBuffer);
        host.windowBuffer = &drawTarget;

        CHECK(frame->GZPaint());
        FakeFrameBuffer* scratch = host.lastCreated;
        CHECK_EQ(host.createCalls, 1);

        // The graphics system disappearing does not stop a correctly sized scratch buffer from being composed.
        SetWinGraphics(nullptr);
        iface->EnableScrollArrow(true, true);
        scratch->Clear();
        CHECK(frame->GZPaint());
        CHECK_EQ(host.createCalls, 1);
        CHECK_EQ(scratch->lockFlags.size(), 2u);
        CHECK(scratch->blts.size() > 4u);

        // A size mismatch with no graphics system re-inits the existing buffer instead of creating one.
        frame->SetArea(0, 0, 131, 104);
        iface->EnableScrollArrow(true, false);
        scratch->Clear();
        CHECK(frame->GZPaint());
        CHECK_EQ(host.createCalls, 1);
        CHECK_EQ(scratch->inits, 2);

        // A failing factory leaves the paint a no-op (the repaint flag is still cleared).
        SetWinGraphics(&graphics);   // the factory is only asked while RZ::GS() exists
        CHECK(frame->Shutdown());    // drops the scratch buffer
        host.createOk = false;
        iface->EnableScrollArrow(false, false);
        drawTarget.blts.clear();
        CHECK(frame->GZPaint());
        CHECK_EQ(host.createCalls, 2);
        CHECK_EQ(drawTarget.blts.size(), 0u);
        CHECK(frame->GZPaint());    // no flag left: nothing to do
        CHECK_EQ(drawTarget.blts.size(), 0u);

        frame->Release();
        delete background;
        SetWinGraphics(&graphics);
        SetWinTertiaryMenuFrameHost(nullptr);
    }

    // ---- 5. The destructor releases the buffers even when Shutdown never ran -------------------------------
    {
        FakeFrameHost host;
        SetWinTertiaryMenuFrameHost(&host);
        auto* frame = new TestFrame(&mgr);
        mgr.AddWindowToValidList(frame);
        frame->AddRef();
        cISC4WinTertiaryMenuFrame* iface = frame;
        auto* background = new FakeFrameBuffer;
        background->refs = 1;
        background->width = 100;
        background->height = 60;
        iface->SetBackgroundImage(background, 5, 7, 11, 13, 12, 19, 23);
        iface->PositionFrame(2, 3, 50, 100, 10, 200);
        FakeWindowBuffer windowBuffer;      // what cGZWin holds at +0x98
        FakeFrameBuffer drawTarget;         // the same buffer as the cIGZBuffer the host returns
        frame->SetDrawBufferForTest(&windowBuffer);
        host.windowBuffer = &drawTarget;
        CHECK(frame->GZPaint());
        FakeFrameBuffer* scratch = host.lastCreated;
        CHECK(scratch != nullptr);
        CHECK_EQ(background->refs, 2);
        CHECK_EQ(scratch->refs, 1);

        frame->Release();   // no Init/Shutdown: the destructor's Shutdown is a no-op and it releases by hand
        CHECK_EQ(background->refs, 1);
        CHECK_EQ(scratch->refs, 0);

        delete background;
        SetWinTertiaryMenuFrameHost(nullptr);
    }

    SetWinGraphics(nullptr);
    if (failures == 0) std::printf("sc4_wintertiarymenuframe: all checks passed\n");
    return failures == 0 ? 0 : 1;
}

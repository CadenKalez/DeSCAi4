// cGZWinDragAndDropManager: the drag lifecycle (start / 4-pixel threshold / target search / enter-leave-over /
// drop / cancel), the drop source proxy registration, the DragData payloads, the drag image window (placement,
// capture of the source window's private buffer, painting) and the quirks kept from 0x1007b10d0..0x1007b28f0.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <set>

#include "gz/DrawContext.h"
#include "gz/WinDragAndDropManager.h"
#include "gz/WinMgrBase.h"   // kWinFlagCanFocus (0x8000), the flag the drop-target search requires

#include "fake_drawcontext.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {

// ---------------------------------------------------------------- fakes

struct FakeCursors final : cIGZCursorManager {
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    cIGZCursor* LoadCursor(std::uint32_t) override { return nullptr; }
    void SetCursor(cIGZCursor*) override {}
};

struct FakeMgr final : cIGZWinMgr {
    std::set<cIGZWin*> valid;
    cIGZWin* mainWin = nullptr;
    cIGZWin *focus = nullptr, *capture = nullptr, *mouseWin = nullptr;
    int mouseX = 0, mouseY = 0;
    int mousePosQueries = 0;
    int cleanups = 0;
    FakeCursors cursors;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    cIGZWin* GetMainWindow() override { return mainWin; }
    void AppendToPlotStrategy(cIGZWin*) override {}
    void Plot() override {}
    void PostMessageToTarget(cIGZWin*, const cGZMessage&) override {}
    void AddWindowToValidList(cIGZWin* w) override { valid.insert(w); }
    void RemoveWindowFromValidList(cIGZWin* w) override { valid.erase(w); }
    void CleanUpWindowReferences(cIGZWin* w) override {
        ++cleanups;
        if (focus == w) focus = nullptr;
        if (capture == w) capture = nullptr;
        if (mouseWin == w) mouseWin = nullptr;
    }
    bool DestroyWindow(cIGZWin* w) override { return w && w->GetParentWin() ? w->GetParentWin()->ChildRemove(w) : true; }
    bool IsWindowValid(cIGZWin* w) override { return valid.count(w) != 0; }
    cIGZWin* GetWindowFromPoint(int, int) override { return nullptr; }
    void GetMousePosition(int& x, int& y) override { ++mousePosQueries; x = mouseX; y = mouseY; }   // cIGZWinMgr vslot 0x108
    void GetCursorPos(cIGZWin*, int& x, int& y) override { x = mouseX; y = mouseY; }
    void SetCurrentMouseWin(cIGZWin* w) override { mouseWin = w; }
    cIGZWin* GZGetCapture() override { return capture; }
    void GZSetCapture(cIGZWin* w) override { capture = w; }
    void GZReleaseCapture(cIGZWin* w) override { if (capture == w) capture = nullptr; }
    cIGZCursorManager* GetCursorManager() override { return &cursors; }
    cIGZWin* GZGetFocus() override { return focus; }
    void GZSetFocus(cIGZWin* w) override { focus = w; }
    cIGZKeyboard* GetKeyboard() override { return nullptr; }
    int RunPopup(cIGZWin*) override { return -1; }
};

// A window whose private buffer can be injected (the drag image is copied from it).
struct Win final : cGZWin {
    using cGZWin::cGZWin;
    void InjectPrivateBuffer(IWinBuffer* buffer) { privateBuffer_ = buffer; }
};

// Records every message that reaches a window and consumes it. Heap allocated: the window holds the only
// reference and releases (and so deletes) it when destroyed.
struct RecFilter final : cIGZWinMessageFilter {
    std::vector<cGZMessage> received;
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return std::uint32_t(++refs); }
    std::uint32_t Release() override {
        if (--refs <= 0) {
            delete this;
            return 0;
        }
        return std::uint32_t(refs);
    }
    bool DoMessage(cIGZWin*, cGZMessage& message) override {
        received.push_back(message);
        return true;
    }
};

struct FakeBuffer final : IWinBuffer {
    int refs = 1;
    cRZRect bounds{0, 0, 0, 0};
    bool initOk = true, transparency = false;
    int inits = 0, fills = 0, blts = 0;
    std::uint32_t fillColor = 0xffffffffu, transparentColor = 0x12345678u;
    cRZRect filledRect{};
    IWinBuffer* bltSource = nullptr;
    cRZRect bltSourceRect{}, bltDestRect{};
    void* context = nullptr;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return std::uint32_t(++refs); }
    std::uint32_t Release() override { return std::uint32_t(--refs); }
    const cRZRect* GetBounds() const override { return &bounds; }
    std::uint32_t MakeNativeColor(std::uint8_t, std::uint8_t, std::uint8_t) override { return 0; }
    void NativeToRGB(std::uint32_t, std::uint8_t&, std::uint8_t&, std::uint8_t&) override {}
    void* GetDrawContext() override { return context; }
    bool SetFadeAlpha(std::uint8_t) override { return true; }
    bool Init(int w, int h) override { ++inits; bounds = {0, 0, w, h}; return initOk; }
    bool Fill(std::uint32_t color, const cRZRect& rect) override {
        ++fills; fillColor = color; filledRect = rect; return true;
    }
    bool GetTransparentColor(std::uint32_t& color) override {
        if (!transparency) return false;
        color = transparentColor;
        return true;
    }
    bool Blt(IWinBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect) override {
        ++blts; bltSource = source; bltSourceRect = sourceRect; bltDestRect = destRect; return true;
    }
    gz::IDrawTargetBuffer* AsDrawSource() override { return reinterpret_cast<gz::IDrawTargetBuffer*>(this); }
};

struct FakeGfx final : IWinGraphics {
    std::vector<FakeBuffer*> created;
    FakeBuffer* primary = nullptr;
    std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override {
        return 0xff000000u | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b;
    }
    void NativeToRGB(std::uint32_t n, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override {
        r = std::uint8_t((n >> 16) & 0xff); g = std::uint8_t((n >> 8) & 0xff); b = std::uint8_t(n & 0xff);
    }
    IWinBuffer* PrimaryBuffer() override { return primary; }
    bool DrawSystemActive() override { return false; }
    IWinBuffer* GraphicsCreateBuffer() override {
        auto* buffer = new FakeBuffer;
        created.push_back(buffer);
        return buffer;
    }
    bool GetPrivateBufferKeyColor(std::uint32_t& color) override { color = 0x00ff00ffu; return true; }
};

std::uintptr_t Ptr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

// ---------------------------------------------------------------- fixture

struct Fixture {
    FakeMgr mgr;
    FakeGfx gfx;
    gz::cGZWinDragAndDropManager* m = nullptr;
    std::vector<cGZWin*> windows;   // every window the test created, in creation order (each holds one reference)
    std::vector<FakeBuffer*> buffers;

    Fixture() {
        SetWinGraphics(&gfx);
        m = new gz::cGZWinDragAndDropManager(&mgr);
        m->AddRef();   // the reference the COM class factory would hold
    }
    ~Fixture() {
        m->Shutdown();               // frees the DragData and clears the proxy list
        m->SetParentWin(nullptr);    // the windows below are about to go away
        for (cGZWin* window : windows) window->Release();   // parents first: they release their children
        m->Release();
        for (FakeBuffer* buffer : buffers) delete buffer;
        for (FakeBuffer* buffer : gfx.created) delete buffer;
        SetWinGraphics(nullptr);
    }
    Win* NewWindow(int l, int t, int r, int b, std::uint32_t id = 0) {
        auto* window = new Win(&mgr);
        window->SetID(id);
        window->SetArea(l, t, r, b);
        window->AddRef();   // the test's own reference
        windows.push_back(window);
        return window;
    }
    // A child window: the parent owns the only reference.
    Win* NewChild(cGZWin* parent, int l, int t, int r, int b, std::uint32_t id = 0) {
        auto* window = new Win(&mgr);
        window->SetID(id);
        window->SetArea(l, t, r, b);
        window->AddRef();   // the test's own reference
        windows.push_back(window);
        if (!parent->ChildAdd(window)) {
            std::printf("FAIL: ChildAdd\n");
            ++failures;
        }
        return window;
    }
    FakeBuffer* NewBuffer(int w, int h) {
        auto* buffer = new FakeBuffer;
        buffer->bounds = {0, 0, w, h};
        buffers.push_back(buffer);
        return buffer;
    }
    cGZWin* Image() { return static_cast<cGZWin*>(m); }
};

// Builds a complete DragData (source + target + image) in the manager.
void StartCompleteDrag(Fixture& fx, cIGZWin* source, cIGZWin* target, IWinBuffer* image, int x, int y) {
    cRZVariant payload(static_cast<cIGZUnknown*>(source));
    gz::cIGZWinDragAndDropManager::DragData data;
    fx.m->CreateDragData(&data, gz::kDragTypeWindow, payload, source, gz::kDragMagicLeft, 1, 2, image, image);
    data.target = target;
    data.x = x;
    data.y = y;
    fx.m->SetDragData(&data);
}

// ---------------------------------------------------------------- tests

void TestLayoutAndInterfaces() {
    Fixture fx;
    auto* m = fx.m;
    CHECK(sizeof(cGZWin) == 0x150);
    CHECK(sizeof(gz::cGZWinDragAndDropManager) == 0x1b0);
    CHECK(reinterpret_cast<char*>(static_cast<cIGZWinMessageFilter*>(m)) - reinterpret_cast<char*>(m) == 0x8);
    CHECK(reinterpret_cast<char*>(static_cast<cRZSystemService*>(m)) - reinterpret_cast<char*>(m) == 0x10);
    CHECK(reinterpret_cast<char*>(static_cast<cGZWin*>(m)) - reinterpret_cast<char*>(m) == 0x38);
    CHECK(static_cast<gz::cIGZWinDragAndDropManager*>(m) == m);
    CHECK(m->GetServiceID() == 0x0c0874c6);
    CHECK(m->GetServicePriority() == 0x1312cf);

    void* view = nullptr;
    CHECK(m->QueryInterface(0x4c0874d7, &view) && view == static_cast<gz::cIGZWinDragAndDropManager*>(m));
    CHECK(m->QueryInterface(0xc6ae7085, &view) && view == static_cast<cIGZWinMessageFilter*>(m));
    CHECK(m->QueryInterface(0x287fb697, &view) && view == static_cast<cRZSystemService*>(m));
    CHECK(m->QueryInterface(0x22ba0121, &view) && view == static_cast<cGZWin*>(m));
    CHECK(!m->QueryInterface(0x12345678, &view));
    // the fixture holds 1, the four successful queries took 4 more
    CHECK(m->Release() == 4u);
    CHECK(m->Release() == 3u);
    CHECK(m->Release() == 2u);
    CHECK(m->Release() == 1u);
}

void TestInitShutdown() {
    Fixture fx;
    auto* m = fx.m;
    CHECK(m->GetDragData() == nullptr);
    CHECK(!m->IsDragging());
    CHECK(m->Init());
    auto* data = m->GetDragData();
    CHECK(data != nullptr);
    CHECK(data->type == 0);
    CHECK(data->magic == gz::kDragMagicLeft);   // Init pre-fills the persistent DragData with the left-drag magic
    CHECK(data->source.get() == nullptr && data->target.get() == nullptr);
    CHECK(data->image.get() == nullptr && data->imageCopy.get() == nullptr);
    CHECK(data->startX == 0 && data->startY == 0 && data->x == 0 && data->y == 0);
    CHECK(data->imageMode == 0 && !data->cancelOnMouseUp);
    CHECK(fx.Image()->GetFlag(0x40000));
    CHECK(fx.Image()->GetFlag(kWinPrivateBufferTransparent));
    CHECK(fx.Image()->GetFlag(kWinFlag4));
    CHECK(fx.Image()->GetFlag(kWinInitialized));
    CHECK(m->Init());                        // a second Init keeps the same DragData
    CHECK(m->GetDragData() == data);
    CHECK(m->Shutdown());
    CHECK(m->GetDragData() == nullptr);
    CHECK(!m->IsDragging());
    CHECK(!m->Shutdown());                   // not initialised: false
}

void TestDragData() {
    Fixture fx;
    auto* m = fx.m;
    m->Init();
    Win* source = fx.NewWindow(0, 0, 100, 100);
    FakeBuffer* image = fx.NewBuffer(40, 30);
    CHECK(!m->IsDragging());

    cRZVariant payload(static_cast<cIGZUnknown*>(source));
    gz::cIGZWinDragAndDropManager::DragData data;
    m->CreateDragData(&data, gz::kDragTypeWindow, payload, source, gz::kDragMagicLeft, 11, 22, image, nullptr);
    CHECK(data.type == gz::kDragTypeWindow);
    CHECK(data.value.GetValUnknown() == static_cast<cIGZUnknown*>(source));
    CHECK(data.source.get() == source);
    CHECK(data.startX == 11 && data.startY == 22);
    CHECK(data.magic == gz::kDragMagicLeft);
    CHECK(data.image.get() == image);
    CHECK(data.imageCopy.get() == nullptr);
    CHECK(data.target.get() == nullptr);
    CHECK(data.x == 0 && data.y == 0 && data.imageMode == 0 && !data.cancelOnMouseUp);
    CHECK(image->refs == 2);   // the test's reference and the DragData's

    m->SetDragData(&data);
    CHECK(m->IsDragging());
    auto* copy = m->GetDragData();
    CHECK(copy != &data);
    CHECK(copy->type == data.type);
    CHECK(copy->source.get() == source && copy->startX == 11 && copy->startY == 22);
    CHECK(copy->image.get() == image && copy->imageCopy.get() == nullptr);
    CHECK(copy->magic == gz::kDragMagicLeft);
    CHECK(image->refs == 3);   // ... and now the manager's DragData too

    // StartDrag copies, and a data with type 0 starts nothing (but still answers true)
    gz::cIGZWinDragAndDropManager::DragData second;
    second.type = gz::kDragTypeWindow;
    second.source = source;
    second.startX = 7;
    second.startY = 8;
    second.magic = gz::kDragMagicRight;
    CHECK(m->StartDrag(&second, true));
    CHECK(m->GetDragData()->startX == 7 && m->GetDragData()->startY == 8);
    CHECK(m->GetDragData()->magic == gz::kDragMagicRight);
    CHECK(m->GetDragData()->cancelOnMouseUp);
    CHECK(m->GetDragData()->image.get() == nullptr);   // `second` had no image

    CHECK(m->Drop());                                   // clears
    CHECK(!m->IsDragging());
    gz::cIGZWinDragAndDropManager::DragData empty;
    CHECK(m->StartDrag(&empty, false));
    CHECK(!m->IsDragging());
    CHECK(m->StartDrag(nullptr, false));
    CHECK(!m->IsDragging());

    // a manager that never ran Init refuses to do anything
    auto* fresh = new gz::cGZWinDragAndDropManager(&fx.mgr);
    fresh->AddRef();
    CHECK(!fresh->StartDrag(&second, false));
    CHECK(!fresh->CancelDrag());
    CHECK(!fresh->Drop());
    CHECK(!fresh->Shutdown());
    fresh->Release();
}

void TestProxyRegistration() {
    Fixture fx;
    auto* m = fx.m;
    m->Init();
    Win* source = fx.NewWindow(0, 0, 100, 100, 0x1111);

    // quirk: false when the window was registered, true when it was already there
    CHECK(!m->AddDropSourceProxy(source, true));
    CHECK(m->GetDragData()->source.get() == nullptr);
    CHECK(m->AddDropSourceProxy(source, true));

    auto* recorder = new RecFilter;
    source->AddMessageFilter(recorder);   // after the manager: the manager's filter runs first and returns false

    cGZMessage down;
    down.type = kGZMsgMouseDownL;
    down.param1 = 21;
    down.param2 = 31;
    source->DoMessage(down);   // the manager is one of the window's filters now
    CHECK(m->IsDragging());
    auto* data = m->GetDragData();
    CHECK(data->type == gz::kDragTypeWindow);
    CHECK(data->source.get() == source);
    CHECK(data->startX == 21 && data->startY == 31);
    CHECK(data->magic == gz::kDragMagicLeft);
    CHECK(data->cancelOnMouseUp);            // deferred: the pointer has not left its start cell
    CHECK(data->image.get() == nullptr && data->target.get() == nullptr);
    CHECK(data->value.GetValUnknown() == static_cast<cIGZUnknown*>(source));
    CHECK(recorder->received.size() == 1);   // the window itself still got the message

    cGZMessage up;
    up.type = kGZMsgMouseUpL;
    source->DoMessage(up);                   // the button came up in the start cell: a click, not a drag
    CHECK(!m->IsDragging());
    CHECK(recorder->received.size() == 2);

    // unregistering answers true (quirk) and the window stops driving the manager
    CHECK(m->AddDropSourceProxy(source, false));
    source->DoMessage(down);
    CHECK(!m->IsDragging());
}

void TestProxyById() {
    Fixture fx;
    auto* m = fx.m;
    m->Init();
    Win* parent = fx.NewWindow(0, 0, 200, 200);
    Win* child = fx.NewChild(parent, 10, 10, 60, 60, 0x2222);
    m->SetParentWin(parent);   // the drag image window lives under the same parent

    CHECK(!m->AddDropSourceProxy(0x2222, nullptr, true));   // found the child of the image window's parent
    CHECK(m->AddDropSourceProxy(0x2222, child, false));     // removal answers true (quirk)
    CHECK(m->AddDropSourceProxy(0x9999, nullptr, true) == false);   // no such child
    CHECK(!m->AddDropSourceProxy(0x2222, parent, true));    // explicit window
}

void TestDropCancelMessages() {
    Fixture fx;
    auto* m = fx.m;
    m->Init();
    Win* source = fx.NewWindow(0, 0, 100, 100);
    Win* target = fx.NewWindow(100, 0, 200, 100);
    FakeBuffer* image = fx.NewBuffer(8, 8);
    auto* sourceLog = new RecFilter;
    auto* targetLog = new RecFilter;
    source->AddMessageFilter(sourceLog);
    target->AddMessageFilter(targetLog);

    StartCompleteDrag(fx, source, target, image, 7, 8);
    auto* data = m->GetDragData();
    CHECK(m->Drop());
    CHECK(!m->IsDragging());
    CHECK(targetLog->received.size() == 1);
    const cGZMessage drop = targetLog->received.back();
    CHECK(drop.type == gz::kDragMsgDrop);
    CHECK(drop.param1 == Ptr(data));
    // The payload is (DragData*, x, y) - two zero-extended 32-bit coordinates, not a packed position and no image
    // (0x1007b1b5e: PMOVZXDQ of DragData +0x38 into message +0x10; the receiver reads the image from the DragData).
    CHECK(drop.param2 == 7);
    CHECK(drop.param3 == 8);
    CHECK(sourceLog->received.size() == 1);
    CHECK(sourceLog->received.back().type == gz::kDragMsgDrop);

    // a window the manager does not consider valid gets nothing
    StartCompleteDrag(fx, source, target, image, 7, 8);
    fx.mgr.valid.erase(target);
    CHECK(m->Drop());
    CHECK(targetLog->received.size() == 1);        // unchanged
    fx.mgr.valid.insert(target);

    // CancelDrag sends the cancel message and releases a capture it holds
    StartCompleteDrag(fx, source, target, image, 9, 10);
    fx.mgr.capture = fx.Image();
    CHECK(m->CancelDrag());
    CHECK(fx.mgr.capture == nullptr);
    CHECK(targetLog->received.size() == 2);
    CHECK(targetLog->received.back().type == gz::kDragMsgCancel);
    CHECK(targetLog->received.back().param2 == 9 && targetLog->received.back().param3 == 10);
    CHECK(!m->IsDragging());

    // a pending (not yet started) drag is cancelled without a message
    StartCompleteDrag(fx, source, target, image, 9, 10);
    m->GetDragData()->cancelOnMouseUp = true;
    CHECK(m->CancelDrag());
    CHECK(targetLog->received.size() == 2);        // unchanged
    CHECK(!m->IsDragging());

    // no source and no target: nothing to tell
    StartCompleteDrag(fx, nullptr, nullptr, nullptr, 0, 0);
    CHECK(m->Drop());
    CHECK(targetLog->received.size() == 2);
}

void TestMouseAndKeyHandlers() {
    Fixture fx;
    auto* m = fx.m;
    m->Init();
    Win* source = fx.NewWindow(0, 0, 100, 100);
    Win* target = fx.NewWindow(100, 0, 200, 100);
    FakeBuffer* image = fx.NewBuffer(8, 8);
    auto* targetLog = new RecFilter;
    target->AddMessageFilter(targetLog);

    // left mouse up drops a real drag ...
    StartCompleteDrag(fx, source, target, image, 3, 4);
    CHECK(m->GZOnMouseUpL(0, 0, 0));
    CHECK(!m->IsDragging());
    CHECK(targetLog->received.size() == 1 && targetLog->received.back().type == gz::kDragMsgDrop);

    // ... but cancels a pending one (the button came up inside the start cell)
    StartCompleteDrag(fx, source, target, image, 3, 4);
    m->GetDragData()->cancelOnMouseUp = true;
    CHECK(m->GZOnMouseUpL(0, 0, 0));
    CHECK(!m->IsDragging());
    CHECK(targetLog->received.size() == 1);          // still pending: cancelled without a message

    // the right button only reacts to a right-button drag (magic 0x0c0c7f51)
    StartCompleteDrag(fx, source, target, image, 3, 4);
    CHECK(m->GZOnMouseUpR(0, 0, 0));
    CHECK(m->IsDragging());                          // wrong magic: untouched
    m->GetDragData()->magic = gz::kDragMagicRight;
    CHECK(m->GZOnMouseUpR(0, 0, 0));
    CHECK(!m->IsDragging());
    CHECK(targetLog->received.back().type == gz::kDragMsgDrop);

    // escape cancels, any other key does not
    StartCompleteDrag(fx, source, target, image, 3, 4);
    CHECK(m->GZOnKeyDown('a', 0));
    CHECK(m->IsDragging());
    CHECK(m->GZOnKeyDown(0x1b, 0));
    CHECK(!m->IsDragging());
    CHECK(targetLog->received.back().type == gz::kDragMsgCancel);

    // losing the capture to another window cancels; the image window itself is fine
    StartCompleteDrag(fx, source, target, image, 3, 4);
    CHECK(m->GZOnCaptureChanged(nullptr, fx.Image()));
    CHECK(m->IsDragging());
    CHECK(m->GZOnCaptureChanged(nullptr, nullptr));
    CHECK(!m->IsDragging());

    // the mouse-down handlers cancel too
    StartCompleteDrag(fx, source, target, image, 3, 4);
    CHECK(m->GZOnMouseDownL(0, 0, 0));
    CHECK(!m->IsDragging());
    CHECK(targetLog->received.back().type == gz::kDragMsgCancel);
    StartCompleteDrag(fx, source, target, image, 3, 4);
    CHECK(m->GZOnMouseDownR(0, 0, 0));
    CHECK(!m->IsDragging());

    // quirk: a mouse move without a drag cancels (and drops a capture the image window holds)
    fx.Image()->GZSetCapture();
    CHECK(fx.mgr.capture == fx.Image());
    CHECK(m->GZOnMouseMove(0, 0, 0));
    CHECK(!m->IsDragging());
    CHECK(fx.mgr.capture == nullptr);

    // the wheel goes to cGZWin
    CHECK(!m->GZOnMouseWheel(1, 2, 0, 3));
}

void TestDeferredStart() {
    Fixture fx;
    auto* m = fx.m;
    m->Init();
    Win* source = fx.NewWindow(0, 0, 100, 100);   // same origin as the (not yet placed) drag image window
    auto* log = new RecFilter;
    source->AddMessageFilter(log);

    cRZVariant payload(static_cast<cIGZUnknown*>(source));
    gz::cIGZWinDragAndDropManager::DragData data;
    m->CreateDragData(&data, gz::kDragTypeWindow, payload, source, gz::kDragMagicLeft, 10, 10, nullptr, nullptr);
    CHECK(m->StartDrag(&data, true));
    CHECK(m->GetDragData()->cancelOnMouseUp);
    CHECK(log->received.empty());

    CHECK(m->GZOnMouseMove(11, 12, 0));   // 1,2 pixels away from the start point: still pending
    CHECK(m->IsDragging());
    CHECK(m->GetDragData()->cancelOnMouseUp);
    CHECK(log->received.empty());
    CHECK(!source->GetFlag(kWinPrivateBuffer));   // nothing was prepared

    source->SetArea(20, 0, 120, 100);             // move the source: the threshold now sees 20 pixels
    CHECK(m->GZOnMouseMove(30, 30, 0));           // begins
    CHECK(!m->GetDragData()->cancelOnMouseUp);
    CHECK(log->received.size() == 1);
    CHECK(log->received[0].type == gz::kDragMsgBegin);
    CHECK(log->received[0].param1 == Ptr(m->GetDragData()));
    CHECK(log->received[0].param2 == 0 && log->received[0].param3 == 0);   // quirk: the position before tracking
    CHECK(source->GetFlag(kWinPrivateBuffer));      // the begin path forces a private buffer on the source
    CHECK(!m->GetDragData()->image.get());
    CHECK(fx.mgr.focus == fx.Image());              // SetFocus on the drag image window
}

void TestTargetSearchAndImage() {
    Fixture fx;
    auto* m = fx.m;
    m->Init();
    Win* root = fx.NewWindow(0, 0, 400, 400);
    Win* source = fx.NewChild(root, 50, 60, 150, 160);
    Win* target = fx.NewChild(root, 10, 10, 200, 200, 0x3001);
    target->SetFlag(gz::kWinFlagCanFocus, true);
    FakeBuffer* sourceBuffer = fx.NewBuffer(100, 100);
    source->InjectPrivateBuffer(sourceBuffer);
    source->SetFlag(kWinPrivateBuffer, true);       // the flag the manager saves and restores

    fx.Image()->SetParentWin(root);   // parenting does not make the drag image window a child yet
    CHECK(!m->AddDropSourceProxy(source, true));

    cGZMessage down;
    down.type = kGZMsgMouseDownL;
    down.param1 = 21;
    down.param2 = 31;
    source->DoMessage(down);
    CHECK(m->IsDragging());
    CHECK(m->GetDragData()->cancelOnMouseUp);
    auto* sourceLog = new RecFilter;                 // after the mouse-down so only drag messages are recorded
    auto* targetLog = new RecFilter;
    source->AddMessageFilter(sourceLog);             // and after the manager: its filter must run first
    target->AddMessageFilter(targetLog);
    const int childrenBefore = static_cast<int>(root->GetChildCount());

    // the source sits at 50,60 from the drag image window: the first move begins the drag
    fx.mgr.mouseX = 15;
    fx.mgr.mouseY = 15;
    CHECK(m->GZOnMouseMove(21, 31, 0));
    CHECK(!m->GetDragData()->cancelOnMouseUp);
    CHECK(static_cast<int>(root->GetChildCount()) == childrenBefore + 1);   // the image window was added
    CHECK(fx.Image()->GetL() == 71 && fx.Image()->GetT() == 91);        // start point in root coordinates
    CHECK(fx.Image()->GetW() == 16 && fx.Image()->GetH() == 16);        // no image yet: 16x16 placeholder
    CHECK(sourceLog->received.size() == 1 && sourceLog->received[0].type == gz::kDragMsgBegin);

    // the second move captures the source's private buffer into a new image buffer
    const std::size_t createdBefore = fx.gfx.created.size();
    CHECK(m->GZOnMouseMove(30, 40, 0));
    auto* data = m->GetDragData();
    CHECK(fx.gfx.created.size() == createdBefore + 1);
    FakeBuffer* image = fx.gfx.created.back();
    CHECK(data->image.get() == image);
    CHECK(data->imageCopy.get() == image);          // both slots hold the same buffer
    CHECK(image->inits == 1);                       // sized like the source buffer
    CHECK(image->fills == 1);                       // cleared to the (unset, 0) transparent colour first
    CHECK(image->blts == 1);
    CHECK(image->bltSource == sourceBuffer);
    CHECK(image->bltSourceRect.left == 0 && image->bltSourceRect.right == 100);
    CHECK(image->bltDestRect.right == 100 && image->bltDestRect.bottom == 100);
    CHECK(fx.Image()->GetW() == 100 && fx.Image()->GetH() == 100);      // the image window takes the image size
    CHECK(fx.Image()->GetL() == 80 && fx.Image()->GetT() == 100);       // GZWinOffset by the pointer delta
    CHECK(data->x == 30 && data->y == 40);
    CHECK(source->GetFlag(kWinPrivateBuffer));      // restored to the value the begin path saved
    CHECK(targetLog->received.size() == 2);         // enter + over
    CHECK(targetLog->received[0].type == gz::kDragMsgEnter);
    CHECK(targetLog->received[1].type == gz::kDragMsgOver);
    CHECK(targetLog->received[1].param1 == Ptr(data));
    CHECK(targetLog->received[1].param2 == 30);
    CHECK(targetLog->received[1].param3 == 40);
    CHECK(data->target.get() == target);

    // a third move elsewhere leaves the target
    fx.mgr.mouseX = 300;
    fx.mgr.mouseY = 300;
    CHECK(m->GZOnMouseMove(40, 50, 0));
    CHECK(data->target.get() == nullptr);
    CHECK(targetLog->received.size() == 3);
    CHECK(targetLog->received[2].type == gz::kDragMsgLeave);
    // every notification goes to the target and the source: begin, enter, over, leave, over
    CHECK(sourceLog->received.size() == 5);
    CHECK(sourceLog->received[1].type == gz::kDragMsgEnter);
    CHECK(sourceLog->received[3].type == gz::kDragMsgLeave);

    // moving again does not re-add the already placed image window
    const int childrenPlaced = static_cast<int>(root->GetChildCount());
    CHECK(m->GZOnMouseMove(45, 55, 0));
    CHECK(static_cast<int>(root->GetChildCount()) == childrenPlaced);

    // painting: the image is blitted at the window's draw area, from the slot imageMode selects
    fake_dc::Ctx context;
    FakeBuffer* primary = fx.NewBuffer(400, 400);
    primary->context = &context;
    fx.gfx.primary = primary;
    fx.Image()->SetArea(5, 6, 105, 106);
    fx.Image()->SetBufferToDrawTo();
    CHECK(m->GZPaint());
    CHECK(context.log.size() == 1);
    CHECK(context.log[0] == "blt 0,0,100,100>5,6,105,106");
    m->GetDragData()->imageMode = 1;                // imageCopy (the same buffer here)
    CHECK(m->GZPaint());
    CHECK(context.log.size() == 2);

    // idle painting removes the image window from its parent again
    CHECK(m->Drop());
    CHECK(m->GZPaint());
    CHECK(fx.Image()->GetParentWin() == nullptr);
    CHECK(static_cast<int>(root->GetChildCount()) == childrenPlaced - 1);
}

void TestTargetFlags() {
    Fixture fx;
    auto* m = fx.m;
    m->Init();
    Win* root = fx.NewWindow(0, 0, 400, 400);
    Win* source = fx.NewChild(root, 300, 300, 400, 400);
    Win* target = fx.NewChild(root, 10, 10, 200, 200);
    m->SetParentWin(root);
    fx.mgr.mouseX = 20;
    fx.mgr.mouseY = 20;
    StartCompleteDrag(fx, source, target, nullptr, 0, 0);

    // a window that may not take the focus is not a drop target (cIGZWin flag 0x8000; a fresh cGZWin has it set)
    CHECK(target->GetFlag(gz::kWinFlagCanFocus));
    target->SetFlag(gz::kWinFlagCanFocus, false);
    CHECK(m->GZOnMouseMove(310, 310, 0));
    CHECK(m->GetDragData()->target.get() == nullptr);
    target->SetFlag(gz::kWinFlagCanFocus, true);
    CHECK(m->GZOnMouseMove(311, 311, 0));
    CHECK(m->GetDragData()->target.get() == target);
    // ... and neither is an invisible or mouse-transparent one
    target->SetFlag(kWinVisible, false);
    const int queriesBefore = fx.mgr.mousePosQueries;
    CHECK(m->GZOnMouseMove(312, 312, 0));
    CHECK(m->GetDragData()->target.get() == nullptr);
    // Quirk kept (0x1007b0fd0): the callback reads the pointer position before it tests the window, so both children
    // enumerated at the root (the dragged-from window and the invisible target) are queried.
    CHECK(fx.mgr.mousePosQueries - queriesBefore == 2);
    target->SetFlag(kWinVisible, true);
    target->SetFlag(kWinIgnoreCursor, true);
    CHECK(m->GZOnMouseMove(313, 313, 0));
    CHECK(m->GetDragData()->target.get() == nullptr);
    target->SetFlag(kWinIgnoreCursor, false);
    CHECK(m->GZOnMouseMove(314, 314, 0));
    CHECK(m->GetDragData()->target.get() == target);

    // the drag source is never its own target
    fx.mgr.mouseX = 350;
    fx.mgr.mouseY = 350;
    CHECK(m->GZOnMouseMove(315, 315, 0));
    CHECK(m->GetDragData()->target.get() == nullptr);
}

}  // namespace

int main() {
    TestLayoutAndInterfaces();
    TestInitShutdown();
    TestDragData();
    TestProxyRegistration();
    TestProxyById();
    TestDropCancelMessages();
    TestMouseAndKeyHandlers();
    TestDeferredStart();
    TestTargetSearchAndImage();
    TestTargetFlags();
    std::printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}

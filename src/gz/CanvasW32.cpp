#include "gz/CanvasW32.h"

#include "gz/GZCOMDirector.h"
#include "gz/RZServices.h"
#include "gz/cIGZFrameWork.h"
#include "platform/WinCompat.h"
#include "render/gd/VideoModes.h"

using namespace gz;

namespace {
// the input events of the message classes
constexpr std::uint32_t kMouseMove = 0xd, kMouseWheel = 0xe, kLeftDown = 7, kRightDown = 8, kLeftUp = 10, kRightUp = 11;
constexpr std::uint32_t kKeyChar = 4, kKeyDown = 5, kKeyUp = 6;
constexpr std::uint32_t kMouseNotification = 0xfa0c6390;

template <class Message>
cRZAutoRefCount<Message> Clone(const cRZAutoRefCount<Message>& prototype, std::uint32_t iid) {
    cRZAutoRefCount<Message> copy;
    prototype->Create(iid, copy.AsOutParam());
    return copy;
}
}  // namespace

cGZCanvasW32::cGZCanvasW32() : cRZSystemService(kGZSID_cGZCanvasW32, kGZCanvasW32Priority) {
    canvasPrototype_ = cRZAutoRefCount<cGZCanvasMessage>(new cGZCanvasMessage());
    mousePrototype_ = cRZAutoRefCount<cGZMouseMessage>(new cGZMouseMessage());
    keyPrototype_ = cRZAutoRefCount<cGZKeyMessage>(new cGZKeyMessage());
    cRZAutoRefCount<cIGZUnknown> server1, server2;
    if (cIGZFrameWork* framework = RZGetFramework()) {
        framework->GetSystemService(0x64063387, 0xc1085722, server1.AsOutParam());
        framework->GetSystemService(0x04fa845b, 0x652294c7, server2.AsOutParam());
    }
    if (server1) messageServer1_ = static_cast<cIGZMessageServer*>(static_cast<void*>(server1.get()));
    if (server2) messageServer2_ = static_cast<cIGZMessageServer2*>(static_cast<void*>(server2.get()));
}

std::uint32_t cGZCanvasW32::Release() {
    if (refCount_ > 1) return --refCount_;
    FinalRelease();
    return 0;
}

bool cGZCanvasW32::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZCanvasW32 || iid == kGZIID_cIGZCanvasMessageTarget || iid == kGZIID_cIGZCanvas) {
        *out = this;
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

bool cGZCanvasW32::FinalRelease() {
    if (initialized_) Shutdown();
    delete this;
    return false;
}

// ---- messages ---------------------------------------------------------------------------------------------

void cGZCanvasW32::PostCanvas(std::uint32_t event) {
    auto message = Clone(canvasPrototype_, kGZIID_cIGZCanvasMessage);
    if (!message) return;
    message->Initialize(event);
    if (messageServer2_) messageServer2_->MessageSend(message.get());
}

void cGZCanvasW32::PostMouse(std::uint32_t event, int x, int y, std::uint32_t flags, std::int32_t wheel) {
    auto message = Clone(mousePrototype_, kGZIID_cIGZMouseMessage);
    if (!message) return;
    message->Initialize(event, cRZPoint{x, y}, flags, wheel);
    if (messageServer2_) messageServer2_->MessageSend(message.get());
}

void cGZCanvasW32::PostKey(std::uint32_t event, std::uint32_t key, std::uint32_t flags, std::uint32_t repeat) {
    auto message = Clone(keyPrototype_, kGZIID_cIGZKeyMessage);
    if (!message) return;
    message->Initialize(event, key, flags, repeat);
    if (messageServer2_) messageServer2_->MessageSend(message.get());
}

cRZPoint cGZCanvasW32::Clamp(int x, int y) const {
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    if (GetW() <= x) x = GetW() - 1;
    if (GetH() <= y) y = GetH() - 1;
    return {x, y};
}

// native flags (shift/control as MK_ bits in the high bits) plus the live state of the three mouse buttons
std::uint32_t cGZCanvasW32::Modifiers(std::uint32_t native, bool) {
    std::uint32_t flags = native >> 9 & 4 | native >> 0xb & 2 | native >> 9 & 1;
    if (wincompat::GetKeyState(1) < 0) flags |= 0x10;
    if (wincompat::GetKeyState(2) < 0) flags |= 0x20;
    if (wincompat::GetKeyState(4) < 0) flags |= 0x40;
    return flags;
}

void cGZCanvasW32::OnMouseMove(int x, int y, std::uint32_t nativeFlags) {
    if (mouseDisabled_ != 0) return;
    cRZPoint point = Clamp(x, y);
    std::uint32_t flags = Modifiers(nativeFlags, true);
    std::uint32_t buttons = mouseButtonState ? mouseButtonState() : 0;
    if ((buttons & 1) != 0) flags |= 0x10;
    if ((buttons & 2) != 0) flags |= 0x20;
    PostMouse(kMouseMove, point.x, point.y, flags, 0);
}

void cGZCanvasW32::OnMouseWheel(int x, int y, std::uint32_t nativeFlags) {
    if (mouseDisabled_ != 0) return;
    cRZPoint point = Clamp(x, y);
    int delta = static_cast<int>(nativeFlags) >> 16;
    int scaled = delta * 100;
    int notches = scaled < 120000 ? scaled / 0x78 : 100;     // wheel units (120 per notch) as a percentage, capped at 100
    PostMouse(kMouseWheel, point.x, point.y, Modifiers(nativeFlags, false), notches);
}

void cGZCanvasW32::OnLButtonDown(int x, int y, std::uint32_t nativeFlags) {
    if (mouseDisabled_ != 0) return;
    cRZPoint point = Clamp(x, y);
    PostMouse(kLeftDown, point.x, point.y, Modifiers(nativeFlags, false), 0);
}

void cGZCanvasW32::OnRButtonDown(int x, int y, std::uint32_t nativeFlags) {
    if (mouseDisabled_ != 0) return;
    cRZPoint point = Clamp(x, y);
    PostMouse(kRightDown, point.x, point.y, Modifiers(nativeFlags, false), 0);
}

void cGZCanvasW32::OnLButtonUp(int x, int y, std::uint32_t nativeFlags) {
    if (mouseDisabled_ != 0) return;
    cRZPoint point = Clamp(x, y);
    PostMouse(kLeftUp, point.x, point.y, Modifiers(nativeFlags, false), 0);
}

void cGZCanvasW32::OnRButtonUp(int x, int y, std::uint32_t nativeFlags) {
    if (mouseDisabled_ != 0) return;
    cRZPoint point = Clamp(x, y);
    PostMouse(kRightUp, point.x, point.y, Modifiers(nativeFlags, false), 0);
}

// The numeric keypad reports as the main digit row (and the operator keys as their main-row codes).
static std::uint32_t NumericKeypadKey(std::uint32_t key) {
    if (key >= 0x60 && key <= 0x69) return key - 0x30;
    switch (key) {
        case 0x6d: return 0xbd;
        case 0x6e: return 0xbe;
        case 0x6f: return 0xbf;
        default: return key;
    }
}

// Backspace, Tab, Enter and Delete are characters; every other key is a plain key-down. Characters are followed by a
// key-down unless the key is extended (flag 0x1000).
void cGZCanvasW32::OnKeyDown(std::uint32_t key, int repeat, std::uint32_t nativeFlags) {
    if (key == 0xe5 || keyboardDisabled_ != 0) return;
    bool plainKey = key - 0x70 < 0xf || key > 0x2e || ((0x400000002300ull >> (key & 0x3f)) & 1) == 0;
    std::uint32_t event = plainKey ? kKeyDown : kKeyChar;
    std::uint32_t sendEvent = (nativeFlags & 0x1000) == 0 ? event : kKeyDown;
    if (cIGZFrameWork* framework = RZGetFramework()) (void)framework;      // (the original asks the window manager to refresh its keyboard state here)
    std::uint32_t reported = NumericKeypadKey(key);
    std::uint32_t flags = Modifiers(nativeFlags, false);
    PostKey(sendEvent, reported, flags, static_cast<std::uint32_t>(repeat));
    if (!plainKey && (nativeFlags & 0x1000) == 0) PostKey(kKeyDown, reported, flags & ~0u, static_cast<std::uint32_t>(repeat));
}

void cGZCanvasW32::OnKeyUp(std::uint32_t key, int repeat, std::uint32_t nativeFlags) {
    if (key == 0xe5 || keyboardDisabled_ != 0) return;
    PostKey(kKeyUp, key, Modifiers(nativeFlags, false), static_cast<std::uint32_t>(repeat));
}

// A character in the framework's code page is sent at once; other code pages assemble multi-byte characters first.
void cGZCanvasW32::OnChar(char character, int repeat, std::uint32_t nativeFlags) {
    if (keyboardDisabled_ != 0) return;
    int frameworkPage = frameworkCodePage ? frameworkCodePage() : codePage_;
    std::uint32_t flags = Modifiers(nativeFlags, false);
    if (codePage_ == frameworkPage) {
        PostKey(kKeyChar, static_cast<std::uint32_t>(static_cast<int>(character)), flags, static_cast<std::uint32_t>(repeat));
        return;
    }
    pendingBytes_.push_back(character);
    if (expectedBytes_ == 0) expectedBytes_ = (followByteCount ? followByteCount(character, codePage_) : 0) + 1;
    if (pendingBytes_.size() >= static_cast<std::size_t>(expectedBytes_)) {
        std::string converted = convertToFramework ? convertToFramework(pendingBytes_, codePage_) : pendingBytes_;
        for (char c : converted) PostKey(kKeyChar, static_cast<std::uint32_t>(static_cast<int>(c)), flags, static_cast<std::uint32_t>(repeat));
        pendingBytes_.clear();
        expectedBytes_ = 0;
    }
}

void cGZCanvasW32::OnActivate(std::uint32_t, void*, int minimised) {
    PostCanvas(static_cast<std::uint32_t>(minimised == 0) | 2);
}

void cGZCanvasW32::ForceFullSystemRedraw() {
    if (!pendingRestore_) PostCanvas(kCanvasPaint);
}

bool cGZCanvasW32::DoMessage(cIGZMessage2* message) {
    if (message->GetType() == kMouseNotification) {
        cRZAutoRefCount<cIGZMouseMessage> mouse;
        if (message->QueryInterface(kGZIID_cIGZMouseMessage, mouse.AsOutParam()) && mouse->EventType() == kMouseMove) {
            cRZPoint position = mouse->Position();
            lastCursorX_ = position.x;
            lastCursorY_ = position.y;
        }
    }
    return true;
}

// ---- state ------------------------------------------------------------------------------------------------

void cGZCanvasW32::SetArea(int left, int top, int right, int bottom) { area_ = {left, top, right, bottom}; }

bool cGZCanvasW32::GetInputGenerationEnabled(std::uint32_t which) const {
    if (which == 2) return mouseDisabled_ == 0;
    if (which == 1) return keyboardDisabled_ == 0;
    return false;
}

// Disabling counts up (bit 1: keyboard, bit 2: mouse); the first mouse disable drops the message-server hook for the
// mouse notification, the last re-enable puts it back.
void cGZCanvasW32::SetInputGenerationEnabled(std::uint32_t which, bool enable) {
    if (enable) {
        if ((which & 1) != 0 && keyboardDisabled_ > 0) --keyboardDisabled_;
        if ((which & 2) != 0 && mouseDisabled_ > 0 && --mouseDisabled_ == 0 && messageServer2_)
            messageServer2_->RemoveNotification(static_cast<cIGZMessageTarget2*>(nullptr), kMouseNotification);
    } else {
        if ((which & 1) != 0) ++keyboardDisabled_;
        if ((which & 2) != 0 && mouseDisabled_++ == 0 && messageServer2_)
            messageServer2_->AddNotification(static_cast<cIGZMessageTarget2*>(nullptr), kMouseNotification);
    }
}

bool cGZCanvasW32::AddWinProcFilter(cIGZWinProcFilterW32* filter, bool add) {
    if (!filter) return false;
    auto existing = filters_.begin();
    for (; existing != filters_.end(); ++existing)
        if (existing->get() == filter) break;
    if (add) {
        if (existing != filters_.end()) return false;
        filters_.push_front(cRZAutoRefCount<cIGZWinProcFilterW32>(filter));
        return true;
    }
    if (existing == filters_.end()) return false;
    filters_.erase(existing);
    return true;
}

bool cGZCanvasW32::PostSystemMessage(cIGZVariant*) { return false; }   // (needs MacTools::PostWindowsEvent: host side)

void cGZCanvasW32::StaticWindowProc() {}

// ---- window life cycle ------------------------------------------------------------------------------------

// Creates the native window (or tears it down); a created window needs a video mode of the canvas size.
bool cGZCanvasW32::InitWindow(bool create) {
    gd::cIGZGDriver* driver = graphics_ ? graphics_->GetGDriver() : nullptr;
    if (!create || handle_ != nullptr) {
        if (driver) driver->SetVideoMode(-1, nullptr, true, true);
        handle_ = nullptr;
        surface_ = nullptr;
        PostCanvas(kCanvasWindowDestroyed);
        return true;
    }
    if (!driver || !graphics_) return false;
    std::uint32_t bits = graphics_->GetScreenPixelFormatBits() & 0x3f;
    int modes = driver->CountVideoModes();
    for (int i = 0; i < modes; ++i) {
        gd::sGDMode mode{};
        driver->GetVideoModeInfo(static_cast<std::uint32_t>(i), mode);
        bool fullScreen = (mode.flags[0] & 0xff) != 0;
        if (mode.width == static_cast<std::uint32_t>(GetW()) && mode.height == static_cast<std::uint32_t>(GetH()) &&
            fullScreen == graphics_->IsFullScreenMode() && mode.bitsPerPixel == bits) {
            void* window = reinterpret_cast<void*>(&cGZCanvasW32::StaticWindowProc);
            driver->SetVideoMode(i, window, false, graphics_->IsPageFlippingEnabled());
            if (driver->GetError() != 0) return false;
            if (realizeHandle) realizeHandle(*this);
            PostCanvas(kCanvasWindowCreated);
            return true;
        }
    }
    return false;
}

bool cGZCanvasW32::Init(const cRZRect& area) {
    if (initialized_) return true;
    SetArea(area.left, area.top, area.right, area.bottom);
    surface_ = nullptr;
    InitWindow(true);
    initialized_ = true;
    return true;
}

void cGZCanvasW32::Init(std::uint32_t windowStyle, std::uint32_t width, std::uint32_t height) {
    windowStyle_ = windowStyle;
    Init(cRZRect{0, 0, static_cast<int>(width), static_cast<int>(height)});
}

// Service start: needs the graphic system and the message server; the canvas takes the system's canvas rectangle and
// announces itself (event 0); a failure shuts the canvas down again.
bool cGZCanvasW32::Init() {
    if (graphics_ && messageServer2_) {
        if (Init(graphics_->CanvasRect())) {
            PostCanvas(kCanvasInitialized);
            return true;
        }
        Shutdown();
    }
    return false;
}

bool cGZCanvasW32::Shutdown() {
    if (initialized_) {
        PostCanvas(kCanvasShutdown);
        InitWindow(false);
        surface_ = nullptr;
        while (keyboardDisabled_ > 0) SetInputGenerationEnabled(1, true);
        while (mouseDisabled_ > 0) SetInputGenerationEnabled(2, true);
        graphics_ = nullptr;
        messageServer1_ = nullptr;
        messageServer2_ = nullptr;
        canvasPrototype_ = nullptr;
        mousePrototype_ = nullptr;
        keyPrototype_ = nullptr;
        initialized_ = false;
    }
    return true;
}

bool cGZCanvasW32::Uninitialize() {
    if (initialized_) {
        uninitializing_ = true;
        PostCanvas(kCanvasUninitialize);
        InitWindow(false);
        initialized_ = false;
    }
    return true;
}

bool cGZCanvasW32::Restore() {
    if (initialized_) return true;
    IGraphicSystemControl::DisplayMetrics metrics = graphics_->GetScreenMetrics();
    SetArea(0, 0, static_cast<int>(metrics.width), static_cast<int>(metrics.height));
    bool ok = InitWindow(true);
    initialized_ = true;
    if (!ok) {
        Shutdown();
        return false;
    }
    uninitializing_ = false;
    PostCanvas(kCanvasRestore);
    return true;
}

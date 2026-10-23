// cGZWinIMEProxy test: construction and interface layout, Init / Shutdown service wiring, the two floating windows
// (composition through the recovered cGZWinIMEComposition, candidate list through the IIMEProxyHost seam), the font
// acquisition quirks, the message dispatch, OnTick's focus probe and position override, the accessors and the
// shutdown / destructor release split.
//
// The fakes take no references on notification targets (the real message server does) so the object lifetimes stay
// under the test's control; everything else mirrors the interfaces the proxy talks to.
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "fake_winmgr.h"
#include "gz/Font.h"
#include "gz/FontSys.h"
#include "gz/GZCOMDirector.h"
#include "gz/WinCtrlMgr.h"
#include "gz/WinIMEProxy.h"
#include "gz/WinMgrBase.h"
#include "gz/WinStyle.h"
#include "rz/cRZString.h"
#include "rz/cRZSysServPtr.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace gz;

namespace {

// ------------------------------------------------------------------------------------------------------------------
// Fonts

struct FakeFont final : cIGZFont {
    std::string name = "Arial";
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(refs > 0 ? --refs : 0); }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    void GetName(cIGZString& out) const override { out.AssignFromCString(name.c_str()); }
    std::uint32_t GetStyle() const override { return 0; }
    bool GetStyle(int) const override { return false; }
    bool SetStyle(int, bool) override { return true; }
    std::uint32_t GetType() const override { return 0; }
    std::uint32_t GetSize() const override { return 0x12; }
    bool SetSize(std::uint32_t) override { return true; }
    std::uint32_t GetFontColor() const override { return 0; }
    std::uint32_t GetFontBackgroundColor() const override { return 0; }
    void SetFontColor(std::uint32_t, std::uint32_t) override {}
    int GetAntialiasing() override { return 0; }
    void SetAntialiasing(int) override {}
    float GetAntialiasingGammaCorrection() override { return 1; }
    void SetAntialiasingGammaCorrection(float) override {}
    float GetAntialiasingSharpness() override { return 1; }
    void SetAntialiasingSharpness(float) override {}
    void GetFilePath(cIGZString&) const override {}
    void SetFilePath(const cIGZString&) override {}
    bool GetCodePage(int&) const override { return true; }
    bool SetCodePage(int) override { return true; }
    bool IsCodePageAvailable(int) override { return true; }
    bool IsCharEncodingAvailable(int) override { return true; }
    bool IsCharAvailable(void*, std::uint32_t) override { return true; }
    bool PreloadChars(void*, std::uint32_t) override { return true; }
    void GetScaling(float& x, float& y) const override { x = y = 1; }
    void SetScaling(float, float) override {}
    void GetAdvanceScaling(float& x, float& y) const override { x = y = 1; }
    void SetAdvanceScaling(float, float) override {}
    void GetSpacingAdjustment(int& x, int& y) const override { x = y = 0; }
    void SetSpacingAdjustment(int, int) override {}
    int GetLineHeight() override { return 16; }
    int CharWidth(const void*, std::uint32_t length) override { return static_cast<int>(length) * 8; }
    int CharHeight(const void*, std::uint32_t) override { return 16; }
    int GetSizeAboveBaseline() override { return 12; }
    int GetSizeBelowBaseline() override { return 4; }
    int GetAdvanceWidth(const void*, std::uint32_t length) override { return static_cast<int>(length) * 8; }
    std::uint32_t CalculateWordsToFitInWidth(const void*, std::uint32_t length, std::uint32_t width, std::uint32_t*, WordWrapMode) override {
        const std::uint32_t fit = width / 8;
        if (fit == 0) return 1;   // the text edit loops forever on a zero answer
        return fit < length ? fit : length;
    }
    std::uint32_t CalculateCharsToFitInWidth(const void*, std::uint32_t length, std::uint32_t width, std::uint32_t*) override {
        const std::uint32_t fit = width / 8;
        return fit < length ? fit : length;
    }
    std::uint32_t CalculateNumberOfLines(const void*, std::uint32_t, std::uint32_t) override { return 1; }
    std::uint32_t CalculateWidthOfLines(const void*, std::uint32_t length, std::uint32_t) override { return length * 8; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, bool, bool) override { return true; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, int, int, int) override { return true; }
    bool TextArea(const void*, std::uint32_t, cRZRect& area) override { area = {0, 0, 16, 16}; return true; }
    int GetStringWidth(const void*, std::uint32_t length) override { return static_cast<int>(length) * 8; }
    int GetStringVisibleWidth(const void*, std::uint32_t length, std::uint32_t& visible) override {
        visible = length;
        return static_cast<int>(length) * 8;
    }
    bool FormatForEllipsis(cIGZString&, const cIGZString&, const cRZRect&, bool) override { return true; }
    bool DrawTextLine(void*, int, int, const void*, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, cRZPoint*) override { return true; }
    bool DrawTextParagraph(void*, const cRZRect&, const void*, std::uint32_t, int, std::uint32_t, std::uint32_t, const cRZRect*, cRZPoint*) override { return true; }
    cIGZFontRenderer* GetRenderer() const override { return nullptr; }
    void SetRenderer(cIGZFontRenderer*) override {}
    bool LockSurfaceForFontUse(void*) override { return true; }
    bool UnlockSurfaceForFontUse(void*) override { return true; }
};

// What cGZWinCtrlMgr hands out as a "system font": a cIGZFont view plus the control-font extras.
struct FakeWinFont final : IWinFont {
    FakeFont full;
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(refs > 0 ? --refs : 0); }
    cIGZFont* AsFont() override { return &full; }
    std::uint32_t DefaultTextColor() override { return 0; }
    std::uint32_t DefaultShadowColor() override { return 0; }
    std::int16_t LineHeight(int, int) override { return 16; }
    int CountLines(const char*, std::uint32_t, int) override { return 1; }
    int WrappedWidth(const char*, std::uint32_t length, int) override { return static_cast<int>(length) * 8; }
    void MeasureText(const char*, std::uint32_t, cRZRect& out) override { out = {0, 0, 0, 16}; }
    int TextWidth(const char*, std::uint32_t length) override { return static_cast<int>(length) * 8; }
    void DrawText(IWinBuffer*, int, int, const char*, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, int) override {}
    void DrawTextBox(IWinBuffer*, const cRZRect&, const char*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, int) override {}
};

// IWinGraphics: the native colour is 0xff000000 | rgb.
struct FakeGraphics final : IWinGraphics {
    std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override {
        return 0xff000000u | (static_cast<std::uint32_t>(r) << 16) | (static_cast<std::uint32_t>(g) << 8) | b;
    }
    void NativeToRGB(std::uint32_t native, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override {
        r = static_cast<std::uint8_t>((native >> 16) & 0xff);
        g = static_cast<std::uint8_t>((native >> 8) & 0xff);
        b = static_cast<std::uint8_t>(native & 0xff);
    }
    IWinBuffer* PrimaryBuffer() override { return nullptr; }
    bool DrawSystemActive() override { return false; }
};

struct Styles final : IWinStyleManager {
    FakeFont font;
    cIGZUnknown* GetFontStyle(std::uint32_t) override { return &font; }
};

// cIGZFontSys: records the acquire calls and hands out fresh fonts from a pool.
struct FakeFontSys final : cIGZFontSys {
    struct Acquire { std::string name; int type; std::uint32_t size; int style; bool reuse; };
    std::vector<Acquire> acquires;
    std::vector<std::unique_ptr<FakeFont>> pool;
    int refs = 1;
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool SetDefaultCodePage(int) override { return true; }
    void SetDefaultFontRenderer(cIGZFontRenderer*) override {}
    void SetDefaultFontAntialiasing(int) override {}
    void SetDefaultUnantialiasedSizeRange(int, int, const cIGZString*) override {}
    cIGZFont* FontAcquire(const cIGZString& names, int type, std::uint32_t size, int style, bool reuse) override {
        acquires.push_back({names.Data(), type, size, style, reuse});
        pool.push_back(std::make_unique<FakeFont>());
        FakeFont* font = pool.back().get();
        font->refs = 1;
        return font;
    }
    cIGZFont* FontAcquire(const cIGZString& path, std::uint32_t size, int style, bool reuse) override {
        acquires.push_back({path.Data(), -1, size, style, reuse});
        return nullptr;
    }
    int FontRelease(cIGZFont*) override { return 0; }
    bool AddFont(const cIGZString&) override { return true; }
    int AddAllFontsFromDirectory(const cIGZString&) override { return 0; }
    bool AddFont(cIGZFont*) override { return true; }
    bool RemoveFontByFile(const cIGZString&) override { return false; }
    bool RemoveFontByName(const cIGZString&) override { return false; }
    std::uint32_t EnumerateFontInfo(FontInfo*, std::uint32_t) override { return 0; }
    std::uint32_t EnumerateManagedFonts(cIGZFont**, std::uint32_t) override { return 0; }
    bool AddSubstitution(const cIGZString&, const cIGZString&) override { return true; }
    std::uint32_t EnumerateFontSubstitutions(const cIGZString&, cIGZString**, std::uint32_t) override { return 0; }
    bool IsFontAvailable(const cIGZString&, std::uint32_t, int) override { return true; }
    bool IsSubstitutionAvailable(const cIGZString&, cIGZString&) override { return false; }
    cIGZFont* FontAcquire(int, std::uint32_t, int, std::uint32_t, bool) override { return nullptr; }
};

// cIGZWinCtrlMgr: only SystemFont does anything.
struct FakeCtrlMgr final : cIGZWinCtrlMgr {
    FakeWinFont font;
    std::vector<int> systemFontCalls;
    int refs = 1;
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    IWinFont* SystemFont(int id) override {
        systemFontCalls.push_back(id);
        return id == static_cast<int>(kWinDefaultFontStyle) ? &font : nullptr;
    }
    void SetSystemFont(int, IWinFont*) override {}
    cIGZBuffer* SystemBMP(int) override { return nullptr; }
    void SetSystemBMP(int, cIGZBuffer*) override {}
    cIGZWinBtn* CreateButton(std::uint32_t, std::uint32_t, const cIGZString&, cIGZBuffer*, const cRZRect*, std::uint32_t) override { return nullptr; }
    cIGZWinBtn* CreateButton(std::uint32_t, const cIGZString&, cIGZBuffer*, const cRZRect*, std::uint32_t) override { return nullptr; }
    cIGZWinBtn* CreateToggleButton(std::uint32_t, const cIGZString&, cIGZBuffer*, const cRZRect*, std::uint32_t) override { return nullptr; }
    cIGZWinBtn* CreateRadioCheck(std::uint32_t, const cIGZString&, cIGZBuffer*, const cRZRect*, std::uint32_t) override { return nullptr; }
    cIGZWinText* CreateLabel(std::uint32_t, const cIGZString&) override { return nullptr; }
    cIGZWinOptGrp* CreateOptGrp(std::uint32_t, const cIGZString&, const cRZRect*) override { return nullptr; }
    cIGZWinBMP* CreateWinBMP(std::uint32_t, cIGZBuffer*, bool) override { return nullptr; }
    cIGZWinSlider* CreateSlider(std::uint32_t, int, int, int, int) override { return nullptr; }
    cIGZWinScrollbar* CreateScrollbar(std::uint32_t, int, int, int, int) override { return nullptr; }
    cIGZWinSpinner* CreateSpinner(std::uint32_t, int, int, int, int, int) override { return nullptr; }
    cIGZWinLineInput* CreateLineInput(std::uint32_t, bool) override { return nullptr; }
    cIGZWinBtn* CreateFixedButton(std::uint32_t, const cIGZString&, cIGZBuffer*, const cRZRect*, std::uint32_t) override { return nullptr; }
    cIGZWinBtn* CreateFixedToggleButton(std::uint32_t, const cIGZString&, cIGZBuffer*, const cRZRect*, std::uint32_t) override { return nullptr; }
    cIGZWinBtn* CreateCheckBox(std::uint32_t, const cIGZString&, cIGZBuffer*, const cRZRect*, std::uint32_t) override { return nullptr; }
    cIGZWinBtn* CreateRadioButton(std::uint32_t, const cIGZString&, cIGZBuffer*, const cRZRect*, std::uint32_t) override { return nullptr; }
    bool RegisterCtrlCreator(cIGZCtrlCreator*) override { return true; }
    bool RemoveCtrlCreator(cIGZCtrlCreator*) override { return true; }
    bool CreateCtrl(std::uint32_t, std::uint32_t, void**) override { return false; }
    void SetSystemSndVol(int) override {}
    int GetSystemSndVol() override { return 0; }
    std::uint32_t SystemSndID(int) override { return 0; }
    void SetSystemSndID(int, std::uint32_t) override {}
    std::uint32_t DefaultColor(int) override { return 0; }
    void SetDefaultColor(int, std::uint32_t) override {}
    void SetDefaultColor(int, std::uint8_t, std::uint8_t, std::uint8_t) override {}
    void DefaultColor(int, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override { r = g = b = 0; }
    cIGZString* DefaultText(int) override { return nullptr; }
    void SetDefaultText(int, const cIGZString*) override {}
    cIGZString* GetFontMapping(int) override { return nullptr; }
    int GetFontMappingID(const cIGZString*) override { return 0; }
    void SetFontMapping(int, const cIGZString*) override {}
    std::uint32_t GetDoubleClickTimeMilliseconds() const override { return 500; }
    void SetDoubleClickTimeMilliseconds(std::uint32_t) override {}
    bool CreateDefaultCtrl(std::uint32_t, std::uint32_t, void**) override { return false; }
};

// cIGZWinIMEServer: records the state calls OnTick / Init / Shutdown make.
struct FakeIMEServer final : cIGZWinIMEServer {
    bool available = true;
    bool enabled = false;
    bool compositionEnabled = true;
    int refs = 1;
    std::vector<int> setEnabledCalls;   // 1 / 0
    int getCompositionEnabledCalls = 0;
    int setCompositionEnabledCalls = 0;
    bool lastCompositionEnabled = true;
    cGZIMECompositionStringData composition;
    cGZIMECandidateListData candidates;
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool IsIMEAvailable() const override { return available; }
    bool GetEnabled() const override { return enabled; }
    void SetEnabled(bool value) override { enabled = value; setEnabledCalls.push_back(value ? 1 : 0); }
    bool GetCompositionEnabled() const override { return compositionEnabled; }
    bool SetCompositionEnabled(bool value) override { compositionEnabled = value; ++setCompositionEnabledCalls; return true; }
    bool GetCompositionActive() const override { return false; }
    bool SetCompositionActive(bool) const override { return true; }
    cGZIMECompositionStringData* GetCompositionStringData() override { return &composition; }
    bool SetCompositionString(const std::uint16_t*, std::uint32_t) override { return true; }
    bool GetCandidateListActive() const override { return false; }
    bool SetCandidateListActive(bool) const override { return true; }
    cGZIMECandidateListData* GetCandidateListData() override { return &candidates; }
    bool SetCandidateListSelection(std::uint32_t) override { return true; }
};

// cIGZMessageServer: records the notifications and the sent messages. It takes no target references (unlike the
// real server) so the window lifetimes stay under the test's control. answerProbe makes MessageSend write through
// the message, which is how a listener of the original can answer OnTick's position probe.
struct FakeMessageServer final : cIGZMessageServer {
    struct Note { cIGZMessageTarget* target; std::uint32_t type; };
    std::vector<Note> added, removed;
    std::vector<cGZMessage> sent;
    bool answerProbe = false;
    int refs = 1;
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override {
        if (--refs == 0) { delete this; return 0; }
        return static_cast<std::uint32_t>(refs);
    }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    bool MessageSend(const cGZMessage& message) override {
        sent.push_back(message);
        if (answerProbe) {
            answerProbe = false;   // one shot
            auto& mutableMessage = const_cast<cGZMessage&>(message);
            if (mutableMessage.type == kGZMsgIMEProxyPositionProbe) {
                mutableMessage.param1 = 1;
                mutableMessage.param2 = 100;
                mutableMessage.param3 = 200;
            }
        }
        return true;
    }
    bool MessagePost(const cGZMessage&, bool) override { return true; }
    bool AddNotification(cIGZMessageTarget* target, std::uint32_t type) override { added.push_back({target, type}); return true; }
    bool RemoveNotification(cIGZMessageTarget* target, std::uint32_t type) override { removed.push_back({target, type}); return true; }
    bool GeneralMessagePostToTarget(const cGZMessage&, cIGZMessageTarget*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
};

// cIGZFrameWork: only the tick list matters.
struct FakeFramework final : cIGZFrameWork {
    std::vector<cIGZSystemService*> ticks;
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool AddToTick(cIGZSystemService* service) override { ticks.push_back(service); return true; }
    bool RemoveFromTick(cIGZSystemService* service) override {
        for (auto it = ticks.begin(); it != ticks.end(); ++it)
            if (*it == service) { ticks.erase(it); return true; }
        return false;
    }
    void OnIdle() override {}
    bool AddSystemService(cIGZSystemService*) override { return true; }
    bool RemoveSystemService(cIGZSystemService*) override { return true; }
    bool GetSystemService(std::uint32_t, std::uint32_t, void**) override { return false; }
    bool EnumSystemServices(cIGZUnknownEnumerator&, cIGZUnknown*, std::uint32_t) override { return false; }
    bool AddHook(cIGZFrameWorkHooks*) override { return true; }
    bool RemoveHook(cIGZFrameWorkHooks*) override { return true; }
    bool AddToOnIdle(cIGZSystemService*) override { return true; }
    bool RemoveFromOnIdle(cIGZSystemService*) override { return true; }
    int GetOnIdleInterval() const override { return 0; }
    void SetOnIdleInterval(int) override {}
    void OnTick(std::uint32_t) override {}
    bool IsTickEnabled() const override { return true; }
    void ToggleTick(bool) override {}
    void Quit(int) override {}
    void AbortiveQuit(int) override {}
    cIGZCmdLine* CommandLine() override { return nullptr; }
    bool IsInstall() const override { return false; }
    cIGZCOM* GetCOMObject() override { return nullptr; }
    int GetState() const override { return 0; }
    cIGZDebugStream* GetDebugStream() override { return nullptr; }
    cIGZDebugStream* DefaultDebugStream() override { return nullptr; }
    cIGZDebugStream* DebugStream() override { return nullptr; }
    void SetDebugStream(cIGZDebugStream*) override {}
    void SetDebugLevel(int) override {}
    int GetDebugLevel() const override { return 0; }
    cIGZOStream* StdOut() override { return nullptr; }
    cIGZOStream* StdErr() override { return nullptr; }
    cIGZIStream* StdIn() override { return nullptr; }
    bool GetStream(int, std::uint32_t, void**) override { return false; }
    bool SetStream(int, cIGZUnknown*) override { return false; }
    void SetApplication(cIGZApp*) override {}
    cIGZApp* Application() const override { return nullptr; }
    void ReportException(const char*) override {}
    cIGZUnknown* ExceptionNotificationObj() const override { return nullptr; }
};

// ------------------------------------------------------------------------------------------------------------------
// The candidate list window (the not-yet-recovered cGZWinIMECandidateList): a real window that also answers through
// the interface, like the binary class (cIGZWinIMECandidateList at +0, the window at +8).

int g_candidateWindowsAlive = 0;

struct CandidateWin final : cGZWin, cIGZWinIMECandidateList {
    explicit CandidateWin(cIGZWinMgr* mgr) : cGZWin(mgr) { ++g_candidateWindowsAlive; }
    ~CandidateWin() override { if (font) font->Release(); --g_candidateWindowsAlive; }
    std::vector<cIGZFont*> fontsSet;
    cIGZFont* font = nullptr;
    std::uint32_t flags = 0;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cIGZWinIMECandidateList) { *out = static_cast<cIGZWinIMECandidateList*>(this); AddRef(); return true; }
        return cGZWin::QueryInterface(iid, out);
    }
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    cIGZWin* AsIGZWin() override { return static_cast<cGZWin*>(this); }
    bool Init() override { return cGZWin::Init(); }
    bool Shutdown() override { return cGZWin::Shutdown(); }
    cIGZFont* GetFont() const override { return font; }
    void SetFont(cIGZFont* value) override {
        if (value) value->AddRef();
        if (font) font->Release();
        font = value;
        fontsSet.push_back(value);
    }
    bool GetCandidateListFontColor(int, std::uint32_t& native) const override { native = 0; return false; }
    bool SetCandidateListFontColor(int, std::uint32_t) override { return false; }
    void SetCandidateListFlag(int flag) override { flags |= static_cast<std::uint32_t>(flag); }
    void SetCandidateListFlags(int value) override { flags = static_cast<std::uint32_t>(value); }
    void ClearCandidateListFlag(int flag) override { flags &= ~static_cast<std::uint32_t>(flag); }
    bool IsCandidateListFlagSet(int flag) override { return (flags & static_cast<std::uint32_t>(flag)) != 0; }
    std::uint32_t GetCandidateListFlags() override { return flags; }
    bool DoMessage(cGZMessage&) override { return false; }
    void SetCandidateListData(cGZIMECandidateListData*) override {}
    void SetCandidateListSelectionPosition(cGZIMECandidateListData*) override {}
    void SetupWindow(cGZIMECandidateListData*) override {}
};

// GZCOM: either refuses everything (so the fallback runs) or creates the window class.
struct FakeCOM final : cIGZCOM {
    enum Mode { kFail, kComposition, kCandidateList };
    Mode mode = kFail;
    FakeMgr* mgr = nullptr;
    std::vector<std::uint32_t> createdClasses;
    int refs = 1;
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    bool CreateInstance(std::uint32_t clsid, std::uint32_t iid, void** out) override {
        createdClasses.push_back(clsid);
        if (mode == kComposition && clsid == kGZCLSID_cGZWinIMEComposition && iid == kGZIID_cIGZWinIMEComposition) {
            auto* window = new cGZWinIMEComposition(mgr);
            if (window->QueryInterface(iid, out)) return true;   // the factory hands out one reference
            window->AddRef();
            window->Release();
            return false;
        }
        if (mode == kCandidateList && clsid == kGZCLSID_cGZWinIMECandidateList && iid == kGZIID_cIGZWinIMECandidateList) {
            auto* window = new CandidateWin(mgr);
            window->AddRef();
            *out = static_cast<cIGZWinIMECandidateList*>(window);
            return true;
        }
        return false;
    }
    cIGZFrameWork* GetFrameWork() override { return nullptr; }
};

// The proxy's fallback for the candidate list (cGZWinIMECandidateList is not recovered).
struct FakeHost final : IIMEProxyHost {
    FakeMgr* mgr = nullptr;
    int calls = 0;
    CandidateWin* last = nullptr;
    cIGZWinIMECandidateList* CreateCandidateListWindow(cIGZWinMgr* windowManager) override {
        ++calls;
        last = new CandidateWin(windowManager ? windowManager : static_cast<cIGZWinMgr*>(mgr));
        last->AddRef();
        return static_cast<cIGZWinIMECandidateList*>(last);
    }
};

// ------------------------------------------------------------------------------------------------------------------
// Rig

struct Rig {
    FakeMgr mgr;
    FakeFramework framework;
    FakeFontSys fontSys;
    FakeCtrlMgr ctrlMgr;
    FakeIMEServer ime;
    FakeCOM com;
    FakeGraphics graphics;
    Styles styles;
    FakeHost host;
    FakeMessageServer* ms = nullptr;
    cGZWin* mainWin = nullptr;

    void start() {
        ms = new FakeMessageServer();
        g_rig = this;
        SetRZServiceResolver(&Resolve);
        SetRZFramework(&framework);
        RZGetCOMDllDirector()->InitializeCOM(&com, cRZString(""));
        mainWin = new cGZWin(&mgr);
        mainWin->AddRef();
        mgr.mainWin = mainWin;
        SetWinStyleManager(&styles);
        SetWinGraphics(&graphics);
        SetIMEProxyHost(&host);
        com.mgr = &mgr;
        host.mgr = &mgr;
    }
    void stop() {
        SetIMEProxyHost(nullptr);
        SetWinGraphics(nullptr);
        SetWinStyleManager(nullptr);
        SetRZFramework(nullptr);
        SetRZServiceResolver(nullptr);
        ms->Release();
        mainWin->Release();
    }
    static bool Resolve(std::uint32_t serviceId, std::uint32_t iid, void** out);
    static Rig* g_rig;
};

Rig* Rig::g_rig = nullptr;

bool Rig::Resolve(std::uint32_t serviceId, std::uint32_t iid, void** out) {
    Rig* rig = g_rig;
    if (!rig) return false;
    if (serviceId == kGZSID_cGZMessageServer && iid == kGZIID_cIGZMessageServer) {
        rig->ms->AddRef();
        *out = static_cast<cIGZMessageServer*>(rig->ms);
        return true;
    }
    if (serviceId == kGZSID_cGZWinIMEServer && iid == kGZIID_cIGZWinIMEServer) {
        rig->ime.AddRef();
        *out = static_cast<cIGZWinIMEServer*>(&rig->ime);
        return true;
    }
    if (serviceId == kGZSID_cGZWinMgrBase && iid == kGZIID_cIGZWinMgr) {
        rig->mgr.AddRef();
        *out = static_cast<cIGZWinMgr*>(&rig->mgr);
        return true;
    }
    if (serviceId == kGZFontSysDefaultServiceID && iid == kGZIID_cIGZFontSys) {
        rig->fontSys.AddRef();
        *out = static_cast<cIGZFontSys*>(&rig->fontSys);
        return true;
    }
    if (serviceId == kGZSID_cGZWinCtrlMgr && iid == kGZIID_cIGZWinCtrlMgr) {
        rig->ctrlMgr.AddRef();
        *out = static_cast<cIGZWinCtrlMgr*>(&rig->ctrlMgr);
        return true;
    }
    return false;
}

}  // namespace

int main() {
    // ---- construction, interface map and the reference-count quirk ------------------------------------------------
    {
        auto* proxy = new cGZWinIMEProxy();
        CHECK(proxy->GetServiceID() == kGZSID_cGZWinIMEProxy);
        CHECK(proxy->GetServicePriority() == kGZWinIMEProxyPriority);
        CHECK(proxy->GetServiceTickPriority() == 0);   // quirk: the non-const cIGZSystemService overload
        {
            const cRZSystemService& service = *proxy;
            CHECK(service.GetServiceTickPriority() == kGZWinIMEProxyPriority);
        }
        void* raw = nullptr;
        int refs = 0;
        CHECK(proxy->QueryInterface(kGZIID_cIGZWinIMEProxy, &raw)); ++refs;
        CHECK(raw == static_cast<cIGZWinIMEProxy*>(proxy));
        CHECK(proxy->QueryInterface(kGZIID_cIGZUnknown, &raw)); ++refs;   // iid 1
        CHECK(raw == static_cast<cIGZWinIMEProxy*>(proxy));
        CHECK(proxy->QueryInterface(kGZIID_cGZWinAlt, &raw)); ++refs;     // 0xe98b2f57, the +8 message target
        CHECK(raw == static_cast<cIGZMessageTarget*>(proxy));
        CHECK(proxy->QueryInterface(kGZIID_cIGZSystemService, &raw)); ++refs;   // 0x287fb697, through the base
        CHECK(raw == static_cast<cIGZSystemService*>(proxy));
        CHECK(!proxy->QueryInterface(0x12345678u, &raw));
        CHECK(!proxy->QueryInterface(kGZIID_cIGZWinTextEdit, &raw));
        CHECK(!proxy->IsInitialized());
        CHECK(proxy->GetCompositionWinEnabled() && proxy->GetCandidateListWinEnabled());
        CHECK(proxy->GetCompositionWin() == nullptr && proxy->GetCandidateListWin() == nullptr);
        CHECK(proxy->GetCompositionFont() == nullptr && proxy->GetCandidateListFont() == nullptr);
        CHECK(proxy->CompositionPosX() == kGZIMEProxyNoPosition && proxy->CompositionPosY() == kGZIMEProxyNoPosition);
        CHECK(proxy->PreviousFocus() == nullptr && proxy->MessageServer() == nullptr);
        CHECK(proxy->Manager() == nullptr && proxy->ImeServer() == nullptr);
        CHECK(proxy->TickCounter() == 0);
        // Count 4 from the four query references: two releases only decrement, the release at count 1 destroys.
        while (refs-- > 0) proxy->Release();
    }
    {
        auto* counter = new cGZWinIMEProxy();
        CHECK(counter->AddRef() == 1);
        CHECK(counter->AddRef() == 2);
        CHECK(counter->Release() == 1);
        counter->Release();   // count 1 < 2: Shutdown + deleting destructor
        auto* fresh = new cGZWinIMEProxy();
        CHECK(fresh->Release() == 0);   // a freshly built (count 0) proxy dies on the first release
    }

    Rig rig;
    rig.start();

    auto* proxy = new cGZWinIMEProxy();
    proxy->AddRef();

    // ---- Init: services, notifications, tick registration ------------------------------------------------------
    CHECK(proxy->Init());
    CHECK(proxy->IsInitialized());
    CHECK(proxy->MessageServer() == static_cast<cIGZMessageServer*>(rig.ms));
    CHECK(proxy->ImeServer() == static_cast<cIGZWinIMEServer*>(&rig.ime));
    CHECK(proxy->Manager() == static_cast<cIGZWinMgr*>(&rig.mgr));
    CHECK(rig.ime.setEnabledCalls.size() == 1 && rig.ime.setEnabledCalls[0] == 1);
    CHECK(rig.ms->added.size() == 4);
    {
        const std::uint32_t expected[] = {kGZMessage_IMEStartComposition, kGZMessage_IMEEndComposition,
                                          kGZMessage_IMEStartCandidateList, kGZMessage_IMEEndCandidateList};
        for (int i = 0; i < 4; ++i) {
            CHECK(rig.ms->added[static_cast<std::size_t>(i)].type == expected[i]);
            CHECK(rig.ms->added[static_cast<std::size_t>(i)].target == static_cast<cIGZMessageTarget*>(proxy));
        }
    }
    CHECK(rig.framework.ticks.size() == 1 && rig.framework.ticks[0] == static_cast<cIGZSystemService*>(proxy));
    CHECK(proxy->Init());   // idempotent
    CHECK(rig.ms->added.size() == 4 && rig.framework.ticks.size() == 1 && rig.ime.setEnabledCalls.size() == 1);

    // An IME server that reports no IME (the recovered cGZWinIMEServer always does) leaves the proxy out of the tick.
    rig.ime.available = false;
    rig.framework.ticks.clear();
    {
        auto* other = new cGZWinIMEProxy();
        other->AddRef();
        CHECK(other->Init());
        CHECK(rig.framework.ticks.empty());
        CHECK(rig.ms->added.size() == 8);   // the second proxy registers too
        other->Release();
        CHECK(rig.ms->removed.size() == 4);
    }
    rig.ime.available = true;
    rig.framework.ticks.clear();
    rig.framework.ticks.push_back(static_cast<cIGZSystemService*>(proxy));   // restore the state we assert below

    // ---- fonts and the composition window, GZCOM refusing the class --------------------------------------------
    rig.com.mode = FakeCOM::kFail;
    rig.fontSys.acquires.clear();
    rig.ctrlMgr.systemFontCalls.clear();
    CHECK(proxy->GetCompositionFont() == nullptr && proxy->GetCandidateListFont() == nullptr);
    proxy->StartComposition();
    CHECK(proxy->GetCompositionWin() != nullptr);
    CHECK(rig.ctrlMgr.systemFontCalls.size() == 1 &&
          rig.ctrlMgr.systemFontCalls[0] == static_cast<int>(kWinDefaultFontStyle));
    CHECK(rig.fontSys.acquires.size() == 2);   // one font per window, both from the same name
    for (const FakeFontSys::Acquire& acquire : rig.fontSys.acquires) {
        CHECK(acquire.name == "Arial");
        CHECK(acquire.type == 0 && acquire.size == 0x12 && acquire.style == 0 && acquire.reuse);
    }
    CHECK(proxy->GetCompositionFont() != nullptr && proxy->GetCandidateListFont() != nullptr);
    CHECK(proxy->GetCompositionFont() != proxy->GetCandidateListFont());
    {
        cIGZWinIMEComposition* composition = proxy->GetCompositionWin();
        CHECK(composition->GetFont() == proxy->GetCompositionFont());
        CHECK(composition->AsIGZWin()->GetParentWin() == rig.mainWin);
        CHECK(rig.mainWin->ChildExists(composition->AsIGZWin()));
        CHECK(!composition->AsIGZWin()->GetFlag(0x800));   // SetFlag(0x800, false)
        CHECK(composition->AsIGZWin()->GetL() == 2 && composition->AsIGZWin()->GetT() == 2);   // no position override
        proxy->StartComposition();   // +0x80 set: no second window
        CHECK(proxy->GetCompositionWin() == composition);
        CHECK(rig.fontSys.acquires.size() == 2);   // and no extra font lookups
    }
    proxy->EndComposition();
    CHECK(proxy->GetCompositionWin() == nullptr);
    proxy->SetCompositionWinEnabled(false);
    CHECK(!proxy->GetCompositionWinEnabled());
    proxy->StartComposition();
    CHECK(proxy->GetCompositionWin() == nullptr);
    proxy->SetCompositionWinEnabled(true);

    // SetupDefaultFonts only looks the missing one up, and the font setters keep the references straight.
    proxy->SetupDefaultFonts();
    CHECK(rig.fontSys.acquires.size() == 2);
    {
        cIGZFont* compositionFont = proxy->GetCompositionFont();
        cIGZFont* candidateFont = proxy->GetCandidateListFont();
        CHECK(static_cast<FakeFont*>(compositionFont)->refs == 1);
        CHECK(static_cast<FakeFont*>(candidateFont)->refs == 1);
        proxy->SetCandidateListFont(nullptr);
        CHECK(proxy->GetCandidateListFont() == nullptr);
        CHECK(static_cast<FakeFont*>(candidateFont)->refs == 0);
        proxy->SetCompositionFont(compositionFont);   // same pointer: no reference churn
        CHECK(static_cast<FakeFont*>(compositionFont)->refs == 1);
        rig.fontSys.acquires.clear();
        proxy->SetupDefaultFonts();
        CHECK(rig.fontSys.acquires.size() == 1);   // only the candidate font was missing
        CHECK(proxy->GetCandidateListFont() != nullptr && proxy->GetCandidateListFont() != compositionFont);
    }

    // ---- the GZCOM path creates the composition window ---------------------------------------------------------
    rig.com.mode = FakeCOM::kComposition;
    rig.com.createdClasses.clear();
    proxy->StartComposition();
    CHECK(rig.com.createdClasses.size() == 1 && rig.com.createdClasses[0] == kGZCLSID_cGZWinIMEComposition);
    CHECK(proxy->GetCompositionWin() != nullptr);
    CHECK(proxy->GetCompositionWin()->GetFont() == proxy->GetCompositionFont());
    CHECK(proxy->GetCompositionWin()->AsIGZWin()->GetParentWin() == rig.mainWin);
    proxy->EndComposition();
    CHECK(proxy->GetCompositionWin() == nullptr);
    rig.com.mode = FakeCOM::kFail;

    // ---- the candidate list window: the seam fallback and the GZCOM path ---------------------------------------
    const int candidatesAlive = g_candidateWindowsAlive;
    rig.host.calls = 0;
    proxy->StartCandidateList();
    CHECK(rig.host.calls == 1);
    CHECK(proxy->GetCandidateListWin() == static_cast<cIGZWinIMECandidateList*>(rig.host.last));
    CHECK(rig.host.last->font == proxy->GetCandidateListFont());
    CHECK(rig.host.last->AsIGZWin()->GetParentWin() == rig.mainWin);
    CHECK(rig.mainWin->ChildExists(rig.host.last->AsIGZWin()));
    CHECK(!rig.host.last->AsIGZWin()->GetFlag(0x800));
    CHECK(g_candidateWindowsAlive == candidatesAlive + 1);
    proxy->StartCandidateList();   // +0x98 set: no second window
    CHECK(rig.host.calls == 1);
    proxy->EndCandidateList();
    CHECK(proxy->GetCandidateListWin() == nullptr);
    CHECK(g_candidateWindowsAlive == candidatesAlive);   // DestroyWindow + Release destroyed it

    // Without the host and with GZCOM refusing the class the window cannot be built.
    SetIMEProxyHost(nullptr);
    rig.host.calls = 0;
    proxy->StartCandidateList();
    CHECK(proxy->GetCandidateListWin() == nullptr);
    CHECK(rig.host.calls == 0);
    SetIMEProxyHost(&rig.host);

    rig.com.mode = FakeCOM::kCandidateList;
    rig.com.createdClasses.clear();
    rig.host.calls = 0;
    proxy->StartCandidateList();
    CHECK(rig.com.createdClasses.size() == 1 && rig.com.createdClasses[0] == kGZCLSID_cGZWinIMECandidateList);
    CHECK(rig.host.calls == 0);   // GZCOM answered, the host was not asked
    CHECK(proxy->GetCandidateListWin() != nullptr);
    CHECK(proxy->GetCandidateListWin()->AsIGZWin()->GetParentWin() == rig.mainWin);
    proxy->EndCandidateList();
    CHECK(proxy->GetCandidateListWin() == nullptr);
    rig.com.mode = FakeCOM::kFail;
    proxy->SetCandidateListWinEnabled(false);
    proxy->StartCandidateList();
    CHECK(proxy->GetCandidateListWin() == nullptr && rig.host.calls == 0);
    proxy->SetCandidateListWinEnabled(true);

    // ---- DoMessage dispatch -----------------------------------------------------------------------------------
    {
        cGZMessage message{};
        message.type = kGZMessage_IMEStartComposition;
        CHECK(proxy->DoMessage(message));
        CHECK(proxy->GetCompositionWin() != nullptr);
        message.type = kGZMessage_IMEStartCandidateList;
        CHECK(proxy->DoMessage(message));
        CHECK(proxy->GetCandidateListWin() != nullptr);
        message.type = kGZMessage_IMEEndCandidateList;
        CHECK(proxy->DoMessage(message));
        CHECK(proxy->GetCandidateListWin() == nullptr);
        message.type = kGZMessage_IMEEndComposition;
        CHECK(proxy->DoMessage(message));
        CHECK(proxy->GetCompositionWin() == nullptr);
        message.type = kGZMsgCharacter;   // not forwarded to a base class, and no window is touched
        CHECK(proxy->DoMessage(message));
        CHECK(proxy->GetCompositionWin() == nullptr && proxy->GetCandidateListWin() == nullptr);
        message.type = 0x12345678u;
        CHECK(proxy->DoMessage(message));
    }

    // ---- OnTick: the focus probe, the four/eight tick cadence and the position override -----------------------
    auto* ticking = new cGZWinIMEProxy();
    ticking->AddRef();
    CHECK(ticking->Init());
    rig.mgr.focus = nullptr;
    rig.ms->sent.clear();
    rig.ime.compositionEnabled = true;
    rig.ime.setCompositionEnabledCalls = 0;
    for (int i = 0; i < 8; ++i) CHECK(ticking->OnTick(16));   // only the eighth tick probes
    CHECK(ticking->TickCounter() == 8);
    CHECK(rig.ms->sent.size() == 1);
    if (!rig.ms->sent.empty()) {
        const cGZMessage& probe = rig.ms->sent.back();
        CHECK(probe.type == kGZMsgIMEProxyPositionProbe);
        CHECK(probe.param1 == 0 && probe.param2 == 2 && probe.param3 == 2);
    }
    CHECK(rig.ime.setCompositionEnabledCalls == 1 && !rig.ime.compositionEnabled);
    CHECK(ticking->CompositionPosX() == kGZIMEProxyNoPosition);
    CHECK(ticking->PreviousFocus() == nullptr);

    // The IME server's composition-enabled flag is already off: no second set.
    rig.ms->sent.clear();
    for (int i = 0; i < 8; ++i) ticking->OnTick(16);
    CHECK(rig.ms->sent.size() == 1 && rig.ime.setCompositionEnabledCalls == 1);

    // A changed focus is examined at the next multiple of four (mainWin accepts none of the three IME ids).
    rig.mgr.focus = rig.mainWin;
    rig.ms->sent.clear();
    for (int i = 0; i < 4; ++i) ticking->OnTick(16);   // ticks 17..20: the change fires at 20
    CHECK(rig.ms->sent.size() == 1);
    CHECK(ticking->PreviousFocus() == rig.mainWin);
    rig.ms->sent.clear();
    for (int i = 0; i < 4; ++i) ticking->OnTick(16);   // 21..24: the same focus only at the multiple of eight
    CHECK(rig.ms->sent.size() == 1);
    rig.ms->sent.clear();
    for (int i = 0; i < 4; ++i) ticking->OnTick(16);   // 25..28: 28 is not a multiple of eight
    CHECK(rig.ms->sent.empty());

    // A focus that answers one of the IME interfaces is left alone. The probe tries the text-edit iid first, and a
    // composition window answers it too (it is a cGZWinTextEdit), so the 0x2ac44a90 check below is dead for the real
    // class; it is kept because the binary has it, and the candidate list window does reach its own check.
    auto* textEdit = new cGZWinTextEdit(&rig.mgr, 0);
    textEdit->AddRef();
    rig.mgr.focus = textEdit;
    rig.ms->sent.clear();
    for (int i = 0; i < 4; ++i) ticking->OnTick(16);   // the change fires at 32
    CHECK(rig.ms->sent.empty());
    {
        auto* composition = new cGZWinIMEComposition(&rig.mgr);
        composition->AddRef();
        rig.mgr.focus = composition;
        rig.ms->sent.clear();
        for (int i = 0; i < 4; ++i) ticking->OnTick(16);   // 36
        CHECK(rig.ms->sent.empty());
        auto* candidates = new CandidateWin(&rig.mgr);
        candidates->AddRef();
        rig.mgr.focus = candidates;
        for (int i = 0; i < 4; ++i) ticking->OnTick(16);   // 40
        CHECK(rig.ms->sent.empty());
        candidates->Release();
        composition->Release();
    }

    // A listener that answers the probe supplies the position override, which the next window is moved to.
    rig.mgr.focus = nullptr;   // a change: handled on the next multiple of four (44)
    rig.ms->sent.clear();
    rig.ms->answerProbe = true;
    rig.ime.compositionEnabled = true;
    rig.ime.setCompositionEnabledCalls = 0;
    for (int i = 0; i < 4; ++i) ticking->OnTick(16);
    CHECK(rig.ms->sent.size() == 1);
    CHECK(ticking->CompositionPosX() == 100 && ticking->CompositionPosY() == 200);
    CHECK(rig.ime.setCompositionEnabledCalls == 0);   // the answer wins over the disable
    ticking->StartComposition();
    CHECK(ticking->GetCompositionWin() != nullptr);
    if (ticking->GetCompositionWin()) {
        CHECK(ticking->GetCompositionWin()->AsIGZWin()->GetL() == 100);
        CHECK(ticking->GetCompositionWin()->AsIGZWin()->GetT() == 200);
    }
    ticking->EndComposition();

    // The system service view shares the same OnTick override.
    {
        const std::uint32_t before = ticking->TickCounter();
        CHECK(static_cast<cIGZSystemService*>(ticking)->OnTick(16));
        CHECK(ticking->TickCounter() == before + 1);
    }
    ticking->Release();
    textEdit->Release();

    // A proxy without the window manager / IME server only bumps its counter.
    {
        rig.ms->sent.clear();
        auto* bare = new cGZWinIMEProxy();
        bare->AddRef();
        for (int i = 0; i < 8; ++i) CHECK(bare->OnTick(16));
        CHECK(bare->TickCounter() == 8);
        CHECK(rig.ms->sent.empty());
        bare->Release();
    }

    // ---- Shutdown releases everything but the two floating windows --------------------------------------------
    rig.mgr.focus = nullptr;
    proxy->StartComposition();
    proxy->StartCandidateList();
    CHECK(proxy->GetCompositionWin() != nullptr && proxy->GetCandidateListWin() != nullptr);
    const int candidatesBeforeShutdown = g_candidateWindowsAlive;
    rig.ms->removed.clear();
    rig.framework.ticks.clear();
    rig.framework.ticks.push_back(static_cast<cIGZSystemService*>(proxy));
    rig.ime.setEnabledCalls.clear();
    CHECK(proxy->Shutdown());
    CHECK(!proxy->IsInitialized());
    CHECK(proxy->MessageServer() == nullptr && proxy->ImeServer() == nullptr && proxy->Manager() == nullptr);
    CHECK(proxy->GetCompositionFont() == nullptr && proxy->GetCandidateListFont() == nullptr);
    CHECK(proxy->GetCompositionWin() != nullptr);      // quirk: the windows are not destroyed
    CHECK(proxy->GetCandidateListWin() != nullptr);
    CHECK(g_candidateWindowsAlive == candidatesBeforeShutdown);
    CHECK(rig.framework.ticks.empty());
    CHECK(rig.ms->removed.size() == 4);
    CHECK(rig.ime.setEnabledCalls.size() == 1 && rig.ime.setEnabledCalls[0] == 0);
    CHECK(proxy->PreviousFocus() == nullptr);
    CHECK(proxy->Shutdown());   // not initialised: nothing happens
    CHECK(rig.ms->removed.size() == 4);
    // The destructor drops the proxy's reference to both windows; they stay alive because the main window still
    // holds them as children (the same reference the proxy's Shutdown leaves behind).
    proxy->Release();
    CHECK(g_candidateWindowsAlive == candidatesBeforeShutdown);
    rig.stop();   // destroying the main window destroys the children, the candidate list window included
    CHECK(g_candidateWindowsAlive == candidatesBeforeShutdown - 1);
    std::printf("gz_winimeproxy: %s\n", failures == 0 ? "all checks passed" : "FAILED");
    return failures == 0 ? 0 : 1;
}

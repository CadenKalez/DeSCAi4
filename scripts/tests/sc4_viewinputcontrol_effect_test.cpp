#include <cstdio>
#include <map>
#include <string>

#include "sc4/ViewInputControlEffect.h"
#include "vic_fakes.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;
using namespace vic_fakes;

namespace {
std::string g_log;

struct FakeEffect final : IToolVisualEffect {
    std::string tag;
    int refs = 1;
    bool started = false;
    float lastFloat = -1;
    cS3DVector3 lastVec;
    explicit FakeEffect(std::string t) : tag(std::move(t)) {}
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void Start(int t) override { started = true; g_log += tag + ".start" + std::to_string(t) + ";"; }
    void Stop(int t) override { started = false; g_log += tag + ".stop" + std::to_string(t) + ";"; }
    bool IsStarted() override { return started; }
    void SetSourceTransform(const cS3DTransform& t) override {
        g_log += tag + ".xf(" + std::to_string(int(t.translation[0])) + "," + std::to_string(int(t.translation[1])) + "," +
                 std::to_string(int(t.translation[2])) + ");";
    }
    bool SetVectorParams(std::uint32_t type, const cS3DVector3* v, int n) override {
        lastVec = *v;
        g_log += tag + ".vec" + std::to_string(type) + "x" + std::to_string(n) + ";";
        return true;
    }
    bool SetFloatParams(std::uint32_t type, const float* v, int n) override {
        lastFloat = *v;
        g_log += tag + ".flt" + std::to_string(type) + "=" + std::to_string(int(*v)) + "x" + std::to_string(n) + ";";
        return true;
    }
};

struct FakeManager final : IToolEffectsManager {
    int refs = 0;
    std::map<std::string, FakeEffect*> byName;        // owned by the test; handed out with one extra ref each
    std::map<std::pair<std::uint32_t, int>, FakeEffect*> byGroup;
    std::string lastQueried;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool HasEffect(const char* name) override { lastQueried = name; return byName.count(name) != 0; }
    bool CreateEffect(const char* name, IToolVisualEffect** out) override {
        auto it = byName.find(name);
        if (it == byName.end()) return false;
        it->second->AddRef();
        *out = it->second;
        return true;
    }
    bool CreateEffect(std::uint32_t g, int type, IToolVisualEffect** out) override {
        auto it = byGroup.find({g, type});
        if (it == byGroup.end()) return false;
        it->second->AddRef();
        *out = it->second;
        return true;
    }
};

struct FakeView final : IEffectView {
    int refs = 1;
    bool pickOk = true;
    cS3DVector3 result{10, 20, 30};
    int px = 0, py = 0;
    bool lastFlag = false;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void EndInput(int) override {}
    cSC4ViewInputControl* TopControl() override { return nullptr; }
    bool PickTerrain(int x, int y, cS3DVector3* out, bool flag) override {
        px = x; py = y; lastFlag = flag;
        if (!pickOk) return false;
        *out = result;
        return true;
    }
};

struct Host final : IViewInputHost, IEffectToolHost {
    FakeView view; Mgr mgr; FakeManager fx;
    IViewInputView* View3D() override { return &view; }
    cIGZWinMgr* WindowManager() override { return &mgr; }
    void PlaySound(int, std::uint32_t) override {}
    IToolEffectsManager* EffectsManager() override { return &fx; }
};

}  // namespace

int main() {
    Host host;
    SetViewInputHost(&host);
    SetEffectToolHost(&host);
    CursorWin win(&host.mgr);

    FakeEffect button("button"), single("single"), control("control"), active("active"), inactive("inactive"),
        trigger("trigger");
    host.fx.byName = {{"boom_button", &button}, {"boom_single", &single}, {"boom_control", &control},
                      {"boom_active", &active}, {"boom_inactive", &inactive}, {"boom_trigger", &trigger}};
    // a name with only the trigger effect
    auto* c = new cSC4ViewInputControlEffect();
    c->AddRef();
    CHECK(!c->ShouldStack());
    CHECK(c->OnKeyDown(1, 2) == !c->IsKeyToBePassed(1, 2));
    c->SetWindow(&win);
    c->SetCursor(std::uint32_t(1));
    CHECK(c->Init());
    CHECK(host.fx.refs == 1 && host.view.refs == 2);
    CHECK(!c->IsMouseDown());

    // --- SetEffectName
    g_log.clear();
    c->SetEffectName("boom");
    CHECK(host.fx.lastQueried == "boom_trigger");
    CHECK(c->HasAnyEffect());
    CHECK(g_log == "button.start0;");                 // the button effect is started once loaded
    CHECK(button.refs == 2 && control.refs == 2 && trigger.refs == 1);
    // reloading releases the old references first
    c->SetEffectName("boom");
    CHECK(button.refs == 2 && single.refs == 2 && inactive.refs == 2);

    // --- mouse move picks and keeps the inactive (hover) effect following the cursor
    g_log.clear();
    CHECK(c->OnMouseMove(5, 6, 0));
    CHECK(host.view.px == 5 && host.view.py == 6);
    CHECK(c->SourceTransform().translation[0] == 10 && c->SourceTransform().translation[2] == 30);
    CHECK((c->SourceTransform().flags & 1) && c->SourceTransform().changeCounter == 1);
    CHECK(g_log == "active.xf(10,20,30);inactive.start0;inactive.xf(10,20,30);");
    // failed pick stops the hover effect
    g_log.clear();
    host.view.pickOk = false;
    CHECK(c->OnMouseMove(1, 1, 0));
    CHECK(g_log == "inactive.stop0;");
    host.view.pickOk = true;

    // --- press: trigger, active, inactive stop, single, control idle -> no button press yet
    g_log.clear();
    CHECK(c->OnMouseDownL(7, 8, 0));
    // hasTrigger_ is true ("boom_trigger" exists): the trigger effect is created, positioned, started, released
    CHECK(g_log ==
          "trigger.xf(10,20,30);trigger.start0;"
          "active.xf(10,20,30);active.start0;inactive.stop0;"
          "single.xf(10,20,30);single.start0;");
    CHECK(trigger.refs == 1);
    CHECK(c->IsMouseDown() && c->IsCapturing() && host.mgr.setCapture == &win);
    // the control effect was idle: it is flagged, and neither control nor button got parameters
    // (mouse moves while pressed do not drive them either)
    g_log.clear();
    CHECK(c->OnMouseMove(9, 9, 0));
    CHECK(g_log == "active.xf(10,20,30);inactive.xf(10,20,30);");   // mouse is down: hover effect not restarted

    // --- release with the control effect idle at press: terrain-controlled state on
    g_log.clear();
    CHECK(c->OnMouseUpL(0, 0, 0));
    CHECK(g_log ==
          "active.stop0;inactive.xf(10,20,30);inactive.start0;control.flt11=0x1;button.flt11=0x1;"
          "control.xf(10,20,30);control.start0;");
    CHECK(!c->IsCapturing() && !c->IsMouseDown() && host.mgr.released == &win);
    // release when not capturing is not handled
    CHECK(!c->OnMouseUpL(0, 0, 0));

    // --- second press with the control effect running: parameters go to control and button
    g_log.clear();
    host.view.result = {1, 2, 3};
    CHECK(c->OnMouseDownL(0, 0, 0));
    CHECK(g_log ==
          "trigger.xf(1,2,3);trigger.start0;"
          "active.xf(1,2,3);active.start0;inactive.stop0;"
          "single.xf(1,2,3);single.stop0;single.start0;"      // single was running: restarted
          "control.flt11=1x1;control.vec10x1;button.flt11=1x1;button.vec10x1;");
    CHECK(control.lastVec.x == 1 && control.lastVec.y == 2 && control.lastVec.z == 3);
    // dragging with the button held forwards the position to control and button
    g_log.clear();
    host.view.result = {4, 5, 6};
    CHECK(c->OnMouseMove(0, 0, 0));
    CHECK(g_log == "control.vec10x1;button.vec10x1;active.xf(4,5,6);inactive.xf(4,5,6);");
    // release with Ctrl-like modifier bit 2 while the control was running: state off
    g_log.clear();
    CHECK(c->OnMouseUpL(0, 0, 2));
    CHECK(g_log ==
          "active.stop0;inactive.xf(4,5,6);inactive.start0;control.flt11=0x1;button.flt11=0x1;control.stop0;");
    // without the bit nothing more happens
    c->OnMouseDownL(0, 0, 0);
    g_log.clear();
    CHECK(c->OnMouseUpL(0, 0, 0));
    // the control effect was stopped by the previous release, so it was idle again at this press: state on
    CHECK(g_log.find("control.stop0") == std::string::npos && g_log.find("control.start0") != std::string::npos);

    // failed pick on press is still "handled" and changes nothing
    g_log.clear();
    host.view.pickOk = false;
    CHECK(c->OnMouseDownL(0, 0, 0) && g_log.empty());
    host.view.pickOk = true;

    // exit stops the hover effect and falls through to the (false) base handler
    g_log.clear();
    CHECK(!c->OnMouseExit());
    CHECK(g_log == "inactive.stop0;");

    // the terrain-controlled state API
    g_log.clear();
    c->SetTerrainControlledEffectState(true);
    c->SetTerrainControlledEffectState(false);
    CHECK(g_log == "control.xf(4,5,6);control.start0;control.stop0;");

    // --- group id loading
    FakeEffect gb("gb"), gs("gs"), gc("gc"), ga("ga"), gi("gi"), gt("gt");
    host.fx.byGroup = {{{0x77, 5}, &gb}, {{0x77, 4}, &gs}, {{0x77, 7}, &gc}, {{0x77, 1}, &ga}, {{0x77, 2}, &gi},
                       {{0x77, 3}, &gt}};
    g_log.clear();
    c->SetEffectGroupID(0x77);
    CHECK(g_log == "gb.start0;");
    CHECK(gb.refs == 2 && gc.refs == 2 && gt.refs == 1 && button.refs == 1 && control.refs == 1);
    // a name that is still set keeps winning for the trigger
    g_log.clear();
    c->PlayTriggerEffect(c->SourceTransform());
    CHECK(g_log == "trigger.xf(4,5,6);trigger.start0;");

    // --- shutdown stops and releases everything, releases capture
    c->OnMouseDownL(0, 0, 0);
    CHECK(c->IsCapturing());
    host.mgr.released = nullptr;
    g_log.clear();
    CHECK(c->Shutdown());
    CHECK(host.mgr.released == &win && !c->IsCapturing());
    CHECK(g_log.find("gb.stop0;") != std::string::npos && g_log.find("gi.stop0;") != std::string::npos);
    CHECK(gb.refs == 1 && gs.refs == 1 && gc.refs == 1 && ga.refs == 1 && gi.refs == 1);
    CHECK(host.fx.refs == 0 && host.view.refs == 1);
    c->Release();

    // name-less tool: group trigger and the no-control press path
    {
        auto* d = new cSC4ViewInputControlEffect();
        d->AddRef();
        d->SetWindow(&win);
        d->Init();
        d->SetEffectGroupID(0x77);
        host.fx.byName.clear();
        g_log.clear();
        d->OnMouseDownL(0, 0, 0);
        // quirk: the trigger flag is looked up by name ("_trigger" with an empty name), so the group trigger never plays
        CHECK(g_log.find("gt.") == std::string::npos && g_log.find("ga.start0;") != std::string::npos);
        d->PlayTriggerEffect(d->SourceTransform());
        CHECK(g_log.find("gt.start0;") != std::string::npos);
        d->Shutdown();
        d->Release();
    }
    // tool without a view/effects
    {
        auto* d = new cSC4ViewInputControlEffect();
        d->AddRef();
        d->SetTerrainControlledEffectState(true);    // guarded: no control effect
        d->Release();
    }
    std::printf(failures ? "FAILED\n" : "ok\n");
    return failures ? 1 : 0;
}

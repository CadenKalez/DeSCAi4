// cGZScriptableControlSet test: fake controls (scriptable + legacy + render views) covering Insert / lookup / ownership
// calls, ThroughClass (keeps going after a refusal), Render and the reverse-order hit-tested ControlEvent.
#include <cstdint>
#include <cstdio>
#include <vector>

#include "gz/ScriptableControlSet.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {

std::vector<int> g_events;   // control ids in the order ControlEvent / Render reached them

struct FakeControl final : cIGZScriptableControl, cIGZLegacyControl, cIGZControl {
    std::uint32_t id;
    int refs = 0;
    bool own = true;
    cRZRect area{0, 0, 10, 10};
    bool handles = false, accept = true, implement = true;
    int implemented = 0, destroyed = 0;
    cIGZWin* window = nullptr;
    int* deathFlag = nullptr;
    explicit FakeControl(std::uint32_t i) : id(i) {}
    ~FakeControl() { if (deathFlag) ++*deathFlag; }
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cIGZLegacyControl) *out = static_cast<cIGZLegacyControl*>(this);
        else if (iid == kGZIID_cIGZControl) *out = static_cast<cIGZControl*>(this);
        else if (iid == 0x77 && accept) *out = static_cast<cIGZScriptableControl*>(this);
        else return false;
        AddRef();
        return true;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { if (--refs <= 0) { delete this; return 0; } return refs; }
    // cIGZScriptableControl
    bool SetParameters(tScriptPropertyArray&) override { return true; }
    IVocabulary* GetControlVocabulary() override { return nullptr; }
    std::uint32_t GetID() override { return id; }
    void SetID(std::uint32_t i) override { id = i; }
    void SetOwnControl(bool o) override { own = o; }
    bool ImplementInterface(cIGZScriptableControlSet*, std::uint32_t, std::uint32_t, cIGZVariant*) override { ++implemented; return implement; }
    bool SetParent(cIGZScriptableControl*) override { return true; }
    bool Render(cIGZGDriver*) override { g_events.push_back(int(id)); return true; }
    bool SetArea(cRZRect& a) override { area = a; return true; }
    bool GetArea(cRZRect& a) override { a = area; return true; }
    bool ControlEvent(cRZPoint&, std::uint32_t) override { g_events.push_back(int(id)); return handles; }
    cIGZWin* GetControl() override { return window; }
    void ForceControl(cIGZWin*) override {}
    void UseWinProc(gz::cIGZWinProc*) override {}
    bool GetScriptFragment(cIGZString**) override { return false; }
};

}  // namespace

int main() {
    auto* set = new cGZScriptableControlSet();
    set->AddRef();

    // QueryInterface: the two views, iid 1, unknown ids refused
    void* out = nullptr;
    CHECK(set->QueryInterface(kGZIID_cIGZScriptableControlSet, &out) && out == static_cast<cIGZScriptableControlSet*>(set));
    set->Release();
    CHECK(set->QueryInterface(1, &out) && out == static_cast<cIGZScriptableControlSet*>(set));
    set->Release();
    CHECK(set->QueryInterface(kGZIID_cIGZRenderControlSet, &out) && out == static_cast<cIGZRenderControlSet*>(set));
    set->Release();
    CHECK(static_cast<void*>(static_cast<cIGZRenderControlSet*>(set)) != static_cast<void*>(static_cast<cIGZScriptableControlSet*>(set)));
    CHECK(!set->QueryInterface(0x1234, &out));

    // Insert: null refused, a control gets one reference
    CHECK(!set->Insert(nullptr) && set->GetSize() == 0);
    int deaths = 0;
    FakeControl* c[4];
    for (int i = 0; i < 4; ++i) {
        c[i] = new FakeControl(100 + i);
        c[i]->deathFlag = &deaths;
        c[i]->area = {i * 20, 0, i * 20 + 30, 30};   // overlapping horizontally
        CHECK(set->Insert(c[i]));
        CHECK(c[i]->refs == 1);
    }
    CHECK(set->GetSize() == 4);
    CHECK(set->AsGZArray() != nullptr);

    // lookups
    CHECK(set->GetByID(102) == static_cast<cIGZScriptableControl*>(c[2]));
    CHECK(set->GetByID(7) == nullptr);
    CHECK(set->GetByIndex(3) == static_cast<cIGZScriptableControl*>(c[3]) && set->GetByIndex(4) == nullptr);
    FakeControl* noWin = c[0];
    CHECK(set->GetByIDAsIGZWin(100) == nullptr && noWin->refs == 1);   // no window: nothing, no leaked reference
    int win = 0;                                                        // (any non-null pointer value will do)
    c[1]->window = reinterpret_cast<cIGZWin*>(&win);
    CHECK(set->GetByIDAsIGZWin(101) == c[1]->window && c[1]->refs == 1);
    CHECK(set->GetByIndexAsIGZWin(1) == c[1]->window);
    CHECK(set->GetByIDAsIGZWin(999) == nullptr && set->GetByIndexAsIGZWin(9) == nullptr);

    // SetOwnControls reaches every control
    set->SetOwnControls(false);
    for (auto* x : c) CHECK(!x->own);
    set->SetOwnControls(true);
    for (auto* x : c) CHECK(x->own);

    // ThroughClass: controls answering the class are asked in order; a refusal does not stop the walk, and the result
    // is false once any refused (later ones are queried and released, but not asked again)
    c[1]->accept = false;                    // does not answer iid 0x77
    c[2]->implement = false;                 // answers but refuses
    CHECK(!set->ThroughClass(0x77, nullptr));
    CHECK(c[0]->implemented == 1 && c[1]->implemented == 0 && c[2]->implemented == 1 && c[3]->implemented == 0);
    for (auto* x : c) CHECK(x->refs == 1);   // every query was balanced
    c[2]->implement = true; c[2]->implemented = 0; c[0]->implemented = 0;
    CHECK(set->ThroughClass(0x77, nullptr));
    CHECK(c[0]->implemented == 1 && c[2]->implemented == 1 && c[3]->implemented == 1);

    // Render: every control draws, in insertion order
    g_events.clear();
    CHECK(set->Render(nullptr));
    CHECK((g_events == std::vector<int>{100, 101, 102, 103}));

    // ControlEvent: reverse order, only controls whose area holds the point (edges inclusive), stops at the first
    // handler, and always answers false
    g_events.clear();
    cRZPoint p{50, 10};   // inside c[1] (20..50), c[2] (40..70), c[3] (60..90 no)
    CHECK(!set->ControlEvent(p, 5));
    CHECK((g_events == std::vector<int>{102, 101}));          // c[3] misses, c[2] and c[1] contain it
    g_events.clear();
    c[2]->handles = true;
    CHECK(!set->ControlEvent(p, 5));
    CHECK((g_events == std::vector<int>{102}));                // handled by c[2]: the walk stops
    g_events.clear();
    cRZPoint outside{500, 500};
    set->ControlEvent(outside, 5);
    CHECK(g_events.empty());

    // Release: refcount 1 destroys the set and releases every control
    CHECK(set->Release() == 0);
    CHECK(deaths == 4);

    std::printf(failures ? "gz_scriptablecontrolset: %d FAILED\n" : "gz_scriptablecontrolset: all passed\n", failures);
    return failures ? 1 : 0;
}

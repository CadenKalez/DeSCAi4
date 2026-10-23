#include <cstdio>
#include <map>
#include <string>

#include "fake_resourcemanager.h"
#include "fake_scriptlua.h"
#include "rz/PortMemoryStreams.h"
#include "rz/cRZSysServPtr.h"
#include "sc4/AdviceNeighborDeal.h"
#include "sc4/DBSegment.h"
#include "sc4/MySimAdvice.h"
#include "sc4/MySimBalloonAdvice.h"
#include "sc4/RewardAdvice.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;
using namespace fakelua;

namespace {
struct Manager final : FakeResourceManager {
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::string> strings;
    bool GetResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out, std::uint32_t, cIGZUnknown*) override {
        if (iid != kGZIID_cIGZString || key.type != kResourceTypeSLString) return false;
        auto it = strings.find({key.group, key.instance});
        if (it == strings.end()) return false;
        *out = new cRZString(it->second.c_str());
        static_cast<cIGZString*>(*out)->AddRef();
        return true;
    }
};
Manager g_manager;
bool Resolve(std::uint32_t service, std::uint32_t iid, void** out) {
    if (service == 0x056b906e && iid == 0x656b8efc) { *out = static_cast<cIGZPersistResourceManager*>(&g_manager); return true; }
    return false;
}
struct SlHost final : ISLStringHost {
    int LanguageOffset() override { return 0; }
    cSCStringDetokenizer* Detokenizer() override { return nullptr; }
};

struct FakeBufferHost final : ISLBufferHost {
    std::vector<std::uint32_t> loaded;
    bool GetBufferResource(const cGZPersistResourceKey& key, std::int32_t, bool, cIGZBuffer** out) override {
        if (key.instance == 404) return false;   // a missing bitmap
        loaded.push_back(key.instance);
        auto* b = new cGZBuffer();
        b->AddRef();
        b->Init(4, 4, {kBufferColorXRGB8888, 32});
        *out = b;
        return true;
    }
    bool HasResource(const cGZPersistResourceKey&) override { return false; }
    bool LowQualityTextures() override { return false; }
    bool LoadBufferFromFile(const std::string&, std::int32_t, cIGZBuffer**) override { return false; }
    cIGZBuffer* CreateBuffer() override { return new cGZBuffer(); }
};

struct Host final : IAdviceHost {
    FakeLua lua;
    std::uint32_t createClsid = 0;
    int begins = 0, ends = 0;
    bool AdvisorSystemPresent() override { return true; }
    IScriptLua* Lua() override { return &lua; }
    void BeginScript(cSC4Advice*) override { ++begins; }
    void EndScript() override { ++ends; }
    bool DebugFunctionalityEnabled() override { return false; }
    cISC4Advice* CreateAdvice(std::uint32_t clsid, std::uint32_t iid) override {
        if (iid != kGZIID_cISC4Advice) return nullptr;
        cSC4Advice* a = nullptr;
        if (clsid == kGZCLSID_cSC4AdviceNeghborDeal) a = new cSC4AdviceNeghborDeal();
        else if (clsid == kGZCLSID_cSC4MySimBalloonAdvice) a = new cSC4MySimBalloonAdvice();
        else if (clsid == kGZCLSID_cSC4RewardAdvice) a = new cSC4RewardAdvice();
        else if (clsid == kGZCLSID_cSC4MySimAdvice) a = new cSC4MySimAdvice();
        if (!a) return nullptr;
        a->AddRef();
        createClsid = clsid;
        return a;
    }
    bool HasView3D() override { return false; }
    void GlobalEffectOn(std::uint32_t) override {}
    void GlobalEffectOff(std::uint32_t) override {}
};

struct RewardHost final : IRewardAdviceHost {
    std::map<std::uint32_t, std::pair<std::uint32_t, std::string>> states;
    std::uint32_t words[2] = {0x40, 0x48};
    bool haveBuilding = true;
    bool GetConditionState(std::uint32_t guid, std::uint32_t& state, cRZString& text) override {
        auto it = states.find(guid);
        if (it == states.end()) return false;
        state = it->second.first;
        text.AssignFromCString(it->second.second.c_str());
        return true;
    }
    void SetConditionState(std::uint32_t guid, std::uint32_t state, const cRZString& text) override { states[guid] = {state, text.ToChar()}; }
    bool GetBuildingWords(std::uint32_t, std::uint32_t& a, std::uint32_t& b) override {
        if (!haveBuilding) return false;
        a = words[0]; b = words[1];
        return true;
    }
    cISC4AdviceSubject* CreateSubject() override { auto* s = new cSC4AdviceSubject(); s->AddRef(); return s; }
};

struct SubjectHost final : IAdviceSubjectHost {
    std::int32_t CityCellsX() override { return 64; }
    std::int32_t CityCellsZ() override { return 64; }
};
struct MySimHost final : IMySimAdviceHost {
    std::vector<int> notified;
    bool NotifySimOfLink(int simId) override { notified.push_back(simId); return true; }
};

ValuePtr Row(std::map<std::string, ValuePtr> fields) {
    ValuePtr t = Tab();
    t->table->byName = std::move(fields);
    return t;
}
std::string S(const cIGZString& s) { return std::string(s.Data(), s.Strlen()); }
}  // namespace

int main() {
    SlHost slHost;
    SetSLStringHost(&slHost);
    SetRZServiceResolver(&Resolve);
    FakeBufferHost bufferHost;
    SetSLBufferHost(&bufferHost);
    SubjectHost subjectHost;
    SetAdviceSubjectHost(&subjectHost);
    g_manager.strings[{kAdvisorTextGroup, 5}] = "Balloon words";
    g_manager.strings[{kAdvisorTextGroup, 7}] = "Reward text";

    Host host;
    SetAdviceHost(&host);
    ValuePtr advices = Tab();
    advices->table->length = 4;
    advices->table->byIndex[1] = Row({{"guid", Num(0x101)}, {"trigger", Str("1")}, {"nd_type", Num(6)}, {"message", Str("text@5")}});
    advices->table->byIndex[2] = Row({{"guid", Num(0x102)}, {"trigger", Str("1")}, {"condition", Fn("1")}, {"message", Str("text@5")}});
    advices->table->byIndex[3] = Row({{"guid", Num(0x103)}, {"trigger", Str("1")}, {"message", Str("text@5")},
                                      {"balloon_message", Str("text@5")}, {"balloon_icon1", Num(11)}, {"balloon_icon3", Num(404)},
                                      {"balloon_icon4", Num(12)}, {"balloon_icon5", Num(0)}, {"balloon_sound", Num(8)},
                                      {"balloon_sound_f", Num(9)}, {"balloon_sound_chance", Num(30)}});
    advices->table->byIndex[4] = Row({{"guid", Num(0x104)}, {"trigger", Str("1")}, {"balloon_message", Str("")}});
    host.lua.globals["advices"] = advices;

    // ---- cSC4AdviceNeghborDeal
    {
        auto* a = new cSC4AdviceNeghborDeal();
        a->AddRef();
        CHECK(a->GetGZCLSID() == 0x6a3848db && a->GetDealType() == 0);
        CHECK(a->Init(1) && a->GetDealType() == 6 && a->GetGUID() == 0x101);
        host.lua.SetTop(0);
        CHECK(!a->Init(9));   // beyond the table: the second reader fails
        host.lua.SetTop(0);
        a->SetDealType(3);
        CHECK(a->GetDealType() == 3);
        CHECK(a->Init(1) && a->GetDealType() == 6);
        a->SetDealType(4);

        cISC4Advice* clone = nullptr;
        CHECK(a->Clone(&clone) && clone && host.createClsid == 0x6a3848db);
        auto* cloned = static_cast<cSC4AdviceNeghborDeal*>(clone);
        CHECK(cloned->GetDealType() == 4 && cloned->GetGUID() == 0x101);
        CHECK(cloned->RefCount() == 1);    // the query reference was released again
        clone->Release();

        PortMemoryOStream os;
        CHECK(a->Write(os));
        PortMemoryIStream is(os.bytes().data(), os.bytes().size());
        auto* r = new cSC4AdviceNeghborDeal();
        r->AddRef();
        CHECK(r->Read(is) && r->GetDealType() == 4 && r->GetGUID() == 0x101 && is.RemainingBytes() == 0);
        const std::uint8_t wrong[] = {3, 0, 0, 0};
        PortMemoryIStream bad(wrong, sizeof wrong);
        CHECK(!r->Read(bad) && bad.RemainingBytes() == 2);   // only the version was consumed
        void* iface = nullptr;
        CHECK(r->QueryInterface(kGZIID_cISC4AdviceNeighborDeal, &iface) && iface == static_cast<cISC4Advice*>(r));
        CHECK(r->QueryInterface(kGZIID_cISC4Advice, &iface) && iface == static_cast<cISC4Advice*>(r));
        CHECK(!r->QueryInterface(0x1234, &iface));
        r->Release(); r->Release(); r->Release();
        CHECK(a->Shutdown());
        a->Release();
    }

    // ---- cSC4RewardAdvice
    {
        RewardHost rewards;
        SetRewardAdviceHost(&rewards);
        auto* a = new cSC4RewardAdvice();
        a->AddRef();
        CHECK(a->GetGZCLSID() == 0xaa371c32);
        CHECK(a->Init(2) && a->CheckFlags(kAdviceFlagReward));
        host.lua.SetTop(0);
        CHECK(a->GetTrigger());   // condition 1: fires, attaches the subject
        CHECK(a->GetEventSubject() && a->GetEventSubject()->GetData1() == 0x40 && a->GetEventSubject()->GetData2() == 0x48);
        CHECK(rewards.states[0x102].first == 6 && host.lua.GetTop() == 0 && host.begins == host.ends);
        CHECK(!a->GetTrigger());  // already fired (bit 4): state stays 6
        CHECK(rewards.states[0x102].first == 6);
        // number 0 resets the state
        advices->table->byIndex[2]->table->byName["condition"] = Fn("0");
        CHECK(!a->GetTrigger() && rewards.states[0x102].first == 5);   // (6 & ~3) | 1
        // other numbers keep the state
        advices->table->byIndex[2]->table->byName["condition"] = Fn("5");
        rewards.states[0x102].first = 0x100 | 4;
        CHECK(!a->GetTrigger() && rewards.states[0x102].first == (0x100 | 4));
        // string result: reason text from the string table, bits 0-1 cleared
        advices->table->byIndex[2]->table->byName["condition"] = Fn("\"text@7\"");
        rewards.states[0x102].first = 7;
        CHECK(!a->GetTrigger() && rewards.states[0x102].first == 4 && rewards.states[0x102].second == "Reward text");
        advices->table->byIndex[2]->table->byName["condition"] = Fn("\"plain words\"");
        CHECK(!a->GetTrigger() && rewards.states[0x102].second == "plain words");
        // a failing call stores the state unchanged and fires nothing
        host.lua.callFails = true;
        CHECK(!a->GetTrigger() && host.lua.GetTop() == 0);
        host.lua.callFails = false;
        // fire again with the flag "no text" masking the result
        advices->table->byIndex[2]->table->byName["condition"] = Fn("1");
        rewards.states[0x102].first = 1;
        a->SetFlags(kAdviceFlagNoText, true);
        CHECK(!a->GetTrigger() && rewards.states[0x102].first == 6);
        a->SetFlags(kAdviceFlagNoText, false);
        // no condition table row
        a->Release();
        auto* missing = new cSC4RewardAdvice();
        missing->AddRef();
        missing->Init(1);   // row without "condition": the call fails (nil is not a function)
        host.lua.SetTop(0);
        CHECK(!missing->GetTrigger());
        missing->Release();
        SetRewardAdviceHost(nullptr);
    }

    // ---- cSC4MySimAdvice
    {
        MySimHost sims;
        SetMySimAdviceHost(&sims);
        auto* a = new cSC4MySimAdvice();
        a->AddRef();
        CHECK(a->GetGZCLSID() == 0x6a9335de);
        a->SetAdvisorID(33);
        host.lua.buffers.clear();
        a->DoLink(cRZString("go()"));
        CHECK(sims.notified.size() == 1 && sims.notified[0] == 33 && host.lua.buffers.empty());   // not current: no script
        a->SetFlags(kAdviceFlagCurrent, true);
        a->DoLink(cRZString("go()"));
        CHECK(sims.notified.size() == 2 && host.lua.buffers.size() == 1);
        a->Release();
        SetMySimAdviceHost(nullptr);
    }

    // ---- cSC4MySimBalloonAdvice
    {
        auto* a = new cSC4MySimBalloonAdvice();
        a->AddRef();
        CHECK(a->GetGZCLSID() == 0x6b70efad && a->GetIconBalloonCount() == 0 && a->GetSoundChance() == 0);
        CHECK(a->Init(3));
        host.lua.SetTop(0);
        CHECK(a->GetIconBalloonCount() == 2 && bufferHost.loaded.size() == 2 && bufferHost.loaded[0] == 11 && bufferHost.loaded[1] == 12);
        CHECK(a->GetIconBalloonID(0) == 11 && a->GetIconBalloonID(1) == 12 && a->GetIconBalloonID(2) == 0 && a->GetIconBalloonID(-1) == 0);
        CHECK(a->GetIconBalloon(0) && a->GetIconBalloon(1) && !a->GetIconBalloon(2) && !a->GetIconBalloon(-1));
        std::uint32_t key = 0;
        CHECK(a->GetIconBalloon(0)->GetTransparentColor(key) && key == a->GetIconBalloon(0)->ConvertRGBValueToNative(0xff, 0, 0xff));
        CHECK(a->GetSoundID(true) == 8 && a->GetSoundID(false) == 9 && a->GetSoundChance() == 30 && a->GetTextBalloonID() == 0);
        CHECK(a->GetGUID() == 0x103);
        a->SetFlags(kAdviceFlagCurrent, true);   // refreshes the three text fields
        CHECK(S(a->GetTextBalloon()) == "Balloon words" && S(a->GetTextMessage()) == "Balloon words");
        CHECK(a->GetTrigger());                  // current
        a->SetFlags(kAdviceFlagCurrent, false);
        CHECK(!a->GetTrigger());                 // neither current nor shown
        a->SetStatusFlags(kBalloonStatusShown, true);
        CHECK(a->CheckStatusFlags(kBalloonStatusShown) && !a->CheckStatusFlags(kBalloonStatusBlocked) && a->GetTrigger());
        a->SetStatusFlags(kBalloonStatusBlocked, true);
        CHECK(!a->GetTrigger() && a->CheckStatusFlags(6) && a->CheckStatusFlags(4));
        a->SetStatusFlags(kBalloonStatusBlocked | kBalloonStatusShown, false);
        CHECK(!a->CheckStatusFlags(6));
        CHECK(a->GetRawTrigger());

        // interface at +0x140 and refcounting through every view
        void* iface = nullptr;
        CHECK(a->QueryInterface(kGZIID_cISC4MySimBalloonAdvice, &iface));
        auto* balloon = static_cast<cISC4MySimBalloonAdvice*>(iface);
        CHECK(balloon->GetIconBalloonCount() == 2 && balloon->AsISC4Advice() == static_cast<cISC4Advice*>(a));
        CHECK(balloon->GetSoundID(true) == 8 && balloon->GetIconBalloonID(1) == 12);
        balloon->Release();
        CHECK(a->QueryInterface(kGZIID_cSC4MySimBalloonAdviceSelf, &iface) && iface == static_cast<cSC4MySimBalloonAdvice*>(a));
        a->Release();
        CHECK(a->QueryInterface(kGZIID_cISC4Advice, &iface) && iface == static_cast<cISC4Advice*>(a));
        a->Release();
        CHECK(a->RefCount() == 1);

        // a row with a balloon message only: no icons, so success depends on the message
        auto* empty = new cSC4MySimBalloonAdvice();
        empty->AddRef();
        CHECK(!empty->Init(4) && empty->GetIconBalloonCount() == 0);   // empty message, no icons -> false
        host.lua.SetTop(0);
        CHECK(!empty->Init(9));   // beyond the table
        host.lua.SetTop(0);
        empty->Release();

        // stream round trip uses the base record only
        PortMemoryOStream os;
        CHECK(a->Write(os));
        PortMemoryIStream is(os.bytes().data(), os.bytes().size());
        auto* r = new cSC4MySimBalloonAdvice();
        r->AddRef();
        CHECK(r->Read(is) && r->GetGUID() == 0x103 && is.RemainingBytes() == 0);
        r->Release();
        a->Release();
    }
    std::printf(failures ? "FAILED\n" : "sc4_advice_subclasses OK\n");
    return failures ? 1 : 0;
}

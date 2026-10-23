// cSC4Advisor: construction defaults, the four interface views, Init (advisor row, advice creation from the script
// tables, re-Init, failure paths), load_heads and the head animation selection, Simulate / simulate_day (trigger,
// timeout, retire) / simulate_day_events (queue quirk) / simulate_month, the four DoMessage messages, the advice
// activation/deactivation bookkeeping, PushEvent, RetireAdvice, Write/Read and Shutdown.
//
// The advices, subjects, message bus and host are fakes; the script tables are the shared FakeLua and the strings come
// from the shared FakeResourceManager. The nested objects of Write/Read need a cISC4DBSegment stream (see the
// cSC4Advice test), so the round trip here checks the scalar record, the pending events (which read back with a null
// subject) and the append behaviour.
#include <array>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "fake_resourcemanager.h"
#include "fake_scriptlua.h"
#include "rz/PortMemoryStreams.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZSysServPtr.h"
#include "sc4/Advisor.h"
#include "sc4/TablePropertyReader.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;
using namespace fakelua;

namespace {

// ---- the string resources ------------------------------------------------------------------------------
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

// ---- a scripted advice ---------------------------------------------------------------------------------
constexpr std::uint32_t kKnownClass = 0x9001, kOtherClass = 0x9002;
int g_sessionCounter = 0;

struct FakeAdvice final : cISC4Advice {
    // identity
    std::uint32_t classId = kKnownClass;
    int guid = 0, sessionId = 0, advisorId = 0, eventId = 0, commandId = 0;
    int frequency = 30, timeout = 30;
    std::uint32_t flags = 0;
    bool trigger = false, initOk = true;
    cRZString triggerString{"1"}, title{"Title"}, message{"Message"};
    std::uint32_t titleGuid = 0, messageGuid = 0;
    cISC4AdviceSubject* eventSubject = nullptr;
    IScriptLua* lua = nullptr;
    // records
    int refs = 1, initCalls = 0, shutdowns = 0, updateTexts = 0, lastRow = -1;

    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != kGZIID_cIGZUnknown) return false;
        *out = static_cast<cIGZUnknown*>(this);
        AddRef();
        return true;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }

    // The fake reads its own identity back from the same script row, like cSC4Advice::Init.
    bool Init(int adviceRow) override {
        ++initCalls;
        lastRow = adviceRow;
        if (!initOk || !lua) return false;
        cTablePropertyReader reader(lua, "advices", adviceRow);
        if (!reader.IsValid()) return false;
        guid = static_cast<int>(reader.GetNumber("guid"));
        sessionId = ++g_sessionCounter;
        eventId = static_cast<int>(reader.GetNumber("event"));
        commandId = static_cast<int>(reader.GetNumber("command"));
        frequency = static_cast<int>(reader.GetNumber("frequency"));
        timeout = static_cast<int>(reader.GetNumber("timeout"));
        trigger = reader.GetNumber("trigger") != 0;
        titleGuid = reader.GetNumber("title_guid");
        messageGuid = reader.GetNumber("message_guid");
        flags = reader.GetNumber("flags");
        if (eventId != 0) flags |= kAdviceFlagEvent;
        if (reader.GetNumber("class_id") != classId) flags |= kAdviceFlagCustomClass;
        return true;
    }
    bool Shutdown() override { ++shutdowns; return true; }
    bool Clone(cISC4Advice** out) override { if (out) *out = nullptr; return false; }
    int GetMood() const override { return 3; }
    int GetPriority() const override { return 50; }
    bool GetTrigger() const override { return trigger; }
    cIGZString& GetTriggerString() override { return triggerString; }
    int GetGUID() const override { return guid; }
    int GetAdvisorID() const override { return advisorId; }
    void SetAdvisorID(int id) override { advisorId = id; }
    int GetSessionID() const override { return sessionId; }
    bool SetFlags(int value, bool on) override {
        const std::uint32_t mask = static_cast<std::uint32_t>(value);
        if (on) flags |= mask; else flags &= ~mask;
        return true;
    }
    bool CheckFlags(int value) const override { return (flags & static_cast<std::uint32_t>(value)) == static_cast<std::uint32_t>(value); }
    int GetFrequencyInDays() const override { return frequency; }
    int GetTimeOutInDays() const override { return timeout; }
    const cIGZString& GetTextTitle() const override { return title; }
    std::uint32_t GetTextTitleGUID() const override { return titleGuid; }
    const cIGZString& GetTextMessage() const override { return message; }
    std::uint32_t GetTextMessageGUID() const override { return messageGuid; }
    bool DoLink(const cIGZString&) override { return true; }
    void NotifyGlobalEffectTriggerChanged() override {}
    bool SetEventSubject(cISC4AdviceSubject* subject) override {
        if (subject) subject->AddRef();
        if (eventSubject) eventSubject->Release();
        eventSubject = subject;
        return true;
    }
    cISC4AdviceSubject* GetEventSubject() override { return eventSubject; }
    int GetEventID() override { return eventId; }
    int GetCommandID() override { return commandId; }
    bool UpdateText() override { ++updateTexts; return true; }
    bool IsCurrent() const override { return CheckFlags(kAdviceFlagCurrent); }
};

struct FakeSubject final : cISC4AdviceSubject {
    int refs = 1;
    std::uint32_t data = 0;
    bool hasLocation = false;
    float x = 0, z = 0;
    cIGZUnknown* object = nullptr;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != kGZIID_cIGZUnknown) return false;
        *out = static_cast<cIGZUnknown*>(this);
        AddRef();
        return true;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    void SetLocation(float px, float pz) override { hasLocation = true; x = px; z = pz; }
    float GetLocationX() override { return x; }
    float GetLocationZ() override { return z; }
    bool HasLocation() override { return hasLocation; }
    bool SetObject(cIGZUnknown* value) override { object = value; return true; }
    cIGZUnknown* GetObjectA() override { return object; }
    std::uint32_t GetData1() const override { return data; }
    void SetData1(std::uint32_t value) override { data = value; }
    std::uint32_t GetData2() const override { return 0; }
    void SetData2(std::uint32_t) override {}
    bool Clone(cISC4AdviceSubject** out) override { if (out) *out = nullptr; return false; }
};

// ---- the message bus -----------------------------------------------------------------------------------
struct SentMessage {
    std::uint32_t type = 0;
    std::uint64_t data1 = 0;
    cIGZUnknown* unknown = nullptr;
    bool post = false;
};

struct FakeMessages final : cIGZMessageServer2 {
    int refs = 1;
    std::vector<SentMessage> sent;
    std::vector<std::pair<cIGZMessageTarget2*, std::uint32_t>> added, removed;
    bool autoClear = true;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != kGZIID_cIGZUnknown) return false;
        *out = static_cast<cIGZUnknown*>(this);
        AddRef();
        return true;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool MessageSend(cIGZMessage2* message) override {
        auto* standard = static_cast<cIGZMessage2Standard*>(message);
        sent.push_back(SentMessage{message->GetType(), standard->GetData1(), standard->GetIGZUnknown(), false});
        return true;
    }
    bool MessagePost(cIGZMessage2* message, bool) override {
        auto* standard = static_cast<cIGZMessage2Standard*>(message);
        sent.push_back(SentMessage{message->GetType(), standard->GetData1(), standard->GetIGZUnknown(), true});
        return true;
    }
    bool AddNotification(cIGZMessageTarget2* target, std::uint32_t type) override { added.emplace_back(target, type); return true; }
    bool RemoveNotification(cIGZMessageTarget2* target, std::uint32_t type) override { removed.emplace_back(target, type); return true; }
    bool GeneralMessagePostToTarget(cIGZMessage2*, cIGZMessageTarget2*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return static_cast<std::uint32_t>(sent.size()); }
    void SetAlwaysClearQueueOnTick(bool value) override { autoClear = value; }
    std::uint32_t GetRefCount() const override { return static_cast<std::uint32_t>(refs); }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }
};

// ---- the host ------------------------------------------------------------------------------------------
struct Host final : IAdvisorHost {
    FakeLua lua;
    FakeMessages* messages = nullptr;
    int dayNumber = 7;
    std::vector<std::pair<int, cISC4Advisor*>> registered, unregistered;
    std::vector<std::array<std::uint32_t, 5>> posted;
    int createCalls = 0;
    std::vector<std::unique_ptr<FakeAdvice>> advicePool;

    IScriptLua* Lua() override { return &lua; }
    void RegisterAdvice(int eventId, cISC4Advisor* advisor) override { registered.emplace_back(eventId, advisor); }
    void UnregisterAdvice(int eventId, cISC4Advisor* advisor) override { unregistered.emplace_back(eventId, advisor); }
    cIGZMessageServer2* Messages() override { return messages; }
    bool CreateInstance(std::uint32_t classId, std::uint32_t iid, void** out) override {
        ++createCalls;
        *out = nullptr;
        if (iid != kGZIID_cISC4Advice) return false;
        if (classId != kKnownClass && classId != kOtherClass) return false;
        auto advice = std::make_unique<FakeAdvice>();
        advice->classId = classId;
        advice->lua = &lua;
        *out = static_cast<cISC4Advice*>(advice.get());
        advice->AddRef();   // the reference GZCOM answers
        advicePool.push_back(std::move(advice));
        return true;
    }
    int SimDayNumber() override { return dayNumber; }
    void PostMessage2(std::uint32_t type, std::uint32_t data1, std::uint32_t data2, std::uint32_t data3,
                      std::uint32_t data4, cIGZUnknown*) override {
        posted.push_back({type, data1, data2, data3, data4});
    }
    void Reset() { registered.clear(); unregistered.clear(); posted.clear(); createCalls = 0; }
};

ValuePtr Row(std::map<std::string, ValuePtr> fields) {
    ValuePtr t = Tab();
    t->table->byName = std::move(fields);
    return t;
}
std::string S(const cIGZString& s) { return std::string(s.Data(), s.Strlen()); }

std::uint32_t MessageCount(const Host& host, std::uint32_t type, bool post) {
    std::uint32_t n = 0;
    for (const SentMessage& m : host.messages->sent) if (m.type == type && m.post == post) ++n;
    return n;
}

}  // namespace

int main() {
    SlHost slHost;
    SetSLStringHost(&slHost);
    SetRZServiceResolver(&Resolve);
    g_manager.strings[{kAdvisorTextGroup, 0x101}] = "Police Chief|the police";
    g_manager.strings[{kAdvisorTextGroup, 0x102}] = "Chief Smith";

    FakeMessages messages;
    Host host;
    host.messages = &messages;
    SetAdvisorHost(&host);

    // "advisors": one row for advisor type 2
    ValuePtr advisors = Tab();
    advisors->table->length = 2;
    advisors->table->byIndex[2] = Row({{"id", Num(21)}, {"caption", Str("text@0x00000101")}, {"name", Str("text@0x00000102")},
                                       {"advice_type", Num(4)}});
    host.lua.globals["advisors"] = advisors;

    // "advices": four rows of type 4 that resolve, one of another type and one of an unknown class
    ValuePtr advices = Tab();
    advices->table->length = 6;
    advices->table->byIndex[1] = Row({{"type", Num(4)}, {"guid", Num(0x1006)}, {"class_id", Num(kOtherClass)},
                                      {"trigger", Num(0)}, {"frequency", Num(8)}});
    advices->table->byIndex[2] = Row({{"type", Num(4)}, {"guid", Num(0x1001)}, {"class_id", Num(kKnownClass)},
                                      {"trigger", Num(1)}, {"frequency", Num(5)}, {"timeout", Num(10)},
                                      {"command", Num(0x8100)}, {"title_guid", Num(0x201)}, {"message_guid", Num(0x202)}});
    advices->table->byIndex[3] = Row({{"type", Num(4)}, {"guid", Num(0x1002)}, {"class_id", Num(kKnownClass)},
                                      {"trigger", Num(1)}, {"frequency", Num(6)}, {"timeout", Num(11)},
                                      {"event", Num(0x7001)}, {"command", Num(0x8001)}});
    advices->table->byIndex[4] = Row({{"type", Num(4)}, {"guid", Num(0x1003)}, {"class_id", Num(kKnownClass)},
                                      {"trigger", Num(0)}, {"frequency", Num(7)}, {"timeout", Num(12)}});
    advices->table->byIndex[5] = Row({{"type", Num(5)}, {"guid", Num(0x1004)}, {"class_id", Num(kKnownClass)}});
    advices->table->byIndex[6] = Row({{"type", Num(4)}, {"guid", Num(0x1005)}, {"class_id", Num(0x1234)}});
    host.lua.globals["advices"] = advices;

    // "advisor_heads": rows 1/2 for gender 1, row 3 for gender 0, row 4 for another advisor
    ValuePtr heads = Tab();
    heads->table->length = 4;
    heads->table->byIndex[1] = Row({{"advisor_type", Num(21)}, {"advisor_gender", Num(1)}});
    heads->table->byIndex[2] = Row({{"advisor_type", Num(21)}, {"advisor_gender", Num(1)}});
    heads->table->byIndex[3] = Row({{"advisor_type", Num(21)}, {"advisor_gender", Num(0)}});
    heads->table->byIndex[4] = Row({{"advisor_type", Num(22)}, {"advisor_gender", Num(1)}});
    // the animation ids are read with numeric keys (GetHeadAnimID passes its argument straight to GetNumber(int))
    heads->table->byIndex[1]->table->byIndex[0] = Num(0x11);
    heads->table->byIndex[1]->table->byIndex[1] = Num(0x12);
    heads->table->byIndex[2]->table->byIndex[0] = Num(0x21);
    heads->table->byIndex[3]->table->byIndex[0] = Num(0x31);
    heads->table->byIndex[4]->table->byIndex[0] = Num(0x41);
    host.lua.globals["advisor_heads"] = heads;

    // ---- construction defaults
    cSC4Advisor* a = new cSC4Advisor();
    a->AddRef();
    CHECK(a->GetID() == 0 && a->TableRow() == 0 && a->AdviceType() == 0);
    CHECK(S(a->GetName()).empty() && S(a->GetPersonalName()).empty());
    CHECK(a->Month() == 0 && a->DayNumber() == 0 && a->IterateIndex() == 0);
    CHECK(a->HeadAnim() == -1 && a->HeadAnimGender1() && !a->Subscribed());
    CHECK(a->GetCurrentAdvices().empty() && a->AdviceInfos().empty() && a->Events().empty());
    CHECK(a->GetGZCLSID() == 0xeac937b4 && a->GetHeadAnimCount(true) == 0 && a->GetHeadAnimCount(false) == 0);
    CHECK(!a->SetHeadAnim(true, 0) && a->HeadAnim() == -1 && a->GetHeadAnimID(0) == 0);

    // ---- layout: the four vtable groups and the members on the original offsets
    {
        const auto base = reinterpret_cast<const char*>(a);
        CHECK(sizeof(cSC4Advisor) == 0x148);
        CHECK(reinterpret_cast<const char*>(static_cast<cISC4Advisor*>(a)) - base == 0x10);
        CHECK(reinterpret_cast<const char*>(static_cast<cIGZSerializable*>(a)) - base == 0x18);
        CHECK(reinterpret_cast<const char*>(static_cast<cIGZMessageTarget2*>(a)) - base == 0x20);
        CHECK(reinterpret_cast<const char*>(&a->GetName()) - base == 0x30);
        CHECK(reinterpret_cast<const char*>(&a->GetPersonalName()) - base == 0x60);
        CHECK(reinterpret_cast<const char*>(&a->GetCurrentAdvices()) - base == 0xc0);
        CHECK(reinterpret_cast<const char*>(&a->AdviceInfos()) - base == 0xd8);
        CHECK(reinterpret_cast<const char*>(&a->Events()) - base == 0x128);
    }

    // ---- the four interface views
    {
        void* iface = nullptr;
        CHECK(a->QueryInterface(kGZIID_cISC4Advisor, &iface) && iface == static_cast<cISC4Advisor*>(a));
        CHECK(a->QueryInterface(0xe4fda3d4, &iface) && iface == static_cast<cIGZSerializable*>(a));
        CHECK(a->QueryInterface(0x452294aa, &iface) && iface == static_cast<cIGZMessageTarget2*>(a));
        CHECK(a->QueryInterface(kGZIID_cIGZUnknown, &iface) && iface == static_cast<cRZUnknown*>(a));
        CHECK(!a->QueryInterface(0x55, &iface));
        a->Release(); a->Release(); a->Release(); a->Release();   // the four references the queries took
    }

    // ---- Init
    CHECK(a->Init(2));
    CHECK(a->GetID() == 21 && a->TableRow() == 2 && a->AdviceType() == 4);
    CHECK(S(a->GetName()) == "Police Chief");         // the caption is cut at the first '|'
    CHECK(S(a->GetPersonalName()) == "Chief Smith");  // and the text comes from the string resources
    CHECK(host.createCalls == 5);                     // four rows resolve, the unknown class is rejected
    CHECK(a->AdviceInfos().size() == 4);
    CHECK(a->AdviceInfos()[0].advice->GetGUID() == 0x1001 && a->AdviceInfos()[1].advice->GetGUID() == 0x1002);
    CHECK(a->AdviceInfos()[2].advice->GetGUID() == 0x1003 && a->AdviceInfos()[3].advice->GetGUID() == 0x1006);
    CHECK(a->AdviceInfos()[1].advice->CheckFlags(kAdviceFlagEvent));
    for (const auto& info : a->AdviceInfos()) {
        CHECK(info.advice->GetAdvisorID() == 21 && info.nextDay == 0 && info.expiryDay == 0 && info.lastMonth == -1);
    }
    CHECK(a->GetCurrentAdvices().empty());
    CHECK(a->GetAdviceByGUID(0x1002) == a->AdviceInfos()[1].advice.get());
    CHECK(a->GetAdviceByGUID(0x9999) == nullptr);
    CHECK(a->GetAdviceBySessionID(a->AdviceInfos()[2].advice->GetSessionID()) == a->AdviceInfos()[2].advice.get());
    CHECK(a->GetAdviceBySessionID(-7) == nullptr);
    CHECK(MessageCount(host, 0x8a09fa68, true) == 1);   // the "loaded" notification
    CHECK(messages.added.size() == 4 && messages.removed.empty());
    CHECK(messages.added[0].first == static_cast<cIGZMessageTarget2*>(a) && a->Subscribed());
    CHECK(host.registered.size() == 1 && host.registered[0].first == 0x7001 &&
          host.registered[0].second == static_cast<cISC4Advisor*>(a));

    // ---- head animations
    CHECK(a->HeadAnim() == 0 && a->HeadAnimGender1());
    CHECK(a->GetHeadAnimCount(true) == 2 && a->GetHeadAnimCount(false) == 1);
    CHECK(a->GetHeadAnimID(0) == 0x11 && a->GetHeadAnimID(1) == 0x12 && a->GetHeadAnimID(5) == 0);
    CHECK(!a->SetHeadAnim(true, 2) && a->HeadAnim() == -1);
    CHECK(a->SetHeadAnim(false, 0) && a->HeadAnim() == 0 && !a->HeadAnimGender1() && a->GetHeadAnimID(0) == 0x31);
    CHECK(a->SetHeadAnim(true, 1) && a->HeadAnim() == 1 && a->HeadAnimGender1() && a->GetHeadAnimID(0) == 0x21);
    CHECK(a->load_heads() && a->HeadAnim() == 1);   // still in range: kept
    CHECK(!a->SetHeadAnim(true, -1) && a->HeadAnim() == -1);
    CHECK(!a->load_heads() && a->HeadAnim() == 0 && a->HeadAnimGender1());   // reselects the first gender-1 row

    // ---- Simulate: the month tick increments the month, the day tick refreshes the day and checks one advice
    host.dayNumber = 7;
    CHECK(a->Simulate(1) && a->Month() == 1 && a->DayNumber() == 0);
    CHECK(a->Simulate(2) && a->Month() == 1 && a->DayNumber() == 0);   // any other period does nothing
    CHECK(a->Simulate(0) && a->DayNumber() == 7 && a->IterateIndex() == 1);
    CHECK(a->GetCurrentAdvices().size() == 1);
    CHECK(a->AdviceInfos()[0].lastMonth == 1 && a->AdviceInfos()[0].expiryDay == 17);   // 7 + timeout 10
    CHECK(a->AdviceInfos()[0].advice->CheckFlags(kAdviceFlagCurrent));
    CHECK(MessageCount(host, 0x8a09fa64, true) == 1);
    CHECK(host.messages->sent.back().type == 0x8a09fa6c && host.messages->sent.back().data1 == 0x8100);
    // the event advice is not checked for a trigger, the third one has trigger 0 and the fourth one too
    CHECK(a->Simulate(0) && a->GetCurrentAdvices().size() == 1);
    CHECK(a->Simulate(0) && a->GetCurrentAdvices().size() == 1);
    CHECK(a->Simulate(0) && a->GetCurrentAdvices().size() == 1 && a->IterateIndex() == 4);
    // the next check wraps back to the first advice
    CHECK(a->Simulate(0) && a->IterateIndex() == 1);
    CHECK(a->CommandWouldTrigger(0x8100));            // the first advice: not event driven, command matches
    CHECK(!a->CommandWouldTrigger(0x8001));           // the event advice is not asked
    CHECK(!a->CommandWouldTrigger(0x9999));

    // ---- simulate_day: the first advice's timeout runs out on day 17
    host.dayNumber = 17;
    CHECK(a->Simulate(0) && a->GetCurrentAdvices().empty());
    CHECK(a->AdviceInfos()[0].nextDay == 22 && a->AdviceInfos()[0].expiryDay == 0);   // 17 + frequency 5
    CHECK(!a->AdviceInfos()[0].advice->CheckFlags(kAdviceFlagCurrent));
    CHECK(MessageCount(host, 0x8a09fa65, false) == 1);
    CHECK(!a->RetireAdvice(a->AdviceInfos()[0].advice.get()));   // not current any more
    CHECK(!a->RetireAdvice(nullptr));

    // ---- PushEvent / simulate_day_events: the event advice becomes current and gets the subject
    auto* subject = new FakeSubject();
    subject->data = 0x5151;
    {
        // an event driven advice has to be activated by an event, not by the day loop
        cISC4Advice* event = a->AdviceInfos()[1].advice.get();
        CHECK(!event->CheckFlags(kAdviceFlagCurrent));
        CHECK(a->PushEvent(0x7001, subject) && a->Events().size() == 1);
        CHECK(subject->refs == 2);   // the test's and the queue's reference
        a->simulate_day_events();
        CHECK(a->Events().empty());
        CHECK(subject->refs == 2);   // the queue released its reference, the advice took one
        CHECK(event->CheckFlags(kAdviceFlagCurrent) && event->GetEventSubject() == subject);
        CHECK(a->GetCurrentAdvices().size() == 1);
        CHECK(a->AdviceInfos()[1].lastMonth == a->Month());
        CHECK(MessageCount(host, 0x8a09fa6c, true) == 2);   // the activation's command notification
    }
    // an already current advice only marks its text dirty and swaps the subject in
    {
        cISC4Advice* event = a->AdviceInfos()[1].advice.get();
        CHECK(event->CheckFlags(kAdviceFlagCurrent));
        CHECK(!event->CheckFlags(kAdviceFlagTextDirty));
        CHECK(a->PushEvent(0x7001, subject));
        a->simulate_day_events();
        CHECK(event->CheckFlags(kAdviceFlagTextDirty) && event->GetEventSubject() == subject);
    }
    CHECK(a->RetireAdvice(a->AdviceInfos()[1].advice.get()));   // clears the subject and the current flag
    CHECK(a->GetCurrentAdvices().empty());
    CHECK(a->AdviceInfos()[1].nextDay == 23);                   // 17 + frequency 6
    // the queue quirk: every iteration reads the *last* queued event and pops the *first* one, so the older event
    // is dropped without ever being handled
    {
        CHECK(a->PushEvent(0x7001, subject) && a->PushEvent(0x9999, nullptr));
        CHECK(a->Events().size() == 2);
        a->simulate_day_events();   // reads 0x9999 twice and pops both
        CHECK(a->Events().empty());
        CHECK(a->GetCurrentAdvices().empty());   // 0x7001 was never handled
    }
    CHECK(a->PushEvent(0x9999, nullptr));   // left for the Write/Read round trip
    subject->Release();

    // ---- DoMessage 0xea917750 / 0x8a917759: re-evaluate the current advices; the first sets "show text keys"
    {
        cISC4Advice* current = a->AdviceInfos()[0].advice.get();
        current->SetFlags(kAdviceFlagCurrent, true);
        a->GetCurrentAdvices()[AdviceRef(current)] = 0;
        const std::uint32_t unloadedBefore = MessageCount(host, 0x8a09fa67, false);
        const std::uint32_t activatedBefore = MessageCount(host, 0x8a09fa64, false);
        cRZMessage2Standard message;
        message.SetType(0xea917750);
        message.SetData1(1);
        CHECK(a->DoMessage(&message));
        for (const auto& info : a->AdviceInfos()) CHECK(info.advice->CheckFlags(kAdviceFlagShowTextKeys));
        CHECK(a->GetCurrentAdvices().size() == 1);
        CHECK(current->CheckFlags(kAdviceFlagCurrent));   // dropped and set again
        CHECK(MessageCount(host, 0x8a09fa67, false) == unloadedBefore + 1);
        CHECK(MessageCount(host, 0x8a09fa64, false) == activatedBefore + 1);   // re-activated with a send, not a post
        // the reset message does not touch the text-key flag
        cRZMessage2Standard reset;
        reset.SetType(0x8a917759);
        CHECK(a->DoMessage(&reset));
        for (const auto& info : a->AdviceInfos()) CHECK(info.advice->CheckFlags(kAdviceFlagShowTextKeys));
        CHECK(MessageCount(host, 0x8a09fa67, false) == unloadedBefore + 2);
    }

    // ---- DoMessage 0x4a91dd6e for this advisor: activate the idle advices and post the trigger message
    {
        for (const auto& info : a->AdviceInfos()) info.advice->SetFlags(kAdviceFlagCurrent, false);
        a->GetCurrentAdvices().clear();
        cRZMessage2Standard changed;
        changed.SetType(0x4a91dd6e);
        changed.SetData1(21);   // this advisor's id
        CHECK(a->DoMessage(&changed));
        // every idle advice whose trigger text is not "=(0)" was activated (the trigger itself is not asked)
        CHECK(a->GetCurrentAdvices().size() == a->AdviceInfos().size());
        CHECK(host.posted.size() == 1 && host.posted[0][0] == 0xea917750 && host.posted[0][1] == 1);
        CHECK(a->AdviceInfos()[0].advice->CheckFlags(kAdviceFlagForceTrigger));
        // an advice whose trigger text is the debug text of trigger 0 is left alone
        for (const auto& info : a->AdviceInfos()) info.advice->SetFlags(kAdviceFlagCurrent, false);
        a->GetCurrentAdvices().clear();
        auto* first = static_cast<FakeAdvice*>(a->AdviceInfos()[0].advice.get());
        first->triggerString.AssignFromCString("_atrg_=(0)");
        cRZMessage2Standard again;
        again.SetType(0x4a91dd6e);
        again.SetData1(21);
        CHECK(a->DoMessage(&again));
        CHECK(a->GetCurrentAdvices().size() == a->AdviceInfos().size() - 1);
        first->triggerString.AssignFromCString("1");
    }

    // ---- DoMessage 0x4a91dd6e for another advice: matched by guid (flags 0x600) or by a text guid
    {
        for (const auto& info : a->AdviceInfos()) info.advice->SetFlags(kAdviceFlagCurrent, false);
        a->GetCurrentAdvices().clear();
        host.posted.clear();
        cRZMessage2Standard byGuid;
        byGuid.SetType(0x4a91dd6e);
        byGuid.SetData1(0x1003);
        CHECK(a->DoMessage(&byGuid));
        CHECK(a->GetCurrentAdvices().size() == 1);
        CHECK(a->AdviceInfos()[2].advice->CheckFlags(kAdviceFlagForceTrigger) &&
              a->AdviceInfos()[2].advice->CheckFlags(kAdviceFlagShowTextKeys));
        CHECK(host.posted.empty());   // the guid path does not post the trigger message
        // by title guid
        for (const auto& info : a->AdviceInfos()) info.advice->SetFlags(kAdviceFlagCurrent, false);
        a->GetCurrentAdvices().clear();
        cRZMessage2Standard byTitle;
        byTitle.SetType(0x4a91dd6e);
        byTitle.SetData1(0x201);
        CHECK(a->DoMessage(&byTitle));
        CHECK(a->GetCurrentAdvices().size() == 1);
        CHECK(a->AdviceInfos()[0].advice->CheckFlags(kAdviceFlagCurrent));
        // by message guid, for an advice that is already current: nothing happens
        cRZMessage2Standard byMessage;
        byMessage.SetType(0x4a91dd6e);
        byMessage.SetData1(0x202);
        CHECK(a->DoMessage(&byMessage) && a->GetCurrentAdvices().size() == 1);
        // an unknown id does nothing
        cRZMessage2Standard unknown;
        unknown.SetType(0x4a91dd6e);
        unknown.SetData1(0x9999);
        CHECK(a->DoMessage(&unknown) && a->GetCurrentAdvices().size() == 1);
    }

    // ---- DoMessage 0x4a92ecef: the debug dump updates the advice text and throws the text away
    {
        cRZMessage2Standard dump;
        dump.SetType(0x4a92ecef);
        dump.SetData1(0x1001);
        auto* first = static_cast<FakeAdvice*>(a->AdviceInfos()[0].advice.get());
        const int before = first->updateTexts;
        CHECK(a->DoMessage(&dump) && first->updateTexts == before + 1);
        cRZMessage2Standard other;
        other.SetType(0x4a92ecef);
        other.SetData1(0x9999);
        CHECK(a->DoMessage(&other) && first->updateTexts == before + 1);
        cRZMessage2Standard nonsense;
        nonsense.SetType(0x12345678);
        CHECK(a->DoMessage(&nonsense));
    }

    // ---- Write / Read
    {
        for (const auto& info : a->AdviceInfos()) info.advice->SetFlags(kAdviceFlagCurrent, false);
        a->GetCurrentAdvices().clear();
        PortMemoryOStream os;
        CHECK(a->Write(os));
        CHECK(os.bytes().size() > 10 && os.bytes()[0] == 3 && os.bytes()[1] == 0);   // version 3
        PortMemoryIStream is(os.bytes().data(), os.bytes().size());
        cSC4Advisor* r = new cSC4Advisor();
        r->AddRef();
        CHECK(r->Read(is));
        CHECK(r->TableRow() == 2 && r->GetID() == 21 && r->AdviceType() == 0);   // +0xa0 is not serialized
        CHECK(S(r->GetName()) == "Police Chief" && S(r->GetPersonalName()) == "Chief Smith");
        CHECK(r->Month() == a->Month() && r->DayNumber() == a->DayNumber());
        CHECK(r->AdviceInfos().size() == a->AdviceInfos().size());
        bool allNull = true;
        for (const auto& info : r->AdviceInfos()) allNull = allNull && !info.advice;
        CHECK(allNull);   // no DB segment stream: the nested advice objects do not load
        CHECK(r->GetCurrentAdvices().empty());
        CHECK(r->Events().size() == 1 && r->Events().front().first == 0x9999);
        CHECK(!r->Events().front().second);   // the subject needs a DB segment stream too
        // a wrong version is refused without touching the object
        PortMemoryOStream bad;
        bad.SetUint16(4);
        PortMemoryIStream badIn(bad.bytes().data(), bad.bytes().size());
        cSC4Advisor* v = new cSC4Advisor();
        v->AddRef();
        CHECK(!v->Read(badIn) && v->GetID() == 0 && v->AdviceInfos().empty());
        v->Release();
        // Read appends (kept quirk)
        PortMemoryIStream again(os.bytes().data(), os.bytes().size());
        CHECK(r->Read(again) && r->AdviceInfos().size() == 2 * a->AdviceInfos().size());
        r->Release();
    }

    // ---- Shutdown
    {
        const std::size_t createCalls = static_cast<std::size_t>(host.createCalls);
        const std::uint32_t unloadedBefore = MessageCount(host, 0x8a09fa67, false);
        CHECK(a->Shutdown());
        CHECK(static_cast<std::size_t>(host.createCalls) == createCalls);
        CHECK(!a->Subscribed() && messages.removed.size() == 4);
        CHECK(host.unregistered.size() == 1 && host.unregistered[0].first == 0x7001);
        CHECK(a->GetCurrentAdvices().empty() && a->AdviceInfos().empty());
        CHECK(a->Month() == 0 && a->DayNumber() == 0);
        CHECK(MessageCount(host, 0x8a09fa67, false) == unloadedBefore + 1);
        CHECK(a->Shutdown());   // idempotent: nothing to unsubscribe any more
        CHECK(messages.removed.size() == 4);
    }
    a->Release();

    // ---- Init failure paths
    {
        cSC4Advisor* b = new cSC4Advisor();
        b->AddRef();
        CHECK(!b->Init(3) && b->GetID() == 0 && !b->Subscribed());   // no such advisor row
        CHECK(b->Init(2) && b->GetID() == 21);
        // a bound id that the table changed under us fails Init
        advisors->table->byIndex[2]->table->byName["id"] = Num(22);
        CHECK(!b->Init(2) && b->GetID() == 21);
        CHECK(b->Subscribed());   // the failed Init left the subscription alone
        advisors->table->byIndex[2]->table->byName["id"] = Num(21);
        b->Release();

        // The "advices" probe cannot fail: a missing table is a valid row-0 reader over nil, so Init succeeds with an
        // empty advice list (kept).
        cSC4Advisor* c = new cSC4Advisor();
        c->AddRef();
        host.lua.globals.erase("advices");
        host.Reset();
        const std::uint32_t loadedBefore = MessageCount(host, 0x8a09fa68, true);
        CHECK(c->Init(2) && c->GetID() == 21 && S(c->GetName()) == "Police Chief" && c->Subscribed());
        CHECK(c->AdviceInfos().empty() && host.registered.empty() && host.createCalls == 0);
        CHECK(MessageCount(host, 0x8a09fa68, true) == loadedBefore + 1);
        c->Shutdown();
        c->Release();
        host.lua.globals["advices"] = advices;
    }

    // ---- re-Init after the table changed: the advice of another class is retired and created again
    {
        cSC4Advisor* e = new cSC4Advisor();
        e->AddRef();
        CHECK(e->Init(2) && e->AdviceInfos().size() == 4);
        auto* oldFirst = static_cast<FakeAdvice*>(e->AdviceInfos()[0].advice.get());
        CHECK(oldFirst->GetGUID() == 0x1001 && oldFirst->classId == kKnownClass);
        const int createCalls = host.createCalls;
        auto oldRow = advices->table->byIndex[2];
        advices->table->byIndex[2] = Row({{"type", Num(4)}, {"guid", Num(0x1001)}, {"class_id", Num(kOtherClass)},
                                          {"trigger", Num(1)}, {"frequency", Num(5)}});
        CHECK(e->Init(2));
        CHECK(oldFirst->shutdowns == 1);      // retired, shut down and removed
        CHECK(host.createCalls > createCalls); // and created again with the row's class
        FakeAdvice* newFirst = nullptr;
        for (const auto& info : e->AdviceInfos())
            if (info.advice->GetGUID() == 0x1001) newFirst = static_cast<FakeAdvice*>(info.advice.get());
        CHECK(newFirst && newFirst != oldFirst && newFirst->classId == kOtherClass);
        CHECK(e->AdviceInfos().size() == 4);
        e->Shutdown();
        e->Release();
        advices->table->byIndex[2] = oldRow;
    }

    // ---- reference counting through the interface views
    {
        cSC4Advisor* f = new cSC4Advisor();
        f->AddRef();
        cISC4Advisor* asAdvisor = nullptr;
        CHECK(f->QueryInterface(kGZIID_cISC4Advisor, reinterpret_cast<void**>(&asAdvisor)) && asAdvisor);
        CHECK(asAdvisor->Init(2) && asAdvisor->GetID() == 21);
        asAdvisor->Shutdown();
        asAdvisor->Release();   // the interface reference
        f->Release();
    }

    delete subject;
    host.advicePool.clear();
    std::printf(failures ? "FAILED (%d)\n" : "sc4_advisor OK\n", failures);
    return failures ? 1 : 0;
}

#include "sc4/Advisor.h"

#include <cstddef>

#include "rz/StringConversion.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/DBSegment.h"
#include "sc4/TablePropertyReader.h"

namespace sc4 {

namespace {
IAdvisorHost* g_host = nullptr;
constexpr std::uint16_t kAdvisorStreamVersion = 3;
constexpr std::uint32_t kTextCodePage = 0x4e4;   // 1252
constexpr int kOnceForever = 0x7fffffff;
constexpr std::size_t kNotFound = static_cast<std::size_t>(-1);
const char kCaptionSeparator[] = "|";
const char kZeroTriggerText[] = "=(0)";          // the debug trigger text of a trigger that is literally 0 (kept quirk)
const char kDumpRule[] = "------------------------------------------\n";
const char kDumpTriggerHeader[] = "\t\t\t*Trigger*==\n";
const char kDumpTitleHeader[] = "\t\t\t*Title*==\n";
const char kDumpMessageHeader[] = "\t\t\t*Message*==\n";

// sNotify<T> (the free template the original posts its notifications with): QueryInterface the object for
// cIGZUnknown, build a cRZMessage2Standard with type / unknown / data1 and send or post it on SL::spMS2. A failed
// QueryInterface posts nothing.
void Notify(cIGZMessageServer2* messages, cIGZUnknown* object, std::uint32_t type, int value, bool post) {
    cIGZUnknown* unknown = nullptr;
    if (object && !object->QueryInterface(kGZIID_cIGZUnknown, reinterpret_cast<void**>(&unknown))) return;
    cRZMessage2Standard* message = new cRZMessage2Standard();
    message->AddRef();
    message->SetType(type);
    message->SetIGZUnknown(unknown);
    message->SetData1(static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
    if (messages) {
        if (post) {
            messages->MessagePost(message, false);
        } else {
            messages->MessageSend(message);
        }
    }
    message->Release();
    if (unknown) unknown->Release();
}

// The name fields: the script text gives the string resource key, the resource gives the text; a text that does not
// load is converted from code page 1252 first (the original's fallback).
void LoadScriptString(cADVString& text, const cRZString& scriptText) {
    text.FromScriptText(scriptText.ToChar());
    if (!text.Load(true, false)) {
        cRZString converted(scriptText);
        ConvertStringEncoding(converted, kTextCodePage, converted);
        text.Copy(converted);
    }
}
}  // namespace

IAdvisorHost* AdvisorHost() { return g_host; }
void SetAdvisorHost(IAdvisorHost* host) { g_host = host; }

// The original constructor sets the four vtables, the table row, the two name strings, the counters, the two maps,
// the three vectors, the head animation state (index -1, list 1), the event list and the subscription flag.
cSC4Advisor::cSC4Advisor() = default;

cSC4Advisor::~cSC4Advisor() = default;

bool cSC4Advisor::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZMessageTarget2) {
        *out = static_cast<cIGZMessageTarget2*>(this);
    } else if (iid == kGZIID_cIGZSerializable) {
        *out = static_cast<cIGZSerializable*>(this);
    } else if (iid == kGZIID_cISC4Advisor) {
        *out = static_cast<cISC4Advisor*>(this);
    } else {
        return cRZUnknown::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

std::uint32_t cSC4Advisor::AddRef() { return cRZUnknown::AddRef(); }

std::uint32_t cSC4Advisor::Release() { return cRZUnknown::Release(); }

// Reads "advisors[advisorType]", then every "advices" row of this advisor's "advice_type", creates one advice per row
// and registers the event driven ones with the advisor system.
bool cSC4Advisor::Init(int advisorType) {
    IAdvisorHost* host = g_host;
    IScriptLua* lua = host->Lua();
    tableRow_ = advisorType;
    {
        cTablePropertyReader reader(lua, "advisors", advisorType);
        if (reader.IsValid()) {
            const int id = static_cast<int>(reader.GetNumber("id"));
            if (id_ != 0 && id_ != id) return false;   // the row is stored before the check; a mismatch fails Init
            id_ = id;
            cRZString caption(reader.GetString("caption"));
            LoadScriptString(name_, caption);
            const std::size_t bar = name_.FindChars(kCaptionSeparator, 0, true);
            if (bar != kNotFound) {
                const cRZString left = name_.Left(bar);
                name_.Copy(left);
            }
            cRZString personal(reader.GetString("name"));
            if (personal.Strlen() != 0) LoadScriptString(personalName_, personal);
            adviceType_ = static_cast<int>(reader.GetNumber("advice_type"));
            Notify(host->Messages(), static_cast<cRZUnknown*>(this), kAdvisorNotificationLoaded, 0, true);
        } else {
            return false;
        }
    }

    // The original probes the advice table with a row-0 reader and fails Init when it is invalid. A row-0 reader is
    // valid for any table length (0 >= 0), so a missing table does not fail here - Init just creates no advices (kept).
    {
        cTablePropertyReader probe(lua, "advices", 0);
        if (!probe.IsValid()) return false;
    }

    std::map<std::uint32_t, std::pair<int, int>> adviceRows;   // guid -> (table row, class id)
    lua->GetGlobal("advices");
    const int rowCount = lua->ObjLen(-1);
    lua->Pop(1);
    for (int row = 1; row <= rowCount; ++row) {
        cTablePropertyReader reader(lua, "advices", row);
        if (!reader.IsValid()) continue;
        if (static_cast<int>(reader.GetNumber("type")) != adviceType_) continue;
        const std::uint32_t guid = reader.GetNumber("guid");
        const std::uint32_t classId = reader.GetNumber("class_id");
        if (guid != 0 && classId != 0) adviceRows[guid] = std::make_pair(row, static_cast<int>(classId));
    }

    // A re-Init (after a Read) drops the current advices first.
    while (!currentAdvices_.empty()) {
        AdviceMap::iterator it = currentAdvices_.begin();
        it->first->SetFlags(kAdviceFlagCurrent, false);
        currentAdvices_.erase(it);
    }
    Notify(host->Messages(), static_cast<cRZUnknown*>(this), kAdvisorNotificationUnloaded, 0, false);

    // Re-initialise the advices already loaded: keep the ones the table still has, retire the others.
    std::vector<std::size_t> removed;
    for (std::size_t i = 0; i < advices_.size(); ++i) {
        cAdviceInfo& info = advices_[i];
        cISC4Advice* advice = info.advice;
        const std::uint32_t guid = static_cast<std::uint32_t>(advice->GetGUID());
        std::map<std::uint32_t, std::pair<int, int>>::iterator row = adviceRows.find(guid);
        if (row == adviceRows.end()) {
            removed.push_back(i);
            continue;
        }
        if (!advice->Init(row->second.first)) {
            advice->SetFlags(kAdviceFlagCustomClass, false);
            removed.push_back(i);
        } else if (advice->CheckFlags(kAdviceFlagCustomClass)) {
            removed.push_back(i);
        }
        if (!advice->CheckFlags(kAdviceFlagCustomClass)) adviceRows.erase(row);
    }
    for (std::size_t n = removed.size(); n != 0; --n) {
        const std::size_t index = removed[n - 1];
        cAdviceInfo& info = advices_[index];
        cISC4Advice* advice = info.advice;
        RetireAdvice(advice);
        advice->Shutdown();
        advices_.erase(advices_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    // Create the advices the table has and the object does not: one per remaining row.
    for (std::map<std::uint32_t, std::pair<int, int>>::const_iterator row = adviceRows.begin();
         row != adviceRows.end(); ++row) {
        cISC4Advice* advice = nullptr;
        bool created = host->CreateInstance(static_cast<std::uint32_t>(row->second.second), kGZIID_cISC4Advice,
                                            reinterpret_cast<void**>(&advice));
        if (created && advice && advice->Init(row->second.first)) {
            advice->SetAdvisorID(GetID());
            cAdviceInfo info;
            info.advice = advice;               // the vector takes one reference
            info.nextDay = dayNumber_;
            info.expiryDay = 0;
            info.lastMonth = month_ - 1;
            advices_.push_back(info);
        }
        if (advice) advice->Release();          // the reference CreateInstance answered
    }

    // Rebuild the event -> advice map and (re-)register the event driven advices.
    eventAdvices_.clear();
    for (std::size_t i = 0; i < advices_.size(); ++i) {
        cISC4Advice* advice = advices_[i].advice;
        if (!advice->CheckFlags(kAdviceFlagEvent)) continue;
        const int eventId = advice->GetEventID();
        eventAdvices_[eventId] = static_cast<int>(i);
        host->RegisterAdvice(eventId, static_cast<cISC4Advisor*>(this));
    }

    load_heads();
    if (!subscribed_) {
        cIGZMessageServer2* messages = host->Messages();
        messages->AddNotification(this, kAdviceMessageTriggered);
        messages->AddNotification(this, kAdviceMessageReset);
        messages->AddNotification(this, kAdviceMessageChanged);
        messages->AddNotification(this, kAdviceMessageDump);
        subscribed_ = true;
    }
    return true;
}

// "advisor_heads": the rows whose "advisor_type" is this advisor's id, split by "advisor_gender" (1 goes to the first
// list, every other value to the second). Keeps the selected animation when it is still in range; otherwise selects the
// first row of the first non-empty list. Returns true when the selection was still valid.
bool cSC4Advisor::load_heads() {
    IAdvisorHost* host = g_host;
    IScriptLua* lua = host->Lua();
    lua->GetGlobal("advisor_heads");
    const int rowCount = lua->ObjLen(-1);
    lua->Pop(1);
    headAnimations1_.clear();
    headAnimations0_.clear();
    for (int row = 1; row <= rowCount; ++row) {
        cTablePropertyReader reader(lua, "advisor_heads", row);
        if (static_cast<int>(reader.GetNumber("advisor_type")) != GetID()) continue;
        if (reader.GetNumber("advisor_gender") == 1) {
            headAnimations1_.push_back(row);
        } else {
            headAnimations0_.push_back(row);
        }
    }
    if (headAnim_ >= 0) {
        const std::size_t count = headAnimList_ ? headAnimations1_.size() : headAnimations0_.size();
        if (static_cast<std::size_t>(headAnim_) < count) return true;
        headAnim_ = -1;
    }
    if (!headAnimations1_.empty()) {
        headAnimList_ = true;
        headAnim_ = 0;
    } else if (!headAnimations0_.empty()) {
        headAnimList_ = false;
        headAnim_ = 0;
    }
    return false;
}

bool cSC4Advisor::Shutdown() {
    if (subscribed_) {
        cIGZMessageServer2* messages = g_host->Messages();
        messages->RemoveNotification(this, kAdviceMessageTriggered);
        messages->RemoveNotification(this, kAdviceMessageReset);
        messages->RemoveNotification(this, kAdviceMessageChanged);
        messages->RemoveNotification(this, kAdviceMessageDump);
        subscribed_ = false;
    }
    month_ = 0;
    dayNumber_ = 0;   // the original clears both with one 8-byte store
    while (!currentAdvices_.empty()) {
        AdviceMap::iterator it = currentAdvices_.begin();
        it->first->SetFlags(kAdviceFlagCurrent, false);
        currentAdvices_.erase(it);
    }
    Notify(g_host->Messages(), static_cast<cRZUnknown*>(this), kAdvisorNotificationUnloaded, 0, false);
    int previousEventId = 0;
    while (!eventAdvices_.empty()) {
        std::map<int, int>::iterator it = eventAdvices_.begin();
        if (previousEventId != it->first) {   // as the original: an event id of 0 is never unregistered
            g_host->UnregisterAdvice(it->first, static_cast<cISC4Advisor*>(this));
            previousEventId = it->first;
        }
        eventAdvices_.erase(it);
    }
    for (std::size_t n = advices_.size(); n != 0; --n) {
        // Shutdown, then release the reference the advice list holds (the original releases explicitly and drops the
        // element without running its destructor; popping releases exactly that one reference).
        if (advices_.back().advice) advices_.back().advice->Shutdown();
        advices_.pop_back();
    }
    return true;
}

int cSC4Advisor::GetID() const { return id_; }

const cIGZString& cSC4Advisor::GetName() const { return name_; }

void cSC4Advisor::SetName(const cIGZString& name) { name_.Copy(name); }

cIGZString& cSC4Advisor::GetPersonalName() { return personalName_; }

void cSC4Advisor::SetPersonalName(const cIGZString& name) { personalName_.Copy(name); }

const AdviceMap& cSC4Advisor::GetCurrentAdvices() const { return currentAdvices_; }

AdviceMap& cSC4Advisor::GetCurrentAdvices() { return currentAdvices_; }

cISC4Advice* cSC4Advisor::GetAdviceByGUID(int guid) const {
    for (const cAdviceInfo& info : advices_) {
        if (info.advice->GetGUID() == guid) return info.advice.get();
    }
    return nullptr;
}

cISC4Advice* cSC4Advisor::GetAdviceBySessionID(int sessionId) const {
    for (const cAdviceInfo& info : advices_) {
        if (info.advice->GetSessionID() == sessionId) return info.advice.get();
    }
    return nullptr;
}

// period 0 (day) refreshes the simulation day and runs simulate_day; period 1 (month) increments the month counter
// and runs simulate_month (an empty base method). Any other period does nothing but answer true.
bool cSC4Advisor::Simulate(int period) {
    if (period == 1) {
        ++month_;
        simulate_month();
    } else if (period == 0) {
        dayNumber_ = g_host->SimDayNumber();
        simulate_day();
    }
    return true;
}

int cSC4Advisor::GetHeadAnimCount(bool gender1) {
    return static_cast<int>((gender1 ? headAnimations1_ : headAnimations0_).size());
}

bool cSC4Advisor::SetHeadAnim(bool gender1, int index) {
    if (index >= 0 && index < GetHeadAnimCount(gender1)) {
        headAnim_ = index;
        headAnimList_ = gender1;
        return true;
    }
    headAnim_ = -1;   // the selected list is left alone on failure
    return false;
}

int cSC4Advisor::GetHeadAnimID(int animIndex) {
    const std::vector<int>& animations = headAnimList_ ? headAnimations1_ : headAnimations0_;
    if (headAnim_ < 0 || static_cast<std::size_t>(headAnim_) >= animations.size()) return 0;
    cTablePropertyReader reader(g_host->Lua(), "advisor_heads", animations[static_cast<std::size_t>(headAnim_)]);
    if (!reader.IsValid()) return 0;
    return static_cast<int>(reader.GetNumber(animIndex));
}

bool cSC4Advisor::Write(cIGZOStream& stream) const {
    stream.SetUint16(kAdvisorStreamVersion);
    stream.SetSint32(tableRow_);
    stream.SetGZStr(name_);
    stream.SetGZStr(personalName_);
    stream.SetSint32(month_);
    stream.SetSint32(dayNumber_);
    stream.SetSint32(id_);
    stream.SetSint32(iterateIndex_);
    stream.SetSint32(static_cast<std::int32_t>(advices_.size()));
    for (const cAdviceInfo& info : advices_) {
        SC4WriteGZSerializable(stream, info.advice.get());
        stream.SetSint32(info.expiryDay);
        stream.SetSint32(info.lastMonth);
        stream.SetSint32(info.nextDay);
        stream.SetSint8(static_cast<std::int8_t>(info.advice->CheckFlags(kAdviceFlagCurrent) ? 1 : 0));
        stream.SetSint32(info.advice->GetEventID());
    }
    stream.SetSint32(static_cast<std::int32_t>(events_.size()));
    // back to front (kept quirk: a save/load round trip reverses the pending events)
    for (EventList::const_reverse_iterator it = events_.rbegin(); it != events_.rend(); ++it) {
        stream.SetSint32(it->first);
        SC4WriteGZSerializable(stream, it->second.get());
    }
    return !stream.GetError();
}

bool cSC4Advisor::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != kAdvisorStreamVersion) return false;
    stream.GetSint32(tableRow_);
    stream.GetGZStr(name_);
    stream.GetGZStr(personalName_);
    stream.GetSint32(month_);
    stream.GetSint32(dayNumber_);
    stream.GetSint32(id_);
    stream.GetSint32(iterateIndex_);
    std::int32_t adviceCount = 0;
    stream.GetSint32(adviceCount);
    for (; adviceCount != 0; --adviceCount) {
        AdviceRef advice;
        SC4ReadGZSerializable(stream, kGZIID_cISC4Advice, reinterpret_cast<void**>(advice.AsOutParam()), false);
        const std::size_t index = advices_.size();
        cAdviceInfo info;
        info.advice = advice;
        std::int32_t expiryDay = 0, lastMonth = 0, nextDay = 0;
        stream.GetSint32(expiryDay);
        stream.GetSint32(lastMonth);
        stream.GetSint32(nextDay);
        info.expiryDay = expiryDay;
        info.lastMonth = lastMonth;
        info.nextDay = nextDay;
        advices_.push_back(info);   // appends: the list is not cleared first (kept)
        std::int8_t current = 0;
        stream.GetSint8(current);
        if (current != 0) currentAdvices_[info.advice] = static_cast<int>(index);
        std::int32_t eventId = 0;
        stream.GetSint32(eventId);
        if (eventId != 0) eventAdvices_[eventId] = static_cast<int>(index);
    }
    std::int32_t eventCount = 0;
    stream.GetSint32(eventCount);
    for (; eventCount != 0; --eventCount) {
        std::int32_t eventId = 0;
        stream.GetSint32(eventId);
        cRZAutoRefCount<cISC4AdviceSubject> subject;
        SC4ReadGZSerializable(stream, kGZIID_cISC4AdviceSubject, reinterpret_cast<void**>(subject.AsOutParam()), false);
        events_.push_back(std::make_pair(eventId, subject));   // appends too (kept)
    }
    return !stream.GetError();
}

std::uint32_t cSC4Advisor::GetGZCLSID() const { return kGZCLSID_cSC4Advisor; }

bool cSC4Advisor::DoMessage(cIGZMessage2* message) {
    // The payload is read through the standard-message slots without checking the message's class (as the original).
    auto* standard = static_cast<cIGZMessage2Standard*>(message);
    const std::uint32_t type = message->GetType();
    if (type == kAdviceMessageTriggered || type == kAdviceMessageReset) {
        // "show text keys" follows the data word of the trigger message (0xea917750 only).
        if (type == kAdviceMessageTriggered) {
            const bool showKeys = standard->GetData1() != 0;
            for (const cAdviceInfo& info : advices_) info.advice->SetFlags(kAdviceFlagShowTextKeys, showKeys);
        }
        // Snapshot the current advices, clear them, notify, then restore the snapshot and re-evaluate each of them.
        const AdviceMap snapshot = currentAdvices_;
        currentAdvices_.clear();
        Notify(g_host->Messages(), static_cast<cRZUnknown*>(this), kAdvisorNotificationUnloaded, 0, false);
        if (!snapshot.empty()) {
            currentAdvices_ = snapshot;
            for (AdviceMap::iterator it = currentAdvices_.begin(); it != currentAdvices_.end(); ++it) {
                it->first->SetFlags(kAdviceFlagCurrent, false);
                it->first->SetFlags(kAdviceFlagCurrent, true);
                Notify(g_host->Messages(), it->first.get(), kAdviceNotificationActivated, 0, false);
            }
        }
        return true;
    }

    if (type == kAdviceMessageChanged) {
        const int id = static_cast<int>(standard->GetData1());
        if (id_ == id) {
            // The advice system changed an advice of this advisor: activate the idle ones whose trigger is not "0".
            for (std::size_t i = 0; i < advices_.size(); ++i) {
                cAdviceInfo& info = advices_[i];
                cISC4Advice* advice = info.advice.get();
                if (!advice) continue;
                if (advice->CheckFlags(kAdviceFlagCurrent)) continue;
                if (advice->CheckFlags(kAdviceFlagNoText)) continue;
                const cRZString zeroTrigger(kZeroTriggerText);
                if (advice->GetTriggerString().FindString(zeroTrigger, 0, true) != kNotFound) continue;
                advice->SetFlags(kAdviceFlagForceTrigger, true);
                info.lastMonth = month_;
                advice->SetEventSubject(nullptr);
                currentAdvices_[info.advice] = static_cast<int>(i);
                on_advice_activation(info);
            }
            g_host->PostMessage2(kAdviceMessageTriggered, 1, 0, 0, 0, nullptr);
            return true;
        }
        // Another advisor's advice: it may be one of ours by guid, title guid or message guid.
        for (std::size_t i = 0; i < advices_.size(); ++i) {
            cAdviceInfo& info = advices_[i];
            cISC4Advice* advice = info.advice.get();
            const std::uint32_t wanted = static_cast<std::uint32_t>(id);
            if (advice->GetGUID() != id && advice->GetTextTitleGUID() != wanted && advice->GetTextMessageGUID() != wanted)
                continue;
            if (advice->CheckFlags(kAdviceFlagCurrent)) return true;
            if (advice->CheckFlags(kAdviceFlagNoText)) return true;
            const cRZString zeroTrigger(kZeroTriggerText);
            if (advice->GetTriggerString().FindString(zeroTrigger, 0, true) != kNotFound) return true;
            advice->SetFlags(kAdviceFlagForceTrigger | kAdviceFlagShowTextKeys, true);
            info.lastMonth = month_;
            advice->SetEventSubject(nullptr);
            currentAdvices_[info.advice] = static_cast<int>(i);
            on_advice_activation(info);
            return true;
        }
        return true;
    }

    if (type == kAdviceMessageDump) {
        const int id = static_cast<int>(standard->GetData1());
        for (const cAdviceInfo& info : advices_) {
            cISC4Advice* advice = info.advice.get();
            const std::uint32_t wanted = static_cast<std::uint32_t>(id);
            if (advice->GetGUID() != id && advice->GetTextTitleGUID() != wanted && advice->GetTextMessageGUID() != wanted)
                continue;
            if (!advice) return true;
            advice->UpdateText();
            // The original formats the advice into a cRZString and drops it (its output call is compiled out).
            cRZString dump;
            dump.str().reserve(0x800);
            dump.AppendChars(kDumpRule, sizeof(kDumpRule) - 1);
            cRZString line;
            line.Sprintf("-- Advice %08x -----------------------\n", static_cast<unsigned>(advice->GetGUID()));
            dump.AppendString(line);
            dump.AppendChars(kDumpTriggerHeader, sizeof(kDumpTriggerHeader) - 1);
            line.Sprintf("\t%s\n", advice->GetTriggerString().ToChar());
            dump.AppendString(line);
            dump.AppendChars(kDumpTitleHeader, sizeof(kDumpTitleHeader) - 1);
            line.Sprintf("\t%s\n", advice->GetTextTitle().ToChar());
            dump.AppendString(line);
            dump.AppendChars(kDumpMessageHeader, sizeof(kDumpMessageHeader) - 1);
            line.Sprintf("\t%s\n\n", advice->GetTextMessage().ToChar());
            dump.AppendString(line);
            return true;
        }
        return true;
    }
    return true;
}

// The advice became current: no blocking day, the timeout runs from the current day, the current flag is set and the
// link handling flag cleared, then the change is notified (and the advisor system is told about a command advice).
void cSC4Advisor::on_advice_activation(cAdviceInfo& info) {
    cISC4Advice* advice = info.advice.get();
    info.nextDay = 0;
    info.expiryDay = dayNumber_ + advice->GetTimeOutInDays();
    advice->SetFlags(kAdviceFlagCurrent, true);
    advice->SetFlags(kAdviceFlagNoLinks, false);
    Notify(g_host->Messages(), advice, kAdviceNotificationActivated, 0, true);
    const int commandId = advice->GetCommandID();
    if (commandId != 0) Notify(g_host->Messages(), static_cast<cRZUnknown*>(this), kAdvisorNotificationEvent, commandId, true);
}

// The advice is no longer current: it may trigger again after its frequency (never when it is a "once" advice), its
// timeout is cleared and the change is notified.
void cSC4Advisor::on_advice_deactivation(cAdviceInfo& info) {
    cISC4Advice* advice = info.advice.get();
    advice->SetFlags(kAdviceFlagForceTrigger | kAdviceFlagNoLinks | kAdviceFlagCurrent, false);
    const bool once = advice->CheckFlags(kAdviceFlagOnce);
    info.nextDay = once ? kOnceForever : dayNumber_ + advice->GetFrequencyInDays();
    info.expiryDay = 0;
    advice->SetEventSubject(nullptr);
    Notify(g_host->Messages(), advice, kAdviceNotificationDeactivated, once ? 1 : 0, false);
}

// One day passed: a fraction of the advice list is checked for a due trigger, the current advices are walked for
// expiring / retired ones, and the pending events are processed.
void cSC4Advisor::simulate_day() {
    const int count = static_cast<int>(advices_.size());
    if (count == 0) return;
    const int checks = static_cast<int>(static_cast<float>(count) * 0.033333335f + 1.0f);
    if (checks != 0) {
        int remaining = checks;
        do {
            if (count <= iterateIndex_) iterateIndex_ = 0;
            --remaining;
            cAdviceInfo& info = advices_[static_cast<std::size_t>(iterateIndex_)];
            cISC4Advice* advice = info.advice.get();
            if (!advice->CheckFlags(kAdviceFlagCurrent) && !advice->CheckFlags(kAdviceFlagEvent) &&
                info.nextDay <= dayNumber_ && info.lastMonth < month_) {
                info.lastMonth = month_;
                if (advice->GetTrigger()) {
                    currentAdvices_[info.advice] = iterateIndex_;
                    on_advice_activation(info);
                }
            }
            iterateIndex_ = iterateIndex_ + 1;
        } while (remaining != 0);
    }

    AdviceMap::iterator it = currentAdvices_.begin();
    while (it != currentAdvices_.end()) {
        AdviceMap::iterator current = it++;
        cAdviceInfo& info = advices_[static_cast<std::size_t>(current->second)];
        cISC4Advice* advice = info.advice.get();
        bool retire = false;
        if (dayNumber_ < info.expiryDay || advice->CheckFlags(kAdviceFlagNoTimeout)) {
            // still inside its time: keep it only while it is persistent or its trigger still fires
            if (advice->CheckFlags(kAdviceFlagNoLinks) ||
                (!advice->CheckFlags(kAdviceFlagPersist) && !advice->GetTrigger()))
                retire = true;
            else if (advice->CheckFlags(kAdviceFlagTextDirty)) {
                advice->UpdateText();
                Notify(g_host->Messages(), advice, kAdviceNotificationTextDirty, 0, false);
            }
        } else {
            retire = true;
        }
        if (retire) {
            currentAdvices_.erase(current);
            on_advice_deactivation(info);
        }
    }
    simulate_day_events();
}

// The pending events (PushEvent) activate every event advice of the last queued event id. The queue is popped at the
// front, so the event read and the one removed differ once more than one event is queued (kept quirk).
void cSC4Advisor::simulate_day_events() {
    int remaining = static_cast<int>(events_.size());
    while (remaining != 0) {
        --remaining;
        if (!events_.empty()) {
            const int eventId = events_.back().first;
            const std::map<int, int>::const_iterator first = eventAdvices_.lower_bound(eventId);
            const std::map<int, int>::const_iterator last = eventAdvices_.upper_bound(eventId);
            for (std::map<int, int>::const_iterator it = first; it != last; ++it) {
                if (it->first != eventId) continue;
                const std::size_t index = static_cast<std::size_t>(it->second);
                cAdviceInfo& info = advices_[index];
                cISC4Advice* advice = info.advice.get();
                cISC4AdviceSubject* subject = events_.back().second.get();
                if (!advice->CheckFlags(kAdviceFlagCurrent)) {
                    info.lastMonth = month_;
                    advice->SetEventSubject(subject);
                    if (advice->GetTrigger()) {
                        currentAdvices_[info.advice] = static_cast<int>(index);
                        on_advice_activation(info);
                    }
                } else {
                    advice->SetFlags(kAdviceFlagTextDirty, true);
                    if (advice->GetTrigger()) advice->SetEventSubject(subject);
                }
            }
            events_.pop_front();
        }
    }
}

void cSC4Advisor::simulate_month() {}

// Queues an event for the next simulate_day_events. The subject is held by reference (the original adds it twice and
// releases it twice while building the pair and its copy).
bool cSC4Advisor::PushEvent(int eventId, cISC4AdviceSubject* subject) {
    events_.push_back(std::make_pair(eventId, cRZAutoRefCount<cISC4AdviceSubject>(subject)));
    return true;
}

bool cSC4Advisor::CommandWouldTrigger(std::uint32_t commandId) {
    for (std::size_t n = advices_.size(); n != 0; --n) {
        cISC4Advice* advice = advices_[n - 1].advice.get();
        if (!advice->CheckFlags(kAdviceFlagEvent) && static_cast<std::uint32_t>(advice->GetCommandID()) == commandId &&
            advice->GetTrigger())
            return true;
    }
    return false;
}

// Removes an advice from the current list (and brings its timeout bookkeeping up to date).
bool cSC4Advisor::RetireAdvice(cISC4Advice* advice) {
    if (!advice || !advice->CheckFlags(kAdviceFlagCurrent)) return false;
    AdviceMap::iterator it = currentAdvices_.find(AdviceRef(advice));
    if (it == currentAdvices_.end()) return false;   // the original would use the last visited node's index
    const std::size_t index = static_cast<std::size_t>(it->second);
    currentAdvices_.erase(it);
    on_advice_deactivation(advices_[index]);
    return true;
}

}  // namespace sc4

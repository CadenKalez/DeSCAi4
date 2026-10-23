#include "sc4/Advice.h"

#include <cstring>
#include <string>

#include "gz/MiscFunctions.h"
#include "rz/StringConversion.h"
#include "sc4/DBSegment.h"
#include "sc4/TablePropertyReader.h"

namespace sc4 {

namespace {
IAdviceHost* g_host = nullptr;
int g_nextSessionId = 0;   // cSC4Advice()::sNewSessionID
constexpr std::uint16_t kAdviceStreamVersion = 4;
constexpr std::uint32_t kEventSubjectIid = 0x6a26d050;
const char kMissingTextPrefix[] = "***_NOT_IN_TEXT_DATABASE_*** ";
}  // namespace

IAdviceHost* AdviceHost() { return g_host; }
void SetAdviceHost(IAdviceHost* host) { g_host = host; }

cSC4Advice::cSC4Advice() { sessionId_ = ++g_nextSessionId; }

cSC4Advice::~cSC4Advice() {
    if (eventSubject_) eventSubject_->Release();
}

bool cSC4Advice::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSerializable) {
        *out = static_cast<cIGZSerializable*>(this);
    } else if (iid == kGZIID_cISC4Advice) {
        *out = static_cast<cISC4Advice*>(this);
    } else {
        return cRZUnknown::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

bool cSC4Advice::Init(int adviceId) {
    adviceId_ = adviceId;   // stored even when the row turns out to be unusable
    if (adviceId == 0) return false;
    IAdviceHost* host = g_host;
    IScriptLua* lua = host->Lua();
    cTablePropertyReader reader(lua, "advices", adviceId);
    if (!reader.IsValid()) return false;

    const int guid = static_cast<int>(reader.GetNumber("guid"));
    if (guid_ != 0 && guid_ != guid) return false;   // a row with another guid than the one already bound
    guid_ = guid;
    priority_ = static_cast<int>(reader.GetNumber("priority"));
    mood_ = static_cast<int>(reader.GetNumber("mood"));
    frequency_ = static_cast<int>(reader.GetNumber("frequency"));
    timeout_ = static_cast<int>(reader.GetNumber("timeout"));

    if (host->DebugFunctionalityEnabled()) triggerString_.AssignFromCString("_atrg_=(");   // debug builds keep the script text

    // trigger: "_atrg_ = function () return (<expr>); end", the expression defaulting to 0
    cRZString trigger(reader.GetString("trigger"));
    if (trigger.Strlen() == 0) trigger.AssignFromCString("0");
    cRZString script;
    script.Sprintf("%s = function () return (%s); end", "_atrg_", trigger.ToChar());
    if (lua->DoString(script.ToChar()) == 0) {
        lua->GetTop();   // result unused in the original
        lua->GetGlobal("_atrg_");
        if (!lua->IsFunction(-1)) {
            lua->Pop(1);
        } else {
            luaRef_ = lua->Ref(true);
        }
    }
    if (host->DebugFunctionalityEnabled()) triggerString_.AppendString(trigger).AppendChars(")", 1);

    SetFlags(kAdviceFlagOnce, reader.GetNumber("once") != 0);
    SetFlags(kAdviceFlagNewsOnly, reader.GetNumber("news_only") != 0);
    eventId_ = static_cast<int>(reader.GetNumber("event"));
    SetFlags(kAdviceFlagEvent, eventId_ != 0);
    commandId_ = static_cast<int>(reader.GetNumber("command"));
    SetFlags(kAdviceFlagPersist, reader.GetNumber("persist") != 0);
    SetFlags(kAdviceFlagNoTimeout, reader.GetNumber("no_timeout") != 0);
    if (static_cast<std::uint32_t>(reader.GetNumber("class_id")) != GetGZCLSID()) SetFlags(kAdviceFlagCustomClass, true);

    // "effects": a table of global effect ids or a single number
    const int top = lua->GetTop();
    if (lua->GetField("effects", top)) {
        if (!lua->IsTable(-1)) {
            if (lua->IsNumber(-1)) effects_.push_back(static_cast<std::uint32_t>(static_cast<std::int64_t>(lua->ToNumber(-1))));
        } else {
            lua->ReadTableAsList(effects_, -1);
        }
        lua->Pop(1);
    }

    // texts: "message" is read before "title"
    cRZString messageScript(reader.GetString("message"));
    message_.FromScriptText(messageScript.ToChar());
    cRZString titleScript(reader.GetString("title"));
    title_.FromScriptText(titleScript.ToChar());
    SetFlags(kAdviceFlagNoText, messageScript.Strlen() == 0 && titleScript.Strlen() == 0);
    SetFlags(kAdviceFlagTextDirty, true);
    if (CheckFlags(kAdviceFlagCurrent) && !loadedFromSave_) RefreshText();
    loadedFromSave_ = false;
    textFields_.clear();
    if (!CheckFlags(kAdviceFlagNoText)) {
        textFields_.emplace_back("message", &message_);
        textFields_.emplace_back("title", &title_);
    }
    return true;
}

bool cSC4Advice::Clone(cISC4Advice** out) {
    if (!out || adviceId_ == 0) return false;
    *out = nullptr;
    cISC4Advice* clone = g_host->CreateAdvice(GetGZCLSID(), kGZIID_cISC4Advice);
    bool ok = false;
    if (clone && clone->Init(adviceId_)) {
        clone->SetAdvisorID(GetAdvisorID());
        cISC4AdviceSubject* subject = GetEventSubject();
        cISC4AdviceSubject* copy = nullptr;
        if (!subject || subject->Clone(&copy)) {
            clone->SetEventSubject(copy);
            *out = clone;
            clone->AddRef();
            ok = true;
        }
        if (copy) copy->Release();
    }
    if (clone) clone->Release();
    return ok;
}

bool cSC4Advice::Shutdown() {
    if (luaRef_ != -2 && g_host->AdvisorSystemPresent()) {
        if (IScriptLua* lua = g_host->Lua()) lua->Unref(luaRef_);
        luaRef_ = -2;
    }
    return true;
}

// get_raw_trigger: runs the stored trigger function. Boolean results are taken as they are, numbers count as true
// when non-zero; a missing/failed trigger answers false (and, like the original, leaves the Lua stack as it was).
bool cSC4Advice::GetRawTrigger() const {
    IAdviceHost* host = g_host;
    host->BeginScript(const_cast<cSC4Advice*>(this));
    IScriptLua* lua = host->Lua();
    bool result = false;
    if (luaRef_ != -2) {
        lua->GetRef(luaRef_);
        if (lua->IsFunction(-1) && lua->CallProtected(0, 1) == 0) {
            if (lua->IsBoolean(-1)) {
                result = lua->ToBoolean(-1);
            } else if (lua->IsNumber(-1)) {
                result = lua->ToNumber(-1) != 0.0;
            }
            lua->Pop(1);
        }
        // else: the original formats "cSC4Advice::GetTrigger() -- error while executing a trigger script ..." and drops it
    }
    host->EndScript();
    return result;
}

bool cSC4Advice::GetTrigger() const {
    if (CheckFlags(kAdviceFlagForceTrigger)) return true;
    if (!GetRawTrigger()) return false;
    return !CheckFlags(kAdviceFlagNoText);
}

bool cSC4Advice::SetFlags(int flags, bool on) {
    const std::uint32_t before = flags_;
    const std::uint32_t mask = static_cast<std::uint32_t>(flags);
    bool notify = false;
    if (on) {
        flags_ = before | mask;
        if (mask & kAdviceFlagCurrent) {
            if (CheckFlags(kAdviceFlagTextDirty)) RefreshText();
            notify = (before & kAdviceFlagCurrent) == 0;
        }
    } else {
        flags_ = before & ~mask;
        if (mask & kAdviceFlagCurrent) {
            SetFlags(kAdviceFlagTextDirty, true);
            notify = (before & kAdviceFlagCurrent) != 0;
        }
    }
    if (notify) NotifyGlobalEffectTriggerChanged();
    if (mask & kAdviceFlagShowTextKeys) SetFlags(kAdviceFlagTextDirty, true);
    return true;
}

bool cSC4Advice::CheckFlags(int flags) const {
    const std::uint32_t mask = static_cast<std::uint32_t>(flags);
    return (flags_ & mask) == mask;
}

std::uint32_t cSC4Advice::GetTextTitleGUID() const {
    cGZPersistResourceKey key;
    title_.GetKey(key);
    return key.instance;
}

std::uint32_t cSC4Advice::GetTextMessageGUID() const {
    cGZPersistResourceKey key;
    message_.GetKey(key);
    return key.instance;
}

// Only runs the link as a script when the advice is current and links are not disabled (flag 0x8).
bool cSC4Advice::DoLink(const cIGZString& link) {
    if (!CheckFlags(kAdviceFlagNoLinks) && CheckFlags(kAdviceFlagCurrent)) {
        IAdviceHost* host = g_host;
        host->BeginScript(this);
        IScriptLua* lua = host->Lua();
        const char* text = link.ToChar();
        lua->DoBuffer(text, std::strlen(text), text);
        host->EndScript();
    }
    return true;
}

bool cSC4Advice::SetEventSubject(cISC4AdviceSubject* subject) {
    SetFlags(kAdviceFlagTextDirty, true);
    cISC4AdviceSubject* old = eventSubject_;
    if (old != subject) {
        if (subject) subject->AddRef();
        eventSubject_ = subject;
        if (old) old->Release();
    }
    return true;
}

// Switches the registered global effects on while the advice is current and off otherwise.
void cSC4Advice::NotifyGlobalEffectTriggerChanged() {
    IAdviceHost* host = g_host;
    if (effects_.empty() || !host->HasView3D()) return;
    const bool current = CheckFlags(kAdviceFlagCurrent);
    for (std::uint32_t effect : effects_) {
        if (current) host->GlobalEffectOn(effect); else host->GlobalEffectOff(effect);
    }
}

bool cSC4Advice::Write(cIGZOStream& stream) const {
    stream.SetUint16(kAdviceStreamVersion);
    stream.SetSint32(adviceId_);
    stream.SetSint32(guid_);
    stream.SetSint32(static_cast<std::int32_t>(flags_));
    stream.SetSint32(advisorId_);
    stream.SetSint32(priority_);
    stream.SetSint32(frequency_);
    stream.SetSint32(timeout_);
    stream.SetSint32(mood_);
    stream.SetSint32(static_cast<std::int32_t>(effects_.size()));
    for (std::uint32_t effect : effects_) stream.SetUint32(effect);
    SC4WriteGZSerializable(stream, eventSubject_);
    return !stream.GetError();
}

bool cSC4Advice::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    stream.GetSint32(adviceId_);
    stream.GetSint32(guid_);
    std::int32_t flags = 0;
    stream.GetSint32(flags);
    flags_ = static_cast<std::uint32_t>(flags);
    stream.GetSint32(advisorId_);
    stream.GetSint32(priority_);
    stream.GetSint32(frequency_);
    stream.GetSint32(timeout_);
    stream.GetSint32(mood_);
    if (version > 3) {
        std::int32_t count = 0;
        stream.GetSint32(count);
        for (; count != 0; --count) {   // appends: the list is not cleared first (kept)
            std::uint32_t effect = 0;
            stream.GetUint32(effect);
            effects_.push_back(effect);
        }
    }
    if (eventSubject_) {
        cISC4AdviceSubject* old = eventSubject_;
        eventSubject_ = nullptr;
        old->Release();
    }
    SC4ReadGZSerializable(stream, kEventSubjectIid, reinterpret_cast<void**>(&eventSubject_), false);
    SetFlags(kAdviceFlagTextDirty, true);
    loadedFromSave_ = !stream.GetError();
    return loadedFromSave_;
}

// refresh_text: re-reads every registered text field ("message", "title") from the script row and loads it from the
// string resources; a text that is not in the database is shown as "***_NOT_IN_TEXT_DATABASE_*** <script text>".
bool cSC4Advice::RefreshText() {
    if (textFields_.empty()) return true;
    IAdviceHost* host = g_host;
    host->BeginScript(this);
    IScriptLua* lua = host->Lua();
    {
        cTablePropertyReader reader(lua, "advices", adviceId_);
        if (reader.IsValid()) {
            for (const auto& field : textFields_) {
                cADVString* text = field.second;
                cRZString raw(reader.GetString(field.first));
                gConvertURLFromEndodedForm(raw, raw);
                text->FromScriptText(raw.ToChar());
                if (!text->Load(false, false)) {
                    std::string combined = kMissingTextPrefix;
                    combined += raw.ToChar();
                    raw.AssignFromChars(combined.data(), static_cast<std::uint32_t>(combined.size()));
                    ConvertStringEncoding(raw, 1252, raw);
                    text->Copy(raw);
                }
                text->ConvertTokens(nullptr, true);
                if (CheckFlags(kAdviceFlagShowTextKeys)) {
                    cGZPersistResourceKey key;
                    text->GetKey(key);
                    if (key.instance != 0) {
                        const std::string current = text->ToChar();
                        text->Sprintf("=text@%08X= %s =adv@%08X=", key.instance, current.c_str(),
                                      static_cast<std::uint32_t>(GetGUID()));
                    }
                }
            }
        }
    }
    host->EndScript();
    SetFlags(kAdviceFlagTextDirty, false);
    return true;
}

}  // namespace sc4

// cSC4Advice — one entry of the advisor's script table "advices[<advice id>]" (class id 0x8a09f5f4, interface
// cISC4Advice 0x8a09f5f5; 39 functions at 0x10043ce9e..0x10043ea48). Base of the advice flavours (neighbour deal,
// reward, mysim, mysim balloon, city situation ...). Original layout (size 0x140): +0x00 cISC4Advice vptr,
// +0x08 cRZUnknown (refcount; its vptr is the secondary vtable), +0x18 cIGZSerializable vptr,
//   +0x20 vector<pair<const char*, cADVString*>> text fields to refresh  +0x38 an unused cRZString
//   +0x58 int 0  +0x60 cADVString title  +0x90 cADVString message  +0xc0 flags  +0xc4 advisor id  +0xc8 priority
//   +0xcc frequency (days)  +0xd0 timeout (days)  +0xd4 mood  +0xd8 advice id (row of the table)  +0xdc guid
//   +0xe0 trigger string (cRZString)  +0x108 byte "loaded from a save"  +0x10c event id  +0x110 event subject
//   +0x118 session id  +0x11c command id  +0x120 Lua registry reference of the trigger function (-2 = none)
//   +0x128 std::list<uint32_t> global effects (size at +0x138).
//
// Flags (SetFlags / CheckFlags): 0x1 news only, 0x2 current (shown), 0x4 once, 0x8 no link handling, 0x10 text
// needs a refresh, 0x40 event driven, 0x80 no text, 0x100 persistent, 0x200 show text keys, 0x400 trigger forced
// true, 0x800 the table's class_id differs from this class, 0x8000 no timeout. Defaults: flags 0x10, priority 50,
// frequency 30, timeout 30, mood 3, luaRef -2, title "<UNDEFINED> title", message "<UNDEFINED> message ".
//
// Quirks kept: the constructor leaves the event id (+0x10c) uninitialised (we use 0); Init sets the table row id
// before checking it and never releases an earlier trigger reference; get_raw_trigger leaves the Lua stack
// unbalanced when the trigger is not a function or its call fails; the bad-script error text it formats is never
// shown; refresh_text/GetString failures substitute "***_NOT_IN_TEXT_DATABASE_*** <script text>" converted to
// code page 1252; Read appends the effect list without clearing it first; Clone shares the event subject by
// cloning it.
// Everything the original reaches through globals goes through IAdviceHost.
#pragma once
#include <cstdint>
#include <list>
#include <utility>
#include <vector>

#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZString.h"
#include "rz/cRZString.h"
#include "rz/cRZUnknown.h"
#include "sc4/AdviceSubject.h"
#include "sc4/SLString.h"
#include "sc4/ScriptLua.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4Advice = 0x8a09f5f4;   // GetGZCLSID @ 0x10043ea3c
constexpr std::uint32_t kGZIID_cISC4Advice = 0x8a09f5f5;

enum AdviceFlags : std::uint32_t {
    kAdviceFlagNewsOnly = 0x1, kAdviceFlagCurrent = 0x2, kAdviceFlagOnce = 0x4, kAdviceFlagNoLinks = 0x8,
    kAdviceFlagTextDirty = 0x10, kAdviceFlagEvent = 0x40, kAdviceFlagNoText = 0x80, kAdviceFlagPersist = 0x100,
    kAdviceFlagShowTextKeys = 0x200, kAdviceFlagForceTrigger = 0x400, kAdviceFlagCustomClass = 0x800,
    kAdviceFlagNoTimeout = 0x8000
};

class cISC4Advice;
class cSC4Advice;

// What cSC4Advice needs from the running game.
class IAdviceHost {
public:
    virtual ~IAdviceHost() = default;
    virtual bool AdvisorSystemPresent() = 0;                 // cSC4AdvisorSystem::spInstance != 0 (Shutdown only)
    virtual IScriptLua* Lua() = 0;                           // spInstance vslot 0x50
    virtual void BeginScript(cSC4Advice* advice) = 0;        // spInstance vslot 0x90 (makes `advice` the current script owner)
    virtual void EndScript() = 0;                            // spInstance vslot 0x98
    virtual bool DebugFunctionalityEnabled() = 0;            // SL::spSC4 vslot 0xa8 (GetDebugFunctionalityEnabled)
    // GZCOM()->vslot 0x18 CreateInstance(clsid, iid): answers a new reference (or null).
    virtual cISC4Advice* CreateAdvice(std::uint32_t clsid, std::uint32_t iid) = 0;
    virtual bool HasView3D() = 0;                            // SL::spView3D != 0
    virtual void GlobalEffectOn(std::uint32_t effectId) = 0; // spView3D vslot 0x178
    virtual void GlobalEffectOff(std::uint32_t effectId) = 0;// spView3D vslot 0x180
};
IAdviceHost* AdviceHost();
void SetAdviceHost(IAdviceHost* host);

// cISC4Advice (vtable 0x100aaece0; the slot offsets are the original vslots).
class cISC4Advice : public cIGZUnknown {
public:
    virtual bool Init(int adviceId) = 0;                                  // 0x18
    virtual bool Shutdown() = 0;                                          // 0x20
    virtual bool Clone(cISC4Advice** out) = 0;                            // 0x28
    virtual int GetMood() const = 0;                                      // 0x30
    virtual int GetPriority() const = 0;                                  // 0x38
    virtual bool GetTrigger() const = 0;                                  // 0x40
    virtual cIGZString& GetTriggerString() = 0;                           // 0x48
    virtual int GetGUID() const = 0;                                      // 0x50
    virtual int GetAdvisorID() const = 0;                                 // 0x58
    virtual void SetAdvisorID(int id) = 0;                                // 0x60
    virtual int GetSessionID() const = 0;                                 // 0x68
    virtual bool SetFlags(int flags, bool on) = 0;                        // 0x70
    virtual bool CheckFlags(int flags) const = 0;                         // 0x78
    virtual int GetFrequencyInDays() const = 0;                           // 0x80
    virtual int GetTimeOutInDays() const = 0;                             // 0x88
    virtual const cIGZString& GetTextTitle() const = 0;                   // 0x90
    virtual std::uint32_t GetTextTitleGUID() const = 0;                   // 0x98
    virtual const cIGZString& GetTextMessage() const = 0;                 // 0xa0
    virtual std::uint32_t GetTextMessageGUID() const = 0;                 // 0xa8
    virtual bool DoLink(const cIGZString& link) = 0;                      // 0xb0
    virtual void NotifyGlobalEffectTriggerChanged() = 0;                  // 0xb8
    virtual bool SetEventSubject(cISC4AdviceSubject* subject) = 0;        // 0xc0
    virtual cISC4AdviceSubject* GetEventSubject() = 0;                    // 0xc8
    virtual int GetEventID() = 0;                                         // 0xd0
    virtual int GetCommandID() = 0;                                       // 0xd8
    virtual bool UpdateText() = 0;                                        // 0xe0
    virtual bool IsCurrent() const = 0;                                   // 0xf8
};

class cSC4Advice : public cRZUnknown, public cIGZSerializable, public cISC4Advice {
public:
    cSC4Advice();                                                         // @ 0x10043ce9e
    ~cSC4Advice() override;                                               // @ 0x10043d04c (deleting 0x10043d1a0, thunks 0x10043d192/0x10043d1ce)

    // cIGZUnknown (all three views share the one refcount)
    bool QueryInterface(std::uint32_t iid, void** out) override;          // @ 0x10043e140 (thunks 0x10043e174, 0x10043e182)
    std::uint32_t AddRef() override { return cRZUnknown::AddRef(); }      // @ 0x10043e190 (thunks 0x10043e19e, 0x10043e1a8)
    std::uint32_t Release() override { return cRZUnknown::Release(); }    // @ 0x10043e1b6 (thunks 0x10043e1c4, 0x10043e1ce)

    // cISC4Advice
    bool Init(int adviceId) override;                                     // @ 0x10043d1dc
    bool Shutdown() override;                                             // @ 0x10043dcdc
    bool Clone(cISC4Advice** out) override;                               // @ 0x10043db70
    int GetMood() const override { return mood_; }                        // @ 0x10043dd2e
    int GetPriority() const override { return priority_; }                // @ 0x10043dd3a
    bool GetTrigger() const override;                                     // @ 0x10043deb6
    cIGZString& GetTriggerString() override { return triggerString_; }    // @ 0x10043dd46
    int GetGUID() const override { return guid_; }                        // @ 0x10043defa
    int GetAdvisorID() const override { return advisorId_; }              // @ 0x10043df06
    void SetAdvisorID(int id) override { advisorId_ = id; }               // @ 0x10043df12
    int GetSessionID() const override { return sessionId_; }              // @ 0x10043df1e
    bool SetFlags(int flags, bool on) override;                           // @ 0x10043df2a
    bool CheckFlags(int flags) const override;                            // @ 0x10043dfe6
    int GetFrequencyInDays() const override { return frequency_; }        // @ 0x10043e00e
    int GetTimeOutInDays() const override { return timeout_; }            // @ 0x10043e01a
    const cIGZString& GetTextTitle() const override { return title_; }    // @ 0x10043e026
    std::uint32_t GetTextTitleGUID() const override;                      // @ 0x10043e030
    const cIGZString& GetTextMessage() const override { return message_; }// @ 0x10043e062
    std::uint32_t GetTextMessageGUID() const override;                    // @ 0x10043e070
    bool DoLink(const cIGZString& link) override;                         // @ 0x10043e0a4
    void NotifyGlobalEffectTriggerChanged() override;                     // @ 0x10043e9b6
    bool SetEventSubject(cISC4AdviceSubject* subject) override;           // @ 0x10043e934
    cISC4AdviceSubject* GetEventSubject() override { return eventSubject_; }   // @ 0x10043e990
    int GetEventID() override { return eventId_; }                        // @ 0x10043e99e
    int GetCommandID() override { return commandId_; }                    // @ 0x10043e9aa
    bool UpdateText() override { return RefreshText(); }                  // @ 0x10043ea2e (tail-calls vslot 0x118)
    bool IsCurrent() const override { return CheckFlags(kAdviceFlagCurrent); }   // @ 0x10043dffa

    // cIGZSerializable
    bool Write(cIGZOStream& stream) const override;                       // @ 0x10043e1dc (thunk 0x10043e2ec)
    bool Read(cIGZIStream& stream) override;                              // @ 0x10043e2fa (thunk 0x10043e480)
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4Advice; }      // @ 0x10043ea3c (thunk 0x10043ea48)

    // vslot 0x118 (virtual so that the flavours can override it)
    virtual bool RefreshText();                                           // @ 0x10043e48e

    const std::list<std::uint32_t>& GlobalEffects() const { return effects_; }
    int AdviceId() const { return adviceId_; }                            // +0xd8
    int LuaRef() const { return luaRef_; }                                // +0x120
    bool LoadedFromSave() const { return loadedFromSave_; }               // byte at +0x108

protected:
    bool GetRawTrigger() const;                                           // @ 0x10043dd54 (get_raw_trigger)

    // Fields are protected: the advice flavours (subclasses) extend the record.
    std::vector<std::pair<const char*, cADVString*>> textFields_;   // +0x20
    cRZString spare_;                                                // +0x38 (never used by the recovered code)
    int field58_ = 0;                                                // +0x58
    cADVString title_{"<UNDEFINED> title"};                          // +0x60
    cADVString message_{"<UNDEFINED> message "};                     // +0x90
    std::uint32_t flags_ = kAdviceFlagTextDirty;                     // +0xc0
    int advisorId_ = 0;                                              // +0xc4
    int priority_ = 50;                                              // +0xc8
    int frequency_ = 30;                                             // +0xcc
    int timeout_ = 30;                                               // +0xd0
    int mood_ = 3;                                                   // +0xd4
    int adviceId_ = 0;                                               // +0xd8
    int guid_ = 0;                                                   // +0xdc
    cRZString triggerString_;                                        // +0xe0
    bool loadedFromSave_ = false;                                    // +0x108
    int eventId_ = 0;                                                // +0x10c (uninitialised in the original)
    cISC4AdviceSubject* eventSubject_ = nullptr;                     // +0x110
    int sessionId_ = 0;                                              // +0x118
    int commandId_ = 0;                                              // +0x11c
    int luaRef_ = -2;                                                // +0x120
    std::list<std::uint32_t> effects_;                               // +0x128
};

}  // namespace sc4

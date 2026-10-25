// cSC4Advisor — one advisor ("the police chief", "the transport advisor" ...) and the base class of the advisor
// windows and of cSC4MySim (35 functions at 0x100441942..0x1004452c4, class id 0xeac937b4, size 0x148). It reads its
// own row "advisors[<advisor type>]", creates one advice per matching row of the "advices" table, keeps the ones that
// are currently shown, and turns the advisor system's messages into advice activation/deactivation notifications.
//
// Original layout (the four vtable groups of the multiple inheritance land on these offsets):
//   +0x00 cRZUnknown (cIGZUnknown vptr, refcount at +0x08)  +0x10 cISC4Advisor vptr (iid 0xca09f544)
//   +0x18 cIGZSerializable vptr (0xe4fda3d4)                +0x20 cIGZMessageTarget2 vptr (0x452294aa)
//   +0x28 int  the "advisors" table row (Init's argument)
//   +0x30 cADVString name ("caption", cut at the first '|')       +0x60 cADVString personal name ("name")
//   +0x90 int  month counter (Simulate(1) increments it)          +0x94 int simulation day number (Simulate(0))
//   +0x98 int  advisor id ("id")                                  +0x9c int round-robin index into the advice list
//   +0xa0 int  advice type ("advice_type"; matches the advices' "type")
//   +0xa8 std::map<int,int>                 event id -> index of the advice in +0xd8
//   +0xc0 std::map<cRZAutoRefCount<cISC4Advice>,int>   the advices that are currently shown
//   +0xd8 std::vector<cAdviceInfo>          every advice of this advisor
//   +0xf0 int  selected head animation       +0xf4 bool which head animation list (see load_heads)
//   +0xf8 std::vector<int> head animation rows with "advisor_gender" 1   +0x110 all the other genders
//   +0x128 std::list<std::pair<int,cRZAutoRefCount<cISC4AdviceSubject>>> pending events
//   +0x140 bool subscribed to the four message types of the advisor system
//
// cAdviceInfo (0x18 bytes): +0x00 cRZAutoRefCount<cISC4Advice>, +0x08 int day it may trigger again, +0x0c int day its
// timeout runs out, +0x10 int month it last triggered.
//
// The original vtable groups are 0x00 (cRZUnknown + the class's own virtuals Init 0x38, Shutdown 0x40 .. RetireAdvice
// 0xc0, DoMessage 0xc8, Write 0xd0, Read 0xd8, GetGZCLSID 0xe0, simulate_day 0xe8, simulate_day_events 0xf0,
// simulate_month 0xf8), 0x10 (cISC4Advisor: QueryInterface/AddRef/Release, Init 0x18 .. RetireAdvice 0xa0), 0x18
// (cIGZSerializable: Write/Read/GetGZCLSID) and 0x20 (cIGZMessageTarget2: DoMessage). The port keeps the object layout
// (the test asserts the +0x10/+0x18/+0x20 subobjects and the member offsets) and the contents of the three secondary
// groups. The declarations are ordered so that the primary group slots match the binary too (DoMessage 0xc8 before the
// serializable Write/Read/GetGZCLSID), which keeps subclasses' simulated overrides on the original slots.
//
// Quirks kept:
//  * Simulate(1) increments the month counter and calls simulate_month, Simulate(0) refreshes the day number from the
//    simulator date and calls simulate_day (the base simulate_month is empty).
//  * Init stores the advisor id before checking it against the one already bound; a mismatch fails the whole Init after
//    the row was read. The "advices" table is probed *after* the advisor row was loaded and the loaded notification was
//    posted, so a missing table still leaves the row loaded.
//  * Init re-initialises every advice it already has: an advice whose guid is no longer in the table, whose Init fails
//    or whose class_id differs from its own class is retired, shut down and removed. While doing so it erases each
//    matched row from the guid->row map, so only the advices that still have to be created remain.
//  * The message 0x4a91dd6e activates advices only when their trigger text does not contain "=(0)" (the debug trigger
//    text of a trigger that is literally 0); the advice's title/message guid may also match the message's data word.
//  * The advices' 0x200 flag is set from the data word of message 0xea917750 ("show text keys"), not from a boolean
//    message type.
//  * simulate_day_events reads the event id of the *last* queued event but pops the *first* one; Write walks the event
//    list from the back to the front, so a save/load round trip reverses the pending events.
//  * The sNotify<cISC4Advice> calls of on_advice_activation and of the reset path pass whatever value the previous call
//    left in ecx; the port passes 0 (the value is not read anywhere recovered).
//  * The debug dump of message 0x4a92ecef builds a text and throws it away (its output call is compiled out).
//  * GetHeadAnimID(animIndex) reads the numeric field <animIndex> of the "advisor_heads" row, not an id column.
//  * Read appends to the advice and event lists instead of clearing them first.
//  * Shutdown does not unregister an event id of 0 (the original compares each key with the previous one and skips the
//    match); RetireAdvice answers false when a current advice is missing from the current list, where the original would
//    use the last tree node it visited.
// Everything the original reaches through globals goes through IAdvisorHost.

#pragma once
#include "sc4/CommonIIDs.h"
#include <cstdint>
#include <list>
#include <map>
#include <utility>
#include <vector>

#include "gz/cIGZMessage2.h"
#include "gz/cIGZIStream.h"
#include "gz/cIGZOStream.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZString.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZString.h"
#include "rz/cRZUnknown.h"
#include "sc4/Advice.h"
#include "sc4/AdviceSubject.h"
#include "sc4/SLString.h"
#include "sc4/ScriptLua.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4Advisor = 0xeac937b4;        // GetGZCLSID @ 0x1004452b8
constexpr std::uint32_t kGZIID_cISC4Advisor = 0xca09f544;         // the interface at +0x10

// The advisor messages (the "type" of the cRZMessage2Standard the original's sNotify<> helpers build).
constexpr std::uint32_t kAdviceMessageTriggered = 0xea917750;      // show/hide the advices (data1 = show text keys)
constexpr std::uint32_t kAdviceMessageReset = 0x8a917759;          // drop the current advices, then re-evaluate them
constexpr std::uint32_t kAdviceMessageChanged = 0x4a91dd6e;        // the advice system changed one advice (data1 = id)
constexpr std::uint32_t kAdviceMessageDump = 0x4a92ecef;           // debug dump of one advice (data1 = id)
constexpr std::uint32_t kAdvisorNotificationLoaded = 0x8a09fa68;   // sNotify<cSC4Advisor>: the row was loaded
constexpr std::uint32_t kAdvisorNotificationUnloaded = 0x8a09fa67; // sNotify<cSC4Advisor>: the current list was dropped
constexpr std::uint32_t kAdvisorNotificationEvent = 0x8a09fa6c;    // sNotify<cSC4Advisor>: data1 = the event id
constexpr std::uint32_t kAdviceNotificationActivated = 0x8a09fa64;   // sNotify<cISC4Advice>
constexpr std::uint32_t kAdviceNotificationDeactivated = 0x8a09fa65; // sNotify<cISC4Advice>
constexpr std::uint32_t kAdviceNotificationTextDirty = 0x8a09fa66;   // sNotify<cISC4Advice>

class cISC4Advisor;
class cSC4Advisor;

using AdviceRef = cRZAutoRefCount<cISC4Advice>;
using AdviceMap = std::map<AdviceRef, int>;   // the advice -> index into cSC4Advisor::cAdviceInfo vector
using EventList = std::list<std::pair<int, cRZAutoRefCount<cISC4AdviceSubject>>>;

// What cSC4Advisor needs from the running game.
class IAdvisorHost {
public:
    virtual ~IAdvisorHost() = default;
    virtual IScriptLua* Lua() = 0;                                     // cSC4AdvisorSystem::spInstance vslot 0x50
    virtual void RegisterAdvice(int eventId, cISC4Advisor* advisor) = 0;      // vslot 0x80
    virtual void UnregisterAdvice(int eventId, cISC4Advisor* advisor) = 0;    // vslot 0x88
    virtual cIGZMessageServer2* Messages() = 0;                        // SL::spMS2 (notifications and subscriptions)
    // GZCOM() vslot 0x18 CreateInstance(clsid, iid, out): the clsid comes from the advice row's "class_id".
    virtual bool CreateInstance(std::uint32_t classId, std::uint32_t iid, void** out) = 0;
    // SL::spSimulator vslot 0x30 (cISC4Simulator::GetSimDate) handed the simulation date; the original then reads
    // cIGZDate::DayNumber (vslot 0xd0), so this answers that day number.
    virtual int SimDayNumber() = 0;
    // SC4PostMessage2(type, data1, data2, data3, data4, unknown) @ 0x10003a5ba.
    virtual void PostMessage2(std::uint32_t type, std::uint32_t data1, std::uint32_t data2, std::uint32_t data3,
                             std::uint32_t data4, cIGZUnknown* unknown) = 0;
};
IAdvisorHost* AdvisorHost();
void SetAdvisorHost(IAdvisorHost* host);

// cISC4Advisor (the interface at +0x10; the slot offsets are the original vslots).
class cISC4Advisor : public cIGZUnknown {
public:
    virtual bool Init(int advisorType) = 0;                                    // 0x18
    virtual bool Shutdown() = 0;                                              // 0x20
    virtual int GetID() const = 0;                                            // 0x28
    virtual const cIGZString& GetName() const = 0;                            // 0x30
    virtual void SetName(const cIGZString& name) = 0;                         // 0x38
    virtual cIGZString& GetPersonalName() = 0;                                // 0x40
    virtual void SetPersonalName(const cIGZString& name) = 0;                 // 0x48
    virtual const AdviceMap& GetCurrentAdvices() const = 0;                   // 0x50
    virtual AdviceMap& GetCurrentAdvices() = 0;                               // 0x58
    virtual cISC4Advice* GetAdviceByGUID(int guid) const = 0;                 // 0x60
    virtual cISC4Advice* GetAdviceBySessionID(int sessionId) const = 0;       // 0x68
    virtual bool Simulate(int period) = 0;                                    // 0x70
    virtual int GetHeadAnimCount(bool gender1) = 0;                           // 0x78
    virtual bool SetHeadAnim(bool gender1, int index) = 0;                    // 0x80
    virtual int GetHeadAnimID(int animIndex) = 0;                             // 0x88
    virtual bool PushEvent(int eventId, cISC4AdviceSubject* subject) = 0;     // 0x90
    virtual bool CommandWouldTrigger(std::uint32_t commandId) = 0;            // 0x98
    virtual bool RetireAdvice(cISC4Advice* advice) = 0;                       // 0xa0
};

class cSC4Advisor : public cRZUnknown, public cISC4Advisor, public cIGZSerializable, public cIGZMessageTarget2 {
public:
    struct cAdviceInfo {
        AdviceRef advice;      // +0x00
        int nextDay = 0;       // +0x08 the advice may trigger again from this day on
        int expiryDay = 0;     // +0x0c the advice times out on this day
        int lastMonth = 0;     // +0x10 the month it last triggered
    };

    cSC4Advisor();                                   // @ 0x100441942
    ~cSC4Advisor() override;                         // @ 0x100441ab2 (complete 0x100441bea, deleting 0x100441bf4)

    // cIGZUnknown (all four views share the one reference count)
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10044317e (thunks 0x1004431be/cc/da)
    std::uint32_t AddRef() override;                               // @ 0x1004431e8 (thunks 0x1004431f2/200/20e)
    std::uint32_t Release() override;                              // @ 0x10044321c (thunks 0x100443226/234/242)

    // cISC4Advisor
    bool Init(int advisorType) override;                           // @ 0x100441c22 (thunk 0x100442b34)
    bool Shutdown() override;                                      // @ 0x100442b42 (thunk 0x100442cae)
    int GetID() const override;                                    // @ 0x100442cc0 (thunk 0x100442ccc)
    const cIGZString& GetName() const override;                    // @ 0x100442cd8 (thunk 0x100442ce2)
    void SetName(const cIGZString& name) override;                 // @ 0x100442cec (thunk 0x100442cfa)
    cIGZString& GetPersonalName() override;                        // @ 0x100442d08 (thunk 0x100442d12)
    void SetPersonalName(const cIGZString& name) override;         // @ 0x100442d1c (thunk 0x100442d2a)
    const AdviceMap& GetCurrentAdvices() const override;           // @ 0x100442d38 (thunk 0x100442d46)
    AdviceMap& GetCurrentAdvices() override;                       // @ 0x100442d54 (thunk 0x100442d62)
    cISC4Advice* GetAdviceByGUID(int guid) const override;         // @ 0x100442d70 (thunk 0x100442dc8)
    cISC4Advice* GetAdviceBySessionID(int sessionId) const override;   // @ 0x100442e20 (thunk 0x100442e78)
    bool Simulate(int period) override;                            // @ 0x100442ed0 (thunk 0x100442f2e)
    int GetHeadAnimCount(bool gender1) override;                   // @ 0x100442f40 (thunk 0x100442f66)
    bool SetHeadAnim(bool gender1, int index) override;            // @ 0x100442f8c (thunk 0x100442fe2)
    int GetHeadAnimID(int animIndex) override;                     // @ 0x10044303c (thunk 0x100443170)
    bool PushEvent(int eventId, cISC4AdviceSubject* subject) override;      // @ 0x1004449b2 (thunk 0x100444a68)
    bool CommandWouldTrigger(std::uint32_t commandId) override;    // @ 0x100444a7a (thunk 0x100444b2a)
    bool RetireAdvice(cISC4Advice* advice) override;               // @ 0x100444b38 (thunk 0x100444bf0)

    // cIGZMessageTarget2 (declared before the serializable members so that the primary vtable keeps the original's
    // slot numbers: DoMessage 0xc8, Write 0xd0, Read 0xd8, GetGZCLSID 0xe0)
    bool DoMessage(cIGZMessage2* message) override;                // @ 0x100443250 (thunk 0x100443e92)

    // cIGZSerializable
    bool Write(cIGZOStream& stream) const override;                // @ 0x100443eaa (thunk 0x10044405c)
    bool Read(cIGZIStream& stream) override;                       // @ 0x10044406a (thunk 0x1004444b2)
    std::uint32_t GetGZCLSID() const override;                     // @ 0x1004452b8 (thunk 0x1004452c4)

    // vslots 0xe8 / 0xf0 / 0xf8 (virtual so that the advisor flavours can override them)
    virtual void simulate_day();                                   // @ 0x100444538
    virtual void simulate_day_events();                            // @ 0x100444798
    virtual void simulate_month();                                 // @ 0x1004449ac

    bool load_heads();                                             // @ 0x10044291e
    void on_advice_activation(cAdviceInfo& info);                  // @ 0x100443cf0
    void on_advice_deactivation(cAdviceInfo& info);                // @ 0x1004444c0

    const std::vector<cAdviceInfo>& AdviceInfos() const { return advices_; }   // +0xd8 (for the subclasses / tests)
    int TableRow() const { return tableRow_; }                     // +0x28
    int AdviceType() const { return adviceType_; }                 // +0xa0
    int Month() const { return month_; }                           // +0x90
    int DayNumber() const { return dayNumber_; }                   // +0x94
    int IterateIndex() const { return iterateIndex_; }             // +0x9c
    int HeadAnim() const { return headAnim_; }                     // +0xf0
    bool HeadAnimGender1() const { return headAnimList_; }         // +0xf4
    const EventList& Events() const { return events_; }            // +0x128
    bool Subscribed() const { return subscribed_; }                // +0x140
    // +0xa8 (the event -> advice map) is private; cSC4MySim::init_balloons remaps it onto its balloon list and erases
    // the events that have no balloon advice, so the subclass may read and erase single entries.
    // subclass access to +0xc0 / +0xd8 (cSC4AdvisorCitySituations rebuilds the active set itself)
    AdviceMap& MutableCurrentAdvices() { return currentAdvices_; }
    std::vector<cAdviceInfo>& MutableAdvices() { return advices_; }
    const std::map<int, int>& EventAdvices() const { return eventAdvices_; }
    void EraseEventAdvice(int eventId) { eventAdvices_.erase(eventId); }

private:
    int tableRow_ = 0;              // +0x28
    cADVString name_{""};           // +0x30 (the original default-constructs a cSLString; the key is overwritten by Init)
    cADVString personalName_{""};   // +0x60
    int month_ = 0;                 // +0x90
    int dayNumber_ = 0;             // +0x94
    int id_ = 0;                    // +0x98
    int iterateIndex_ = 0;          // +0x9c
    int adviceType_ = 0;            // +0xa0
    std::map<int, int> eventAdvices_;    // +0xa8 event id -> advice index
    AdviceMap currentAdvices_;      // +0xc0
    std::vector<cAdviceInfo> advices_;   // +0xd8
    int headAnim_ = -1;             // +0xf0
    bool headAnimList_ = true;      // +0xf4 true: headAnimations1_ (the original constructor stores 1)
    std::vector<int> headAnimations1_;   // +0xf8 rows with "advisor_gender" == 1
    std::vector<int> headAnimations0_;   // +0x110 rows with any other gender
    EventList events_;              // +0x128
    bool subscribed_ = false;       // +0x140
};

}  // namespace sc4

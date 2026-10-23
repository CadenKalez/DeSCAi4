// cSC4MySimBalloonAdvice — advice for the speech balloon over a MySim (class id 0x6b70efad, interfaces
// cISC4MySimBalloonAdvice 0x6b70e6d9 at +0x140 and 0x2b722c71 answered by the primary pointer; 51 functions at
// 0x1006aa0b4..0x1006aac74). Adds to cSC4Advice: +0x148 vector<cSLBuffer> of up to nine balloon icons ("balloon_icon1"
// .. "balloon_icon9", numbers naming bitmap instances of group 0xab7e5421), +0x160 the balloon text ("balloon_message"
// script text, refreshed together with the advice texts), +0x190 text id (never set), +0x194 status flags,
// +0x198/+0x19c the male / female sound ids ("balloon_sound_m" / "_f", both default to "balloon_sound"), +0x1a0 the
// sound chance ("balloon_sound_chance").
//
// Status flags: bit 2 = shown (setting it refreshes the text when the base text is dirty), bit 4 = muted/blocked.
// GetTrigger: false while status bit 4 is on; otherwise it needs status bit 2 or the base "current" flag, then asks the
// base class.
// Quirks kept: Init answers true for a row without icons only when the balloon message is non-empty; the icon bitmaps get
// magenta (255,0,255) as colour key; CheckStatusFlags is "any bit" while the base CheckFlags is "all bits".
// Original vslots: AsISC4Advice 0x120/0x128, GetTextBalloon 0x130, GetTextBalloonID 0x138, GetIconBalloonCount 0x140,
// GetIconBalloon 0x148, GetIconBalloonID 0x150, GetSoundID 0x158, GetSoundChance 0x160, SetStatusFlags 0x168,
// CheckStatusFlags 0x170, GetRawTrigger 0x178; the interface at +0x140 repeats them in a different order.
#pragma once
#include <vector>

#include "sc4/Advice.h"
#include "sc4/SLBuffer.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4MySimBalloonAdvice = 0x6b70efad;
constexpr std::uint32_t kGZIID_cISC4MySimBalloonAdvice = 0x6b70e6d9;
constexpr std::uint32_t kGZIID_cSC4MySimBalloonAdviceSelf = 0x2b722c71;
constexpr std::uint32_t kBalloonIconGroup = 0xab7e5421;
constexpr int kBalloonMaxIcons = 9;
constexpr int kBalloonStatusShown = 0x2, kBalloonStatusBlocked = 0x4;

class cISC4MySimBalloonAdvice : public cIGZUnknown {
public:
    virtual cISC4Advice* AsISC4Advice() = 0;                      // 0x18
    virtual const cISC4Advice* AsISC4AdviceConst() const = 0;     // 0x20
    virtual const cIGZString& GetTextBalloon() const = 0;         // 0x28
    virtual int GetTextBalloonID() const = 0;                     // 0x30
    virtual int GetIconBalloonCount() const = 0;                  // 0x38
    virtual cIGZBuffer* GetIconBalloon(int index) const = 0;      // 0x40
    virtual std::uint32_t GetIconBalloonID(int index) const = 0;  // 0x48
    virtual int GetSoundID(bool male) const = 0;                  // 0x50
    virtual int GetSoundChance() const = 0;                       // 0x58
    virtual bool GetRawTrigger() = 0;                             // 0x60
    virtual void SetStatusFlags(int flags, bool on) = 0;          // 0x68
    virtual bool CheckStatusFlags(int flags) const = 0;           // 0x70
};

class cSC4MySimBalloonAdvice : public cSC4Advice, public cISC4MySimBalloonAdvice {
public:
    cSC4MySimBalloonAdvice();                                     // @ 0x1006aa0b4
    ~cSC4MySimBalloonAdvice() override = default;                 // @ 0x1006aa18c (deleting 0x1006aa236, thunks 0x1006aa21e/228/264)

    bool QueryInterface(std::uint32_t iid, void** out) override;  // @ 0x1006aab4e (thunks 0x1006aab82/90/9e)
    std::uint32_t AddRef() override { return cSC4Advice::AddRef(); }     // @ 0x1006aabb0 (thunks 0x1006aabba/c8/d6)
    std::uint32_t Release() override { return cSC4Advice::Release(); }   // @ 0x1006aabe8 (thunks 0x1006aabf2/00/0e)

    bool Init(int adviceId) override;                             // @ 0x1006aa272
    bool UpdateText() override { return cSC4Advice::UpdateText(); }   // @ 0x1006aa886
    bool GetTrigger() const override;                             // @ 0x1006aa8ac
    bool Write(cIGZOStream& stream) const override { return cSC4Advice::Write(stream); }   // @ 0x1006aac20 (thunk 0x1006aac2a)
    bool Read(cIGZIStream& stream) override { return cSC4Advice::Read(stream); }           // @ 0x1006aac38 (thunk 0x1006aac42)
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4MySimBalloonAdvice; }  // @ 0x1006aac50 (thunk 0x1006aac5c)

    // cISC4MySimBalloonAdvice (each has a non-virtual thunk for the +0x140 view)
    cISC4Advice* AsISC4Advice() override { return this; }                          // @ 0x1006aa906 (thunk 0x1006aa910)
    const cISC4Advice* AsISC4AdviceConst() const override { return this; }         // @ 0x1006aa91e (thunk 0x1006aa928)
    const cIGZString& GetTextBalloon() const override { return balloonText_; }     // @ 0x1006aa936 (thunk 0x1006aa944)
    int GetTextBalloonID() const override { return textBalloonId_; }               // @ 0x1006aa94e (thunk 0x1006aa95a)
    int GetIconBalloonCount() const override { return static_cast<int>(icons_.size()); }   // @ 0x1006aa964 (thunk 0x1006aa982)
    cIGZBuffer* GetIconBalloon(int index) const override;                          // @ 0x1006aa99a (thunk 0x1006aa9de)
    std::uint32_t GetIconBalloonID(int index) const override;                      // @ 0x1006aaa1c (thunk 0x1006aaa88)
    int GetSoundID(bool male) const override { return male ? soundMale_ : soundFemale_; }   // @ 0x1006aaa9a (thunk 0x1006aaab8)
    int GetSoundChance() const override { return soundChance_; }                   // @ 0x1006aac68 (thunk 0x1006aac74)
    bool GetRawTrigger() override { return cSC4Advice::GetRawTrigger(); }          // @ 0x1006aa890 (thunk 0x1006aa89a)
    void SetStatusFlags(int flags, bool on) override;                              // @ 0x1006aaad0 (thunk 0x1006aab1e)
    bool CheckStatusFlags(int flags) const override { return (statusFlags_ & static_cast<std::uint32_t>(flags)) != 0; }   // @ 0x1006aab32 (thunk 0x1006aab42)

private:
    std::vector<cSLBuffer> icons_;         // +0x148
    cADVString balloonText_{""};           // +0x160 (the original starts as a plain cSLString)
    int textBalloonId_ = 0;                // +0x190
    std::uint32_t statusFlags_ = 0;        // +0x194
    int soundMale_ = 0;                    // +0x198
    int soundFemale_ = 0;                  // +0x19c
    int soundChance_ = 0;                  // +0x1a0
};

}  // namespace sc4

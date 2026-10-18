// cGZWinIMEServer — the input-method (IME) service (service id 0x2ac45447, priority -999999, interface 0x2ac44ae7; ctor
// 0x1001f6e50, vtable 0x100a84980). On the Mac port no IME exists: IsIMEAvailable answers false, composition and candidate
// state are only stored, and the "Set...Active" calls are really no-ops that merely re-read the flag (they call the matching
// getter and report success).
//
// Layout: +8 cRZSystemService, +0x2c refcount, +0x34 initialised, +0x35 enabled (starts true), +0x38 message server,
// +0x40 message server 2, +0x48 the composition string data, +0xa0 the candidate list data.
//
// The two data blocks keep wide (UTF-16) text. cGZIMECompositionStringDataEx owns the text and attribute bytes and its Set()
// points the plain view (text, attributes, length) at them; cGZIMECandidateListDataEx owns the candidate strings, makes sure
// each ends in a NUL (appending one to the stored string when missing) and exposes an array of pointers to them.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "gz/cGZMessageServer.h"
#include "rz/cRZSysServPtr.h"
#include "rz/cRZSystemService.h"

constexpr std::uint32_t kGZSID_cGZWinIMEServer = 0x2ac45447;
constexpr std::int32_t kGZWinIMEServerPriority = -999999;
constexpr std::uint32_t kGZIID_cIGZWinIMEServer = 0x2ac44ae7;
constexpr std::uint32_t kGZMessage_IMEStartComposition = 0x2ac44c0e;
constexpr std::uint32_t kGZMessage_IMEEndComposition = 0x2ac44c11;
constexpr std::uint32_t kGZMessage_IMEStartCandidateList = 0x2ac44ed1;
constexpr std::uint32_t kGZMessage_IMEEndCandidateList = 0x2ac44ed4;

using IMEString = std::basic_string<std::uint16_t>;

// What a window reads: the text, one attribute byte per character, the length, the caret and the language.
struct cGZIMECompositionStringData {
    const std::uint16_t* text = nullptr;         // +0
    const std::uint8_t* attributes = nullptr;    // +8
    std::uint32_t length = 0;                    // +0x10
    std::uint32_t caret = 0;                     // +0x14
    std::uint32_t language = 0;                  // +0x18
};

struct cGZIMECandidateListData {
    const std::uint16_t* const* candidates = nullptr;   // +0 (NUL terminated strings)
    std::uint32_t count = 0;                             // +8
    std::uint32_t selection = 0;                         // +0xc
    std::uint32_t language = 0;                          // +0x10
};

class IIMELanguageSource {
public:
    virtual ~IIMELanguageSource() = default;
    virtual std::uint32_t CurrentLanguage() = 0;   // cIGZLanguageManager::GetCurrentLanguage (service 0x441e5070)
};
void SetIMELanguageSource(IIMELanguageSource* source);

class cGZIMECompositionStringDataEx : public cGZIMECompositionStringData {
public:
    cGZIMECompositionStringDataEx();                 // @ 0x1001f7510
    void Set();                                      // @ 0x1001f75e0
    IMEString textStorage;                           // +0x20
    std::vector<std::uint8_t> attributeStorage;      // +0x38
    bool active = false;                             // +0x51
};

class cGZIMECandidateListDataEx : public cGZIMECandidateListData {
public:
    cGZIMECandidateListDataEx();                     // @ 0x1001f763e
    void Set();                                      // @ 0x1001f7708
    std::vector<IMEString> strings;                  // +0x18
    std::vector<const std::uint16_t*> pointers;      // +0x30
    bool active = false;                             // +0x48
};

class cIGZWinIMEServer : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                                      // +0x18
    virtual bool Shutdown() = 0;                                                                  // +0x20
    virtual bool IsIMEAvailable() const = 0;                                                      // +0x28
    virtual bool GetEnabled() const = 0;                                                          // +0x30
    virtual void SetEnabled(bool enabled) = 0;                                                    // +0x38
    virtual bool GetCompositionEnabled() const = 0;                                               // +0x40
    virtual bool SetCompositionEnabled(bool enabled) = 0;                                         // +0x48
    virtual bool GetCompositionActive() const = 0;                                                // +0x50
    virtual bool SetCompositionActive(bool active) const = 0;                                     // +0x58
    virtual cGZIMECompositionStringData* GetCompositionStringData() = 0;                          // +0x60
    virtual bool SetCompositionString(const std::uint16_t* text, std::uint32_t length) = 0;       // +0x68
    virtual bool GetCandidateListActive() const = 0;                                              // +0x70
    virtual bool SetCandidateListActive(bool active) const = 0;                                   // +0x78
    virtual cGZIMECandidateListData* GetCandidateListData() = 0;                                  // +0x80
    virtual bool SetCandidateListSelection(std::uint32_t index) = 0;                              // +0x88
};

class cGZWinIMEServer final : public cIGZWinIMEServer, public cRZSystemService {
public:
    cGZWinIMEServer() : cRZSystemService(kGZSID_cGZWinIMEServer, kGZWinIMEServerPriority) {}   // @ 0x1001f6e50
    ~cGZWinIMEServer() override;                                                              // @ 0x1001f6f66

    bool QueryInterface(std::uint32_t iid, void** out) override;        // @ 0x1001f70f8 (ids 1, 0x2ac44ae7)
    std::uint32_t AddRef() override { return ++refCount_; }             // @ 0x1001f713c
    std::uint32_t Release() override;                                   // @ 0x1001f7166
    bool Init() override;                                               // @ 0x1001f721e
    bool Shutdown() override;                                           // @ 0x1001f7338
    bool IsIMEAvailable() const override { return false; }              // @ 0x1001f73a0
    bool GetEnabled() const override { return enabled_; }               // @ 0x1001f73a8
    void SetEnabled(bool enabled) override { enabled_ = enabled; }      // @ 0x1001f73b2
    bool GetCompositionEnabled() const override { return true; }        // @ 0x1001f73c8
    bool SetCompositionEnabled(bool) override { return true; }          // @ 0x1001f73d0
    bool GetCompositionActive() const override { return composition_.active; }   // @ 0x1001f73d8
    bool SetCompositionActive(bool) const override { GetCompositionActive(); return true; }   // @ 0x1001f73e4
    cGZIMECompositionStringData* GetCompositionStringData() override { return &composition_; }   // @ 0x1001f74b0
    bool SetCompositionString(const std::uint16_t* text, std::uint32_t length) override;   // @ 0x1001f73f2
    bool GetCandidateListActive() const override { return candidates_.active; }    // @ 0x1001f74ba
    bool SetCandidateListActive(bool) const override { GetCandidateListActive(); return true; }   // @ 0x1001f74c6
    cGZIMECandidateListData* GetCandidateListData() override { return &candidates_; }   // @ 0x1001f7502
    bool SetCandidateListSelection(std::uint32_t index) override;       // @ 0x1001f74d4

    cGZIMECompositionStringDataEx& Composition() { return composition_; }
    cGZIMECandidateListDataEx& Candidates() { return candidates_; }

private:
    std::uint32_t refCount_ = 0;                                         // +0x2c
    bool initialized_ = false;                                           // +0x34
    bool enabled_ = true;                                                // +0x35
    cRZSysServPtr<cIGZMessageServer, kGZSID_cGZMessageServer, kGZIID_cIGZMessageServer> messageServer_;   // +0x38
    cRZSysServPtr<cIGZMessageServer2, 0x04fa845b, 0x652294c7> messageServer2_;   // +0x40
    cGZIMECompositionStringDataEx composition_;                          // +0x48
    cGZIMECandidateListDataEx candidates_;                               // +0xa0
};

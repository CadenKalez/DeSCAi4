// cGZCheatCodeManager (36 functions, 0x1002dda80..0x1002df866, service id 0x69d9e74b priority 0x1312d0, iid
// 0xa1085722): the cheat code service. Cheat strings are registered with an id; typing one (case-insensitively,
// as a prefix of the input) notifies the listeners with message 0x230e27ac, records it in the cheat history and
// can switch persistent "cheat states" on and off. Cheat codes as a whole can be disabled behind a password
// (stored RefPack-compressed, exactly the block cRZFastCompression3 produces).
// Layout: +0x2c refcount, +0x30 history enabled, +0x38 registered strings (string, id, "should be saved"), +0x50 /
// +0x68 listeners (old / new message targets, id 0 = every cheat), +0x80 enabled cheat states, +0x98 history
// records (id, julian day, kind: 0 typed / 1 enabled / 2 disabled, 0), +0xb0 password block, +0xbc cheat codes
// enabled, +0xc0 / +0xe8 the console words that enable / disable cheat codes (default "enable" / "disable").
// Save data: u32 total, u32 padded password size, u32 password size, password padded to 4, u32 history bytes,
// the history, u32 enabled-state bytes, the enabled ids.
// Quirks kept: SetCheatCodePassword turns cheat codes ON when the block is consistent; the console enable /
// disable words hand the (always empty) remainder to EnableCheatCodes. Deviation: SetCheatCodeSaveData reads the
// enabled-state list as bytes / 4 ids (the original loops bytes times and reads past the block).
#pragma once
#include "gz/cGZMessageServer.h"   // cIGZMessageTarget (the pre-cIGZMessage2 target: gets a cGZMessage)
#include <cstdint>
#include <list>
#include <set>
#include <string>
#include <vector>

#include "gz/WinInterfaces.h"
#include "gz/cIGZMessage2.h"
#include "gz/cIGZString.h"
#include "rz/cRZString.h"
#include "rz/cRZSystemService.h"

constexpr std::uint32_t kGZIID_cIGZCheatCodeManager = 0xa1085722;
constexpr std::uint32_t kGZSID_cGZCheatCodeManager = 0x69d9e74b;
constexpr std::uint32_t kMessageCheatCodeTyped = 0x230e27ac, kMessageCheatCodesEnabled = 0x030e27b7,
                        kMessageCheatCodesDisabled = 0x69d9a743;

struct CheatCodeHistoryRecord {
    std::uint32_t cheatId, julianDay, kind, extra;
};

class cGZCheatCodeManager final : public cRZSystemService {
public:
    cGZCheatCodeManager();
    ~cGZCheatCodeManager() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool Init() override { return true; }
    bool Shutdown() override { return true; }

    bool RegisterCheatCode(std::uint32_t id, const cIGZString& text);
    bool UnregisterCheatCode(std::uint32_t id);
    bool AddNotification(cIGZMessageTarget* target, std::uint32_t id);
    bool RemoveNotification(cIGZMessageTarget* target, std::uint32_t id);
    bool AddNotification2(cIGZMessageTarget2* target, std::uint32_t id);
    bool RemoveNotification2(cIGZMessageTarget2* target, std::uint32_t id);
    void EnableCheatCodeHistory(bool enable) { historyEnabled_ = enable; }
    void ClearCheatCodeHistory() { history_.clear(); }
    // Fills `buffer` (when non-null) and always reports the required size.
    bool GetCheatCodeSaveData(char* buffer, std::uint32_t& size) const;
    bool SetCheatCodeSaveData(const char* data, std::uint32_t size);
    void AddCheatCodeHistoryRecord(std::uint32_t id, std::uint32_t julianDay, std::uint32_t kind, std::uint32_t extra);
    std::uint32_t GetListOfAllCheatStrings(const cIGZString** out, std::uint32_t& count) const;
    int HasUserUsedCheat(std::uint32_t id) const;
    bool GetCheatState(std::uint32_t id) const { return states_.count(id) != 0; }
    bool SetCheatState(std::uint32_t id, bool on);
    std::uint32_t GetCheatsWithEnabledStates(std::uint32_t* out, std::uint32_t& count) const;
    bool SetStateShouldBeSaved(std::uint32_t id, bool save);
    void ClearAllEnabledCheatStates() { states_.clear(); }
    bool SendCheatNotifications(const cIGZString& text, std::uint32_t id);
    // 0 no match, 1 the input starts with a cheat string, 2 the input is exactly a cheat string
    int DoesCheatCodeMatch(const cIGZString& input, std::uint32_t& id) const;
    bool DoDefaultCheatCodeProcessing(const cIGZString& input, std::uint32_t& id);
    bool EnableCheatCodes(bool enable, const cIGZString& password);
    bool GetCheatCodesEnabled() const { return cheatCodesEnabled_; }
    bool GetCheatCodePassword(std::uint8_t* buffer, std::uint32_t& size) const;
    bool SetCheatCodePassword(const std::uint8_t* buffer, std::uint32_t size);
    bool SetEnableDisableStrings(const cIGZString& enableWord, const cIGZString& disableWord);

    const std::vector<CheatCodeHistoryRecord>& History() const { return history_; }

private:
    struct StringAssociation { cRZString text; std::uint32_t id; bool shouldBeSaved; };
    struct Listener { cIGZMessageTarget* target; std::uint32_t id; };
    struct Listener2 { cIGZMessageTarget2* target; std::uint32_t id; };

    std::uint32_t refCount_ = 0;
    bool historyEnabled_ = true;
    std::list<StringAssociation> cheats_;
    std::list<Listener> listeners_;
    std::list<Listener2> listeners2_;
    std::set<std::uint32_t> states_;
    std::vector<CheatCodeHistoryRecord> history_;
    std::vector<std::uint8_t> password_;       // the whole block: u32 total + RefPack stream (empty = none)
    bool cheatCodesEnabled_ = true;
    cRZString enableWord_, disableWord_;
};

// cSC4AudioAEMSSoundBank — a sound bank file kept in memory for the AEMS sound engine (11 functions at
// 0x1007b547c..0x1007b5658). It loads a whole file (from a resource key or an open file) into a byte buffer and hands
// the buffer to the engine; the engine's module-bank handle is stored beside it. Reference counted; the interface
// answers only iid 1.
// Layout: +8 refcount, +0x10 bytes, +0x28 key, +0x34 module bank handle.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZFile.h"
#include "gz/cIGZUnknown.h"

namespace sc4 {

// SC4GetResourceAsFile (0x10003a3d2): opens a resource of the persistence layer as a file.
class ISoundBankHost {
public:
    virtual ~ISoundBankHost() = default;
    virtual bool GetResourceAsFile(const cGZPersistResourceKey& key, cIGZFile** out, bool flag) = 0;   // a counted reference
};
ISoundBankHost* SoundBankHost();
void SetSoundBankHost(ISoundBankHost* host);

class cSC4AudioAEMSSoundBank final : public cIGZUnknown {
public:
    cSC4AudioAEMSSoundBank() = default;                       // @ 0x1007b547c
    ~cSC4AudioAEMSSoundBank() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1007b5630
    std::uint32_t AddRef() override { return ++refCount_; }       // @ 0x1007b564a
    std::uint32_t Release() override;                             // @ 0x1007b5658

    bool LoadFromResource(const cGZPersistResourceKey& key);      // @ 0x1007b54b6
    bool LoadFromFile(cIGZFile* file);                            // @ 0x1007b555a
    void GetKey(cGZPersistResourceKey& out) const { out = key_; }  // @ 0x1007b55e4
    void SetModuleBankHandle(int handle) { moduleBankHandle_ = handle; }   // @ 0x1007b55fc
    int GetModuleBankHandle() const { return moduleBankHandle_; }          // @ 0x1007b5606
    std::uint8_t* Data() { return bytes_.empty() ? nullptr : bytes_.data(); }   // @ 0x1007b561c
    static void* modulebankmalloc(void* memory, int, int) { return memory; }   // @ 0x1007b5626

private:
    std::int32_t refCount_ = 0;               // +8
    std::vector<std::uint8_t> bytes_;         // +0x10
    cGZPersistResourceKey key_{};             // +0x28
    int moduleBankHandle_ = 0;                // +0x34
};

}  // namespace sc4

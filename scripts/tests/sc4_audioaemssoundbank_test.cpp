#include <cstdio>
#include <vector>

#include "rz/cRZFile.h"
#include "sc4/AudioAEMSSoundBank.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct MemFile final : cRZFile {
    std::vector<std::uint8_t> bytes;
    std::size_t at = 0;
    int closes = 0, refs = 1;
    bool failRead = false;
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    std::uint32_t Length() override { return static_cast<std::uint32_t>(bytes.size()); }
    bool Read(void* d, std::uint32_t n) override {
        if (failRead || at + n > bytes.size()) return false;
        std::memcpy(d, bytes.data() + at, n);
        at += n;
        return true;
    }
    bool Close() override { ++closes; return true; }
};
struct Host final : ISoundBankHost {
    MemFile file;
    bool available = true;
    cGZPersistResourceKey lastKey;
    bool GetResourceAsFile(const cGZPersistResourceKey& key, cIGZFile** out, bool) override {
        lastKey = key;
        if (!available) return false;
        *out = &file;
        return true;
    }
};
}  // namespace

int main() {
    Host host;
    SetSoundBankHost(&host);
    auto* bank = new cSC4AudioAEMSSoundBank();
    bank->AddRef();
    void* v = nullptr;
    CHECK(bank->QueryInterface(1, &v) && v == bank && !bank->QueryInterface(2, &v));
    bank->Release();
    CHECK(bank->Data() == nullptr && bank->GetModuleBankHandle() == 0);
    bank->SetModuleBankHandle(42);
    CHECK(bank->GetModuleBankHandle() == 42);

    CHECK(!bank->LoadFromFile(nullptr));
    host.file.bytes = {1, 2, 3, 4, 5};
    const cGZPersistResourceKey key{0x11, 0x22, 0x33};
    CHECK(bank->LoadFromResource(key));
    CHECK(host.lastKey == key && host.file.closes == 1 && host.file.refs == 0);
    cGZPersistResourceKey out;
    bank->GetKey(out);
    CHECK(out == key && bank->Data() && bank->Data()[0] == 1 && bank->Data()[4] == 5);
    // a failing read leaves the old key
    host.file.refs = 1; host.file.at = 0; host.file.failRead = true;
    CHECK(!bank->LoadFromResource(cGZPersistResourceKey{9, 9, 9}));
    bank->GetKey(out);
    CHECK(out == key && host.file.closes == 2);
    host.available = false;
    CHECK(!bank->LoadFromResource(key));
    host.available = true;
    // a shorter file shrinks the buffer
    host.file.failRead = false; host.file.at = 0; host.file.bytes = {7, 8};
    CHECK(bank->LoadFromFile(&host.file) && bank->Data()[1] == 8);
    CHECK(cSC4AudioAEMSSoundBank::modulebankmalloc(bank, 1, 2) == bank);
    bank->Release();
    SetSoundBankHost(nullptr);
    std::printf(failures ? "FAILED\n" : "sc4_audioaemssoundbank ok\n");
    return failures != 0;
}

#include "sc4/AudioAEMSSoundBank.h"

namespace sc4 {

namespace {
ISoundBankHost* g_host = nullptr;
}
ISoundBankHost* SoundBankHost() { return g_host; }
void SetSoundBankHost(ISoundBankHost* host) { g_host = host; }

bool cSC4AudioAEMSSoundBank::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4AudioAEMSSoundBank::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cSC4AudioAEMSSoundBank::LoadFromFile(cIGZFile* file) {
    if (!file) return false;
    const std::uint32_t size = file->Length();
    bytes_.resize(size);
    return file->Read(bytes_.data(), file->Length());
}

bool cSC4AudioAEMSSoundBank::LoadFromResource(const cGZPersistResourceKey& key) {
    cIGZFile* file = nullptr;
    bool result = false;
    if (g_host && g_host->GetResourceAsFile(key, &file, false) && file) {
        if (LoadFromFile(file)) {
            key_ = key;
            result = file->Close();
        } else {
            file->Close();
        }
    }
    if (file) file->Release();
    return result;
}

}  // namespace sc4

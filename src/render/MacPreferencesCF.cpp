// The CFPreferences-backed store behind MacPreferences (the calls MacPreferences::GetString / PutString /
// Synchronize make directly in the original).
#include <CoreFoundation/CoreFoundation.h>

#include "render/MacPreferences.h"

namespace sgl {

namespace {
class CFStore final : public IPreferencesStore {
public:
    bool CopyString(const char* key, std::string& out) override {
        CFStringRef k = CFStringCreateWithCString(nullptr, key, kCFStringEncodingMacRoman);
        CFPropertyListRef v = CFPreferencesCopyAppValue(k, kCFPreferencesCurrentApplication);
        CFRelease(k);
        if (!v) return false;
        char buf[1024];
        const bool ok = CFGetTypeID(v) == CFStringGetTypeID() &&
                        CFStringGetCString(static_cast<CFStringRef>(v), buf, sizeof buf, kCFStringEncodingMacRoman);
        CFRelease(v);
        if (ok) out = buf;
        return ok;
    }
    void SetString(const char* key, const char* value) override {
        CFStringRef k = CFStringCreateWithCString(nullptr, key, kCFStringEncodingMacRoman);
        CFStringRef v = CFStringCreateWithCString(nullptr, value, kCFStringEncodingMacRoman);
        CFPreferencesSetAppValue(k, v, kCFPreferencesCurrentApplication);
        CFRelease(v);
        CFRelease(k);
    }
    void Synchronize() override { CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication); }
};
}  // namespace

IPreferencesStore* DefaultPreferencesStore() {
    static CFStore store;
    return &store;
}

}  // namespace sgl

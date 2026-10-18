#include "sc4/SLString.h"

#include <cstdio>

#include "gz/cIGZPersistResourceManager.h"
#include "rz/cRZSysServPtr.h"

namespace sc4 {

namespace {
ISLStringHost* g_host = nullptr;
constexpr std::uint32_t kPersistResourceManagerService = 0x056b906e, kPersistResourceManagerIid = 0x656b8efc;
const char kBullet[] = "\xe2\x80\xa2";           // U+2022
const char kZeroWidthSpace[] = "\xe2\x80\x8b";   // U+200B
}  // namespace

ISLStringHost* SLStringHost() { return g_host; }
void SetSLStringHost(ISLStringHost* host) { g_host = host; }

cSLString::cSLString(const cGZPersistResourceKey& key, bool convertTokens) : key_(key) { Load(convertTokens, false); }

cSLString::cSLString(std::uint32_t group, std::uint32_t instance, bool convertTokens) : key_{kResourceTypeSLString, group, instance} {
    Load(convertTokens, false);
}

cSLString& cSLString::operator=(const cSLString& other) {
    if (&other != this) {
        cRZString::operator=(other);
        key_ = other.key_;
    }
    return *this;
}

bool cSLString::Load(bool convertTokens, bool htmlClean) {
    cRZSysServPtr<cIGZPersistResourceManager, kPersistResourceManagerService, kPersistResourceManagerIid> manager;
    if (manager.get()) {
        const int language = g_host ? g_host->LanguageOffset() : 0;
        cGZPersistResourceKey key = key_;
        key.group = key_.group + language;
        cIGZString* text = nullptr;
        bool found = manager->GetResource(key, kGZIID_cIGZString, reinterpret_cast<void**>(&text), 0, nullptr);
        if (language != 0 && !found) {
            key.group = key_.group;
            if (text) {
                cIGZString* stale = text;
                text = nullptr;
                stale->Release();
            }
            found = manager->GetResource(key, kGZIID_cIGZString, reinterpret_cast<void**>(&text), 0, nullptr);
        }
        if (found) {
            AssignFromChars(text->Data(), text->Strlen());
            if (convertTokens) ConvertTokens(nullptr, htmlClean);
        }
        if (text) text->Release();
        if (found) {
            const cRZString bullet(kBullet), replacement(kZeroWidthSpace);
            for (std::size_t at = FindString(bullet, 0, true); at != static_cast<std::size_t>(-1); at = FindString(bullet, at, true))
                ReplaceRangeWithString(at, replacement);
            return true;
        }
    }
    Resize(0);
    return false;
}

bool cSLString::ConvertTokens(cSCStringDetokenizer* detokenizer, bool htmlClean) {
    if (!detokenizer && g_host) detokenizer = g_host->Detokenizer();
    if (detokenizer) {
        const bool previous = detokenizer->GetHTMLCleanMode();
        detokenizer->SetHTMLCleanMode(htmlClean);
        detokenizer->Detokenize(*this, *this);
        detokenizer->SetHTMLCleanMode(previous);
    }
    return false;
}

cADVString::cADVString(const char* text) { FromScriptText(text); }

cGZPersistResourceKey cADVString::KeyFromScriptText(const char* text) {
    cGZPersistResourceKey key{kResourceTypeSLString, kAdvisorTextGroup, 0};
    int value = 0;
    if (std::sscanf(text, "text@0x%08x", &value) == 1 || std::sscanf(text, "text@%08x", &value) == 1 ||
        std::sscanf(text, "text@%u", &value) == 1 || std::sscanf(text, "text@%i", &value) == 1)
        key.instance = static_cast<std::uint32_t>(value);
    return key;
}

void cADVString::FromScriptText(const char* text) { SetKey(KeyFromScriptText(text)); }
void cADVString::FromScriptText(const char* text, unsigned int) { FromScriptText(text); }

}  // namespace sc4

#include <cstdio>
#include <map>
#include <string>

#include "fake_resourcemanager.h"
#include "rz/cRZSysServPtr.h"
#include "sc4/SLString.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Manager final : FakeResourceManager {
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::string> strings;   // (group, instance) -> text
    int lookups = 0;
    bool GetResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out, std::uint32_t, cIGZUnknown*) override {
        ++lookups;
        if (iid != kGZIID_cIGZString || key.type != kResourceTypeSLString) return false;
        auto it = strings.find({key.group, key.instance});
        if (it == strings.end()) return false;
        *out = new cRZString(it->second.c_str());
        static_cast<cIGZString*>(*out)->AddRef();
        return true;
    }
};
struct Host final : ISLStringHost {
    int language = 0;
    cSCStringDetokenizer* detokenizer = nullptr;
    int LanguageOffset() override { return language; }
    cSCStringDetokenizer* Detokenizer() override { return detokenizer; }
};
Manager g_manager;
bool Resolve(std::uint32_t service, std::uint32_t iid, void** out) {
    if (service == 0x056b906e && iid == 0x656b8efc) { *out = static_cast<cIGZPersistResourceManager*>(&g_manager); return true; }
    return false;
}
std::string Text(const cRZString& s) { return std::string(s.Data(), s.Strlen()); }
}  // namespace

int main() {
    Host host;
    SetSLStringHost(&host);
    g_manager.strings[{100, 7}] = "plain";
    g_manager.strings[{110, 7}] = "language ten";
    g_manager.strings[{100, 8}] = "dots \xe2\x80\xa2 and \xe2\x80\xa2!";
    g_manager.strings[{100, 9}] = "Hello #City#";

    // no resource manager: the load fails and the string stays empty
    {
        cSLString s(100, 7, false);
        CHECK(Text(s).empty());
        CHECK(!s.Load(false, false));
    }
    SetRZServiceResolver(&Resolve);
    {
        cSLString s(100, 7, false);
        CHECK(Text(s) == "plain");
        cGZPersistResourceKey key;
        s.GetKey(key);
        CHECK(key.type == kResourceTypeSLString && key.group == 100 && key.instance == 7);
        host.language = 10;
        cSLString translated(100, 7, false);
        CHECK(Text(translated) == "language ten");
        cSLString fallback(100, 8, false);                                       // no group 110: falls back to 100
        CHECK(Text(fallback) == "dots \xe2\x80\x8b and \xe2\x80\x8b!");           // bullets became zero-width spaces
        cSLString missing(100, 99, false);
        CHECK(Text(missing).empty());
        host.language = 0;
        s.SetKey({kResourceTypeSLString, 100, 99});
        CHECK(!s.Load(false, false) && Text(s).empty());
        cSLString copy;
        copy = translated;
        CHECK(Text(copy) == "language ten");
        cGZPersistResourceKey copied;
        copy.GetKey(copied);
        CHECK(copied.group == 100 && copied.instance == 7);
        cSLString byKey(cGZPersistResourceKey{kResourceTypeSLString, 100, 7}, false);
        CHECK(Text(byKey) == "plain");
        copy.AssignFromIGZString(cRZString("assigned"));
        CHECK(Text(copy) == "assigned");
    }
    {   // token conversion
        auto* detokenizer = new cSCStringDetokenizerDefault();
        detokenizer->AddRef();
        detokenizer->SetTokenReplacementValue(cRZString("City"), cRZString("<Town>"));
        host.detokenizer = detokenizer;
        cSLString tokens(100, 9, true);
        CHECK(Text(tokens) == "Hello <Town>");
        CHECK(!detokenizer->GetHTMLCleanMode());
        cSLString raw(100, 9, false);
        CHECK(Text(raw) == "Hello #City#");
        raw.ConvertTokens(nullptr, true);
        CHECK(Text(raw) == "Hello &lt;Town&gt;" && !detokenizer->GetHTMLCleanMode());   // clean mode only during the call
        CHECK(!raw.ConvertTokens(detokenizer, false));
        host.detokenizer = nullptr;
        raw.AssignFromCString("#City#");
        raw.ConvertTokens(nullptr, false);
        CHECK(Text(raw) == "#City#");                                              // no detokenizer: untouched
        detokenizer->Release();
    }
    {   // advisor strings
        cADVString a("text@0x0000002a");
        cGZPersistResourceKey key;
        a.GetKey(key);
        CHECK(key.type == kResourceTypeSLString && key.group == kAdvisorTextGroup && key.instance == 0x2a);
        cADVString b("text@42");
        b.GetKey(key);
        CHECK(key.instance == 0x42);                         // "%08x" comes before "%u": 42 is read as hex
        cADVString c("text@-5");
        c.GetKey(key);
        CHECK(key.instance == static_cast<std::uint32_t>(-5));
        cADVString d("nothing here");
        d.GetKey(key);
        CHECK(key.instance == 0 && key.group == kAdvisorTextGroup);
        d.FromScriptText("text@0x10", 3u);
        d.GetKey(key);
        CHECK(key.instance == 0x10);
        CHECK(cADVString::KeyFromScriptText("text@0xff").instance == 0xff);
    }
    SetRZServiceResolver(nullptr);
    SetSLStringHost(nullptr);
    std::printf(failures ? "FAILED\n" : "sc4_slstring ok\n");
    return failures != 0;
}

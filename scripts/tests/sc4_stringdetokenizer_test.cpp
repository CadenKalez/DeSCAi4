#include <cstdio>
#include <string>

#include "sc4/StringDetokenizer.h"
#include "fake_resourcemanager.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
std::string Run(cSCStringDetokenizer& d, const char* in, bool* ok = nullptr) {
    cRZString input(in), output;
    const bool result = d.Detokenize(input, output);
    if (ok) *ok = result;
    return std::string(output.Data(), output.Strlen());
}

bool Upper(const cIGZString& token, cIGZString& out, void*) {
    std::string s(token.Data(), token.Strlen());
    for (char& c : s) c = char(toupper(c));
    out.AssignFromChars(s.data(), std::uint32_t(s.size()));
    return true;
}
bool Never(const cIGZString&, cIGZString&, void*) { return false; }
bool Catch(const cIGZString& token, cIGZString& out, void* context) {
    ++*static_cast<int*>(context);
    out.AssignFromCString("?");
    return std::string(token.Data(), token.Strlen()) == "mystery";
}

struct Host final : IDetokenizerHost {
    bool simulator = true;
    bool CityName(cIGZString& out) override { out.AssignFromCString("Springfield"); return true; }
    bool MayorName(cIGZString& out) override { out.AssignFromCString("Quimby"); return true; }
    bool GetYear(int& y) override { y = 1902; return simulator; }
    bool GetPopulation(int& p) override { p = 12345; return simulator; }
    void FormatNumber(long v, cIGZString& out) override { std::string s = "<" + std::to_string(v) + ">"; out.AssignFromChars(s.data(), std::uint32_t(s.size())); }
    bool GetRegionName(cIGZString& out) override { out.AssignFromCString("Capital"); return true; }
};

}  // namespace

int main() {
    {
        auto* d = new cSCStringDetokenizer();
        d->AddRef();
        bool ok = false;
        CHECK(Run(*d, "no tokens here", &ok) == "no tokens here" && ok);
        d->SetTokenReplacementValue(cRZString("Name"), cRZString("Bob"));
        CHECK(Run(*d, "hi #name# and #NAME#!", &ok) == "hi Bob and Bob!" && ok);
        CHECK(Run(*d, "a #  Name  # b") == "a Bob b");
        CHECK(Run(*d, "100## sure") == "100# sure");
        CHECK(Run(*d, "dangling #Name") == "dangling #Name");
        CHECK(Run(*d, "#Zed# x", &ok) == "Zed x" && !ok);              // unknown tokens pass through, call fails
        d->SetTokenReplacementMethod(cRZString("up"), Upper, nullptr);
        CHECK(Run(*d, "#up#", &ok) == "UP" && ok);
        d->SetTokenReplacementMethod(cRZString("no"), Never, nullptr);
        CHECK(Run(*d, "#no#", &ok) == "no" && !ok);
        int calls = 0;
        d->AddUnknownTokenReplacementMethod(Catch, &calls, true);
        d->AddUnknownTokenReplacementMethod(Catch, &calls, true);       // already there
        CHECK(Run(*d, "#mystery#", &ok) == "?" && ok && calls == 1);
        CHECK(Run(*d, "#other#", &ok) == "other" && !ok && calls == 2);
        d->AddUnknownTokenReplacementMethod(Catch, &calls, false);
        Run(*d, "#mystery#");
        CHECK(calls == 2);
        d->SetHTMLCleanMode(true);
        CHECK(d->GetHTMLCleanMode());
        d->SetTokenReplacementValue(cRZString("tag"), cRZString("<b>\"a\" & b</b>"));
        CHECK(Run(*d, "#tag#") == "&lt;b&gt;&quot;a&quot; &amp; b&lt;/b&gt;");
        d->SetHTMLCleanMode(false);
        // output may be the input
        cRZString both("#Name#!");
        CHECK(d->Detokenize(both, both) && std::string(both.Data()) == "Bob!");
        void* p = nullptr;
        CHECK(d->QueryInterface(kGZIID_cSCStringDetokenizer, &p) && p == d);
        d->Release();
        CHECK(!d->QueryInterface(5, &p));
        CHECK(d->Init() && d->Shutdown());
        // no resource manager: the key form fails
        cGZPersistResourceKey key;
        cRZString out;
        CHECK(!d->Detokenize(key, out));
        d->Release();
    }
    {
        Host host;
        SetDetokenizerHost(&host);
        auto* d = new cSCStringDetokenizerDefault();
        d->AddRef();
        CHECK(d->Init());
        bool ok = false;
        CHECK(Run(*d, "#City#, #mayor# in #Year# (#Population#) of #Region#", &ok) == "Springfield, Quimby in <1902> (<12345>) of Capital" && ok);
        host.simulator = false;
        CHECK(Run(*d, "#Year#", &ok) == "Year" && !ok);
        cRZString probe, token("zzz");
        CHECK(!d->DefaultReplacementFunction(token, probe));
        SetDetokenizerHost(nullptr);
        CHECK(Run(*d, "#City#", &ok) == "City" && !ok);
        d->Release();
    }
    if (failures == 0) std::printf("sc4_stringdetokenizer: all passed\n");
    return failures == 0 ? 0 : 1;
}

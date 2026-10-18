#include <cstdio>

#include "gz/WinIMEServer.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Lang final : IIMELanguageSource { std::uint32_t CurrentLanguage() override { return 17; } };
}  // namespace

int main() {
    Lang lang;
    SetIMELanguageSource(&lang);
    auto* ime = new cGZWinIMEServer();
    ime->AddRef();
    CHECK(ime->GetServiceID() == kGZSID_cGZWinIMEServer && ime->GetServicePriority() == -999999);
    void* raw = nullptr;
    CHECK(ime->QueryInterface(kGZIID_cIGZWinIMEServer, &raw) && raw == static_cast<cIGZWinIMEServer*>(ime));
    ime->Release();
    CHECK(!ime->QueryInterface(0x1234, &raw));
    CHECK(!ime->IsIMEAvailable() && ime->GetEnabled() && ime->GetCompositionEnabled() && ime->SetCompositionEnabled(false));
    CHECK(ime->Init() && ime->GetEnabled());
    ime->SetEnabled(false);
    CHECK(!ime->GetEnabled());
    CHECK(!ime->GetCompositionActive() && ime->SetCompositionActive(true) && !ime->GetCompositionActive());   // a no-op
    CHECK(!ime->GetCandidateListActive() && ime->SetCandidateListActive(true) && !ime->GetCandidateListActive());

    CHECK(ime->GetCompositionStringData()->language == 17 && ime->GetCompositionStringData()->text == nullptr);
    const std::uint16_t text[] = {'h', 'i', 0x3042};
    CHECK(ime->SetCompositionString(text, 3));
    ime->Composition().attributeStorage = {1, 2, 3};
    ime->Composition().Set();
    CHECK(ime->GetCompositionStringData()->length == 3 && ime->GetCompositionStringData()->text[2] == 0x3042 &&
          ime->GetCompositionStringData()->attributes[1] == 2);
    CHECK(ime->SetCompositionString(nullptr, 0));
    ime->Composition().Set();
    CHECK(ime->GetCompositionStringData()->length == 0 && ime->GetCompositionStringData()->text == nullptr);

    auto& cl = ime->Candidates();
    cl.Set();
    CHECK(cl.count == 0 && cl.candidates == nullptr);
    cl.strings = {IMEString{'a', 'b'}, IMEString{}, IMEString{'c', 0}};
    cl.Set();
    CHECK(cl.count == 3 && cl.candidates[0][2] == 0 && cl.candidates[1][0] == 0 && cl.candidates[2][0] == 'c');
    CHECK(cl.strings[0].size() == 3 && cl.strings[2].size() == 2);   // terminators appended only where missing
    CHECK(ime->SetCandidateListSelection(2) && !ime->SetCandidateListSelection(3) && !ime->SetCandidateListSelection(9));
    CHECK(ime->GetCandidateListData()->language == 17);
    CHECK(ime->Shutdown() && !ime->GetEnabled());
    ime->Release();
    std::printf(failures ? "FAILED\n" : "gz_winimeserver ok\n");
    return failures != 0;
}

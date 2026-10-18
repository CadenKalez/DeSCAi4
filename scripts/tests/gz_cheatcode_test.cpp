#include <cstdio>
#include <vector>

#include "gz/CheatCodeManager.h"
#include "rz/cRZMessage2Standard.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

struct Target final : cIGZMessageTarget {
    int refs = 1;
    std::vector<std::pair<std::uint64_t, std::string>> got;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool DoMessage(cGZMessage& m) override {
        got.push_back({m.param1, std::string(static_cast<cIGZString*>(reinterpret_cast<void*>(m.param2))->Data())});
        return true;
    }
};
struct Target2 final : cIGZMessageTarget2 {
    int refs = 1;
    std::vector<std::uint64_t> got;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool DoMessage(cIGZMessage2* m) override {
        got.push_back(static_cast<cIGZMessage2Standard*>(m)->GetData1());
        return true;
    }
};

int main() {
    auto* manager = new cGZCheatCodeManager();
    manager->AddRef();
    CHECK(manager->GetServiceID() == 0x69d9e74b && manager->GetCheatCodesEnabled());
    CHECK(manager->RegisterCheatCode(7, cRZString("Fund")) && !manager->RegisterCheatCode(8, cRZString("Fund")));
    CHECK(manager->RegisterCheatCode(9, cRZString("boost")));
    std::uint32_t count = 0;
    CHECK(manager->GetListOfAllCheatStrings(nullptr, count) == 2);
    const cIGZString* strings[4];
    count = 4;
    CHECK(manager->GetListOfAllCheatStrings(strings, count) == 2 && std::string(strings[0]->Data()) == "Fund");

    Target oldStyle;
    Target2 newStyle;
    CHECK(manager->AddNotification(&oldStyle, 0) && !manager->AddNotification(&oldStyle, 0) && oldStyle.refs == 2);
    CHECK(manager->AddNotification2(&newStyle, 9) && newStyle.refs == 2);

    std::uint32_t id = 0;
    CHECK(manager->DoesCheatCodeMatch(cRZString("FUND"), id) == 2 && id == 7);
    CHECK(manager->DoesCheatCodeMatch(cRZString("fund 500"), id) == 1 && id == 7);
    CHECK(manager->DoesCheatCodeMatch(cRZString("nothing"), id) == 0 && id == 0);
    CHECK(manager->DoDefaultCheatCodeProcessing(cRZString("fund 500"), id) && id == 7);
    CHECK(oldStyle.got.size() == 1 && oldStyle.got[0].first == 7 && oldStyle.got[0].second == "fund 500" && newStyle.got.empty());
    CHECK(manager->DoDefaultCheatCodeProcessing(cRZString("boost"), id) && newStyle.got.size() == 1 && newStyle.got[0] == 9);
    CHECK(manager->HasUserUsedCheat(7) == 1 && manager->HasUserUsedCheat(9) == 1 && manager->HasUserUsedCheat(1) == 0);

    // states
    CHECK(manager->SetCheatState(7, true) && manager->GetCheatState(7) && manager->HasUserUsedCheat(7) == 1);   // kind 1 is not a typed use
    CHECK(manager->SetCheatState(11, true) && manager->SetStateShouldBeSaved(7, false) && !manager->SetStateShouldBeSaved(99, true));
    std::uint32_t ids[4], n = 4;
    CHECK(manager->GetCheatsWithEnabledStates(ids, n) == 2 && ids[0] == 7 && ids[1] == 11);
    manager->SetCheatState(11, false);
    CHECK(!manager->GetCheatState(11) && manager->History().back().kind == 2);

    // save data round trip
    std::uint32_t size = 0;
    CHECK(manager->GetCheatCodeSaveData(nullptr, size) && size > 0);
    std::vector<char> blob(size);
    std::uint32_t written = size;
    CHECK(manager->GetCheatCodeSaveData(blob.data(), written) && written == size && *reinterpret_cast<std::uint32_t*>(blob.data()) == size);
    auto* restored = new cGZCheatCodeManager();
    restored->AddRef();
    CHECK(restored->SetCheatCodeSaveData(blob.data(), size));
    CHECK(restored->History().size() == manager->History().size() && restored->GetCheatState(7) && !restored->GetCheatState(11));

    // password protection
    CHECK(manager->EnableCheatCodes(false, cRZString("secret")) && !manager->GetCheatCodesEnabled());
    CHECK(manager->DoesCheatCodeMatch(cRZString("fund"), id) == 0);
    std::uint32_t passwordSize = 0;
    CHECK(manager->GetCheatCodePassword(nullptr, passwordSize) && passwordSize > 4);
    CHECK(!manager->EnableCheatCodes(true, cRZString("wrong!")) && !manager->GetCheatCodesEnabled());
    CHECK(!manager->EnableCheatCodes(true, cRZString("Secre")));
    CHECK(manager->EnableCheatCodes(true, cRZString("secret")) && manager->GetCheatCodesEnabled());
    CHECK(!manager->EnableCheatCodes(true, cRZString("")));   // already enabled
    std::string tooLong(0x41, 'x');
    CHECK(!manager->EnableCheatCodes(false, cRZString(tooLong.c_str())));

    // console words
    CHECK(manager->DoDefaultCheatCodeProcessing(cRZString("  Disable "), id) && !manager->GetCheatCodesEnabled());
    CHECK(manager->DoDefaultCheatCodeProcessing(cRZString("enable"), id) && manager->GetCheatCodesEnabled());
    CHECK(!manager->DoDefaultCheatCodeProcessing(cRZString("hello"), id));
    manager->SetEnableDisableStrings(cRZString("ON"), cRZString("OFF"));
    CHECK(manager->DoDefaultCheatCodeProcessing(cRZString("off"), id) && !manager->GetCheatCodesEnabled());
    CHECK(manager->DoDefaultCheatCodeProcessing(cRZString("on"), id) && manager->GetCheatCodesEnabled());

    CHECK(manager->RemoveNotification(&oldStyle, 0) && oldStyle.refs == 1 && !manager->RemoveNotification(&oldStyle, 0));
    CHECK(manager->RemoveNotification2(&newStyle, 9) && manager->UnregisterCheatCode(9) && !manager->UnregisterCheatCode(9));
    void* raw = nullptr;
    CHECK(manager->QueryInterface(0xa1085722, &raw) && !manager->QueryInterface(5, &raw));
    manager->Release();
    manager->Release();
    restored->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}

#include <cstdio>
#include <vector>

#include "gz/WinKeyAccelerator.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
struct Server final : cIGZMessageServer {
    std::vector<cGZMessage> sent;
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool MessageSend(const cGZMessage& m) override { sent.push_back(m); return true; }
    bool MessagePost(const cGZMessage&, bool) override { return true; }
    bool AddNotification(cIGZMessageTarget*, std::uint32_t) override { return true; }
    bool RemoveNotification(cIGZMessageTarget*, std::uint32_t) override { return true; }
    bool GeneralMessagePostToTarget(const cGZMessage&, cIGZMessageTarget*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
};
Server g_server;
bool Resolve(std::uint32_t service, std::uint32_t iid, void** out) {
    if (service == kGZSID_cGZMessageServer && iid == kGZIID_cIGZMessageServer) { *out = &g_server; g_server.AddRef(); return true; }
    return false;
}
constexpr std::uint32_t kKeyA = 0x41, kShift = 0x10;
}  // namespace

int main() {
    SetRZServiceResolver(&Resolve);
    auto* table = new cRZWinKeyAccelerator();
    table->AddRef();
    void* view = nullptr;
    CHECK(table->QueryInterface(kGZIID_cIGZWinKeyAccelerator, &view) && view == table && table->QueryInterface(1, &view));
    CHECK(!table->QueryInterface(7, &view));
    table->Release();
    table->Release();

    CHECK(table->Init() && g_server.refs == 1 && table->Init() && g_server.refs == 1);
    // ctrl (modifier bit 1 -> 0x10000) + A
    const std::uint32_t ctrlA = (2u << 16) | kKeyA;
    CHECK(table->RegisterAccelerator(ctrlA, 0x1234u));
    cGZMessage custom;
    custom.type = 0x9999; custom.param1 = 7; custom.param2 = 8; custom.param3 = 9;
    CHECK(table->RegisterAccelerator(kKeyA, custom));
    CHECK(table->RegisterAccelerator(ctrlA, 0x5555u));                      // first registration wins
    std::uint32_t command = 0;
    CHECK(table->TestForAccelerator(kKeyA, 2, true, command) && command == 0x1234);
    cGZMessage result;
    CHECK(table->TestForAccelerator(kKeyA, 0, true, result) && result.type == 0x9999 && result.param1 == 7 && result.param3 == 9);
    CHECK(!table->TestForAccelerator(kKeyA, 0, false, command));            // the key-up form is another entry
    CHECK(!table->TestForAccelerator(kKeyA, 0xffffffffu, true, command));   // "no modifiers known"
    CHECK(!table->TestForAccelerator(kShift, 0, true, command));            // modifier keys never match
    CHECK(!table->TestForAccelerator(0x42, 0, true, command));
    // key messages
    cGZMessage down;
    down.type = 5; down.param1 = kKeyA; down.param2 = 2;
    CHECK(table->TestForAccelerator(down, command) && command == 0x1234);
    cGZMessage up = down;
    up.type = 6;
    CHECK(!table->TestForAccelerator(up, command));
    cGZMessage other = down;
    other.type = 4;
    CHECK(!table->TestForAccelerator(other, result));
    // presence tests
    CHECK(table->IsAcceleratorPresent(ctrlA, true, true) && table->IsAcceleratorPresent(kKeyA, true, true));
    CHECK(!table->IsAcceleratorPresent(0x42, true, true));
    CHECK(table->IsAcceleratorPresent(kKeyA, false, false));                 // modifiers dropped: both entries answer
    table->RegisterAccelerator((1u << 28) | 0x50, 0x77u);
    CHECK(!table->IsAcceleratorPresent(0x50, true, true) && table->IsAcceleratorPresent(0x50, true, false));
    // keys for a command
    std::uint32_t keys[4];
    std::uint32_t count = 4;
    CHECK(table->GetKeysForAccelerator(0x1234, keys, count) == 1 && count == 1 && keys[0] == ctrlA);
    table->RegisterAccelerator(0x60, 0x1234u);
    table->RegisterAccelerator(0x61, 0x1234u);
    count = 2;
    CHECK(table->GetKeysForAccelerator(0x1234, keys, count) == 3 && count == 2);   // more matches than room
    count = 4;
    CHECK(table->GetKeysForAccelerator(0xdead, keys, count) == 0 && count == 0);
    // processing sends the message
    CHECK(table->ProcessAccelerator(down) && g_server.sent.size() == 1 && g_server.sent[0].type == 0x1234);
    CHECK(!table->ProcessAccelerator(up) && g_server.sent.size() == 1);
    // unregistering
    CHECK(table->UnregisterAccelerator(ctrlA) && !table->UnregisterAccelerator(ctrlA));
    CHECK(!table->TestForAccelerator(down, command));
    // without a server the match still answers true
    CHECK(table->Shutdown() && g_server.refs == 0);
    CHECK(table->ProcessAccelerator(cGZMessage{5, kKeyA, 0, 0}) && g_server.sent.size() == 1);
    SetRZServiceResolver(nullptr);
    table->Release();
    std::printf(failures ? "FAILED\n" : "gz_winkeyaccelerator ok\n");
    return failures != 0;
}

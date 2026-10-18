#include <cstdio>
#include <string>

#include "gz/CommandServer.h"
#include "gz/StringCommandGenerator.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Gen final : cIGZCommandGenerator {
    int refs = 1;
    std::string log;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool CommandRegistered(std::uint32_t id, const cIGZString& n, const cIGZString& h) override {
        log += "+" + std::to_string(id) + ":" + n.ToChar() + ":" + h.ToChar() + ";"; return true; }
    bool CommandUnregistered(std::uint32_t id, const cIGZString&, const cIGZString&) override {
        log += "-" + std::to_string(id) + ";"; return true; }
};
struct Dispatcher final : cIGZCommandDispatcher {
    int refs = 1; std::uint32_t lastId = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    std::uint32_t ExecuteCommand(std::uint32_t id, cIGZCommandParameterSet*, cIGZCommandParameterSet*) override { lastId = id; return 5; }
};
// A message server that answers every message by writing status 11 into the output set.
struct MsgServer final : cIGZMessageServer2 {
    int refs = 1, sent = 0; std::uint32_t lastType = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool MessageSend(cIGZMessage2* m) override {
        ++sent; lastType = m->GetType();
        auto* s = static_cast<cIGZMessage2Standard*>(m);
        static_cast<cIGZCommandParameterSet*>(s->GetVoid2())->SetStatusParameterValue(11);
        return true; }
    bool MessagePost(cIGZMessage2*, bool) override { return true; }
    bool AddNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool RemoveNotification(cIGZMessageTarget2*, std::uint32_t) override { return true; }
    bool GeneralMessagePostToTarget(cIGZMessage2*, cIGZMessageTarget2*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return 0; }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }
};
MsgServer g_msg;
cGZCommandServer* g_srv = nullptr;
bool Resolve(std::uint32_t service, std::uint32_t, void** out) {
    if (service == kGZSID_cGZCommandServer) { g_srv->AddRef(); *out = static_cast<cIGZCommandServer*>(g_srv); return true; }
    if (service != 0x04fa845b) return false;
    ++g_msg.refs; *out = static_cast<cIGZMessageServer2*>(&g_msg); return true;
}
}  // namespace

int main() {
    auto* srv = new cGZCommandServer();
    srv->AddRef();
    CHECK(srv->GetServiceID() == kGZSID_cGZCommandServer && srv->GetServicePriority() == -1000000);
    void* raw = nullptr;
    CHECK(srv->QueryInterface(kGZIID_cIGZCommandServer, &raw) && raw == static_cast<cIGZCommandServer*>(srv));
    srv->Release();
    CHECK(srv->QueryInterface(0xcb90253b, &raw)); srv->Release();
    CHECK(srv->QueryInterface(kGZIID_cIGZSystemService, &raw)); srv->Release();

    Gen gen;
    CHECK(srv->RegisterCommandGenerator(&gen) && gen.refs == 2);
    CHECK(srv->RegisterCommandGenerator(&gen) && gen.refs == 2);       // duplicates ignored
    CHECK(!srv->RegisterCommand(0, "x", nullptr) && !srv->RegisterCommand(1, nullptr, nullptr));
    CHECK(srv->RegisterCommand(1, "Echo", "echoes") && gen.log == "+1:Echo:echoes;");
    CHECK(!srv->RegisterCommand(1, "Other", nullptr));
    cGZCommandInfoEntry table[] = {{2, "Alpha", "a"}, {3, "Beta", nullptr}, {0, "stop", nullptr}, {4, "Never", nullptr}};
    CHECK(srv->RegisterCommands(table, 4));
    CHECK(srv->GetCommandID(cRZString("alpha")) == 2 && srv->GetCommandID(cRZString("BETA")) == 3 && srv->GetCommandID(cRZString("nope")) == 0);
    CHECK(srv->GetCommandIDFromCommandString("beta", 0xffffffffu) == 3 && srv->GetCommandIDFromCommandString(nullptr, 0) == 0);
    cRZString name, help;
    CHECK(srv->GetCommandInfo(1, name, &help) && std::string(name.ToChar()) == "Echo" && std::string(help.ToChar()) == "echoes");
    CHECK(srv->GetCommandInfo(3, name, nullptr) && !srv->GetCommandInfo(9, name, nullptr));
    CHECK(srv->RegisterCommandParameterInfo(3, "beta help") && srv->RegisterCommandParameterInfo(3, "") && !srv->RegisterCommandParameterInfo(9, "x"));
    srv->GetCommandInfo(3, name, &help);
    CHECK(std::string(help.ToChar()) == "beta help");
    srv->SetDefaultHelpFormat(true);
    CHECK(srv->RegisterCommandParameterInfo(3, nullptr));
    srv->GetCommandInfo(3, name, &help);
    CHECK(std::string(help.ToChar()) == "Format: Beta\nOutput: <none>");
    std::vector<std::uint32_t> ids;
    srv->EnumerateCommands(ids);
    CHECK(ids.size() == 3 && ids[0] == 1 && ids[2] == 3);

    gen.log.clear();
    Gen late;
    CHECK(srv->RegisterCommandGenerator(&late) && late.log.find("+1:Echo") == 0 && late.log.find("+3:Beta") != std::string::npos);
    CHECK(srv->UnregisterCommand(2) && !srv->UnregisterCommand(2) && gen.log == "-2;");
    cGZCommandInfoEntry gone[] = {{1, nullptr, nullptr}, {3, nullptr, nullptr}, {0, nullptr, nullptr}};
    CHECK(srv->UnregisterCommands(gone, 3) && srv->GetCommandID(cRZString("echo")) == 0);
    CHECK(srv->UnregisterCommandGenerator(&late) && late.refs == 1 && srv->UnregisterCommandGenerator(&late));

    // Without a dispatcher or generator.
    CHECK(srv->ExecuteCommand(1u, nullptr, nullptr) == 2 && srv->ExecuteCommand("x", 1, nullptr) == 2);
    cIGZCommandParameterSet* ps = nullptr;
    std::uint32_t id = 0;
    CHECK(!srv->ConvertStringToCommand("x", 1, id, ps) && !srv->ConvertCommandToString(1, nullptr, name) && !srv->ConvertCommandOutputToString(nullptr, name));
    CHECK(srv->ExecuteCommandFormatted("a %d", nullptr, 5) == 2);
    Dispatcher disp;
    CHECK(srv->SetCommandDispatcher(&disp) && disp.refs == 2 && srv->GetCommandDispatcher() == &disp);
    CHECK(srv->ExecuteCommand(77u, nullptr, nullptr) == 5 && disp.lastId == 77);
    CHECK(srv->SetCommandDispatcher(nullptr) && disp.refs == 1);
    srv->CreateCommandParameterSet(&ps);
    CHECK(ps && ps->GetParameterCount() == 0);
    ps->Release();

    // Init builds the default dispatcher and generator; commands then run through the message server.
    g_srv = srv;
    SetRZServiceResolver(&Resolve);
    CHECK(srv->Init() && srv->GetCommandDispatcher());
    CHECK(srv->RegisterCommand(21, "Ping", "p"));
    CHECK(srv->ExecuteCommand("ping 1 2", 0xffffffffu, nullptr) == 11 && g_msg.sent == 1 && g_msg.lastType == 21);
    CHECK(srv->ExecuteCommand("unknown", 0xffffffffu, nullptr) == 2 && g_msg.sent == 1);
    CHECK(srv->ExecuteCommandFormatted("ping %d", nullptr, 3) == 11 && g_msg.sent == 2);
    auto* out = new cGZCommandParameterSet();
    out->AddRef();
    CHECK(srv->ExecuteCommand(21u, nullptr, out) == 11);
    CHECK(srv->Shutdown() && !srv->GetCommandDispatcher());
    out->Release();
    SetRZServiceResolver(nullptr);
    srv->Release();
    std::printf(failures ? "FAILED\n" : "gz_commandserver ok\n");
    return failures != 0;
}

#include <cstdio>
#include <map>
#include <string>

#include "gz/StringCommandGenerator.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct Server final : cIGZCommandServer {
    int refs = 1;
    std::map<std::string, std::uint32_t> ids{{"echo", 7}, {"say hello", 9}};
    std::uint32_t lastId = 0;
    std::string lastArgs;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    void CreateCommandParameterSet(cIGZCommandParameterSet**) override {}
    bool RegisterCommand(std::uint32_t, const char*, const char*) override { return true; }
    bool RegisterCommands(const cGZCommandInfoEntry*, std::uint32_t) override { return true; }
    bool RegisterCommandParameterInfo(std::uint32_t, const char*) override { return true; }
    bool UnregisterCommand(std::uint32_t) override { return true; }
    bool UnregisterCommands(const cGZCommandInfoEntry*, std::uint32_t) override { return true; }
    bool GetCommandInfo(std::uint32_t id, cIGZString& name, cIGZString*) const override {
        for (auto& e : ids) if (e.second == id) { name.AssignFromCString(e.first.c_str()); return true; }
        return false;
    }
    std::uint32_t GetCommandID(const cIGZString& name) const override {
        auto it = ids.find(name.ToChar());
        return it == ids.end() ? 0 : it->second;
    }
    std::uint32_t GetCommandIDFromCommandString(const char*, std::uint32_t) const override { return 0; }
    void EnumerateCommands(std::vector<std::uint32_t>&) const override {}
    bool RegisterCommandGenerator(cIGZCommandGenerator*) override { return true; }
    bool UnregisterCommandGenerator(cIGZCommandGenerator*) override { return true; }
    cIGZCommandDispatcher* GetCommandDispatcher() const override { return nullptr; }
    bool SetCommandDispatcher(cIGZCommandDispatcher*) override { return true; }
    std::uint32_t ExecuteCommand(std::uint32_t id, cIGZCommandParameterSet* in, cIGZCommandParameterSet* out) override {
        lastId = id;
        lastArgs.clear();
        for (std::uint32_t p = in->GetFirstParameterID(); p != 0xffffffffu; p = in->GetNextParameterID(p)) {
            cRZString s;
            in->GetParameter(p)->GetValString(s);
            lastArgs += "[" + std::string(s.ToChar()) + "]";
        }
        if (out) out->AppendParameter(cRZVariant(static_cast<std::uint32_t>(42)));
        return 0;
    }
    std::uint32_t ExecuteCommand(const char*, std::uint32_t, cIGZCommandParameterSet*) override { return 2; }
    std::uint32_t ExecuteCommandFormatted(const char*, cIGZCommandParameterSet*, ...) override { return 2; }
    bool ConvertStringToCommand(const char*, std::uint32_t, std::uint32_t&, cIGZCommandParameterSet*&) override { return false; }
    bool ConvertCommandToString(std::uint32_t, cIGZCommandParameterSet*, cIGZString&) override { return false; }
    bool ConvertCommandOutputToString(cIGZCommandParameterSet*, cIGZString&) override { return false; }
};
Server g_server;
bool Resolve(std::uint32_t service, std::uint32_t iid, void** out) {
    if (service != kGZSID_cGZCommandServer || iid != kGZIID_cIGZCommandServer) return false;
    ++g_server.refs;
    *out = static_cast<cIGZCommandServer*>(&g_server);
    return true;
}
}  // namespace

int main() {
    auto* gen = new cGZStringCommandGenerator();
    gen->AddRef();
    void* raw = nullptr;
    CHECK(gen->QueryInterface(kGZIID_cIGZStringCommandGenerator, &raw) && raw == static_cast<cIGZStringCommandGenerator*>(gen));
    gen->Release();
    CHECK(gen->QueryInterface(kGZIID_cIGZCommandGenerator, &raw) && gen->QueryInterface(1, &raw));
    gen->Release();
    gen->Release();
    CHECK(!gen->QueryInterface(0x1234, &raw));
    CHECK(gen->CommandRegistered(1, cRZString("a"), cRZString("b")) && gen->CommandUnregistered(1, cRZString("a"), cRZString("b")));

    // Without a server nothing works.
    std::uint32_t id = 0;
    cIGZCommandParameterSet* params = nullptr;
    CHECK(gen->Init());
    CHECK(!gen->ConvertStringToCommand("echo hi", 0xffffffffu, id, params) && gen->ExecuteCommand("echo hi", 0xffffffffu, nullptr) == 2);
    cRZString out;
    CHECK(!gen->ConvertCommandToString(7, nullptr, out) && out.Strlen() == 0);

    SetRZServiceResolver(&Resolve);
    CHECK(gen->Init() && g_server.refs == 2);
    CHECK(gen->Init() && g_server.refs == 2);   // already found

    // Text -> command.
    CHECK(gen->ConvertStringToCommand("echo one \"two words\" three", 0xffffffffu, id, params) && id == 7 && params);
    CHECK(params->GetParameterCount() == 3);
    cRZString s;
    CHECK(params->GetParameter(0)->GetValString(s) && std::string(s.ToChar()) == "one");
    CHECK(params->GetParameter(1)->GetValString(s) && std::string(s.ToChar()) == "two words");
    CHECK(params->GetParameter(2)->GetValString(s) && std::string(s.ToChar()) == "three");
    params->Release();
    params = nullptr;
    CHECK(!gen->ConvertStringToCommand("nosuch 1", 0xffffffffu, id, params) && !params);
    CHECK(!gen->ConvertStringToCommand("   ", 0xffffffffu, id, params));
    CHECK(gen->ConvertStringToCommand("echo a b c d e f g h i j", 8, id, params) && params->GetParameterCount() == 2);   // honours the length: "echo a b"
    params->Release();
    params = nullptr;

    // Execute.
    auto* result = new cGZCommandParameterSet();
    result->AddRef();
    CHECK(gen->ExecuteCommand("echo x \"y z\"", 0xffffffffu, result) == 0);
    CHECK(g_server.lastId == 7 && g_server.lastArgs == "[x][y z]" && result->GetParameterCount() == 1);
    CHECK(gen->ExecuteCommand("bogus", 0xffffffffu, result) == 2);

    // Command -> text.
    auto* args = new cGZCommandParameterSet();
    args->AddRef();
    args->AppendParameter(cRZVariant(cRZString("plain")));
    args->AppendParameter(cRZVariant(cRZString("two words")));
    CHECK(gen->ConvertCommandToString(7, args, out) && std::string(out.ToChar()) == "echo plain \"two words\"");
    CHECK(gen->ConvertCommandToString(9, nullptr, out) && std::string(out.ToChar()) == "\"say hello\"");
    CHECK(!gen->ConvertCommandToString(99, args, out) && out.Strlen() == 0);
    CHECK(gen->ConvertCommandOutputToString(args, out) && std::string(out.ToChar()) == "plain \"two words\"");
    auto* empty = new cGZCommandParameterSet();
    empty->AddRef();
    out.AssignFromCString("junk");
    CHECK(gen->ConvertCommandOutputToString(empty, out) && out.Strlen() == 0);

    cRZString arg("a b"), plain("ab");
    cGZStringCommandGenerator::ConvertStringToArgumentString(arg, arg);
    CHECK(std::string(arg.ToChar()) == "\"a b\"");
    cRZString dest;
    cGZStringCommandGenerator::ConvertStringToArgumentString(plain, dest);
    CHECK(std::string(dest.ToChar()) == "ab");

    result->Release(); args->Release(); empty->Release();
    CHECK(gen->Shutdown() && g_server.refs == 1);
    gen->Release();
    SetRZServiceResolver(nullptr);
    std::printf(failures ? "FAILED\n" : "gz_stringcommandgenerator ok\n");
    return failures != 0;
}

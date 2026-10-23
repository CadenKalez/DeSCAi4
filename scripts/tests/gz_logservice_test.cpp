// nGZLog::cService — test. Drives the recovered logging service through fake output backends, a fake
// framework/command-line host and a recording cIGZOStream:
//  * COM identity and the three vtable subobjects (cIService at +0x28, cRZUnknown at +8)
//  * the filter/log-level string maps (including the inverted none/all quirk and the 1/2 offsets)
//  * backend registration/removal/lookup, the case-sensitive name set vs case-insensitive list search,
//    RemoveAllOutputBackEnds leaving the names taken, the default backend bypassing the name set
//  * AddOutputStream through the backend factory, level propagation, group bookkeeping
//  * Output/OutputLevel formatting and backends, the re-entrant +0x30 queue double-delivery quirk and
//    the other-thread +0x48 queue drained by OnTick
//  * Init's -filterLevel/-logLevel parsing and tick registration, Shutdown's cleanup and second calls
//  * DumpOutputLevels, including printing filter values through StringFromLogLevel ("invalid filter level")
//
// Build (from the repo root):
//   /usr/bin/clang++ -std=c++20 -Wall -Wextra -O1 -fsanitize=address,undefined -Isrc
//       -Iscripts/tests/stubs -Iscripts/tests scripts/tests/gz_logservice_test.cpp src/gz/LogService.cpp
//       src/rz/cRZString.cpp src/rz/cRZCriticalSection.cpp -o /tmp/log_test && /tmp/log_test

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "gz/LogService.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
#define CHECK_EQ(a, b) do { auto _a = (a); auto _b = (b); if (!(_a == _b)) { \
    std::printf("FAIL line %d: %s == %s\n", __LINE__, #a, #b); ++failures; } } while (0)

namespace {

// --- recording output stream (cIGZOStream slots 3..20) ---------------------------------------------
class RecordingStream final : public cIGZOStream {
public:
    std::string text;
    std::uint32_t refs = 0;

    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZOStream) { *out = this; AddRef(); return true; }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { if (refs > 1) return --refs; delete this; return 0; }

    void Flush() override {}
    bool SetSint8(std::int8_t) override { return true; }
    bool SetUint8(std::uint8_t) override { return true; }
    bool SetSint16(std::int16_t) override { return true; }
    bool SetUint16(std::uint16_t) override { return true; }
    bool SetSint32(std::int32_t) override { return true; }
    bool SetUint32(std::uint32_t) override { return true; }
    bool SetSint64(std::int64_t) override { return true; }
    bool SetUint64(std::uint64_t) override { return true; }
    bool SetFloat32(float) override { return true; }
    bool SetFloat64(double) override { return true; }
    bool SetRZCharStr(const char* value) override { text += value; return true; }
    bool SetGZStr(const cIGZString& value) override { text += value.ToChar(); return true; }
    bool SetGZSerializable(const cIGZSerializable&) override { return true; }
    bool SetVoid(const void*, std::uint32_t) override { return true; }
    bool GetError() const override { return false; }
    void SetUserData(cIGZVariant*) override {}
    cIGZVariant* GetUserData() override { return nullptr; }
};

struct OutputCall {
    std::string group;
    int level = 0;
    std::string message;
};

// --- fake nGZLog::cDefaultOutputBackEnd (count starts at 0, as in cOutputBackEnd) -----------------
class FakeBackend final : public nGZLog::ILogOutputBackEndHost {
public:
    std::uint32_t refs = 0;
    std::string name;
    int defaultLevel = 0x7fffffff;
    std::map<std::string, int> levels;
    std::vector<OutputCall> outputs;
    bool shouldOutput = true;
    int dumpCalls = 0;
    int queryCalls = 0;
    std::uint32_t lastIid = 0;
    std::function<void(FakeBackend&)> onOutput;

    bool QueryInterface(std::uint32_t iid, void** out) override {
        ++queryCalls;
        lastIid = iid;
        if (iid == nGZLog::kGZIID_cIOutputBackEnd || iid == kGZIID_cIGZUnknown) {
            *out = this;
            AddRef();
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override {
        if (refs > 1) return --refs;
        delete this;
        return 0;
    }

    bool ShouldOutputLevel(const char*, int level) const override { return shouldOutput && level >= -1000; }
    bool SetOutputLevel(const char* group, int level) override {
        if (!group) {
            levels.clear();
            defaultLevel = level;
            return true;
        }
        levels[group] = level;
        return true;
    }
    int GetOutputLevel(const char* group) const override {
        if (!group) return defaultLevel;
        auto it = levels.find(group);
        return it == levels.end() ? defaultLevel : it->second;
    }
    void DumpOutputLevels(cIGZOStream& stream) const override {
        const_cast<FakeBackend*>(this)->dumpCalls++;
        std::string line = "backend:" + name + "\n";
        stream.SetRZCharStr(line.c_str());
    }
    void Output(const char* group, int level, const char* message) override {
        outputs.push_back(OutputCall{group ? group : "", level, message ? message : ""});
        if (onOutput) onOutput(*this);
    }
    bool SetName(const char* newName) override { name = newName ? newName : ""; return true; }
    const char* Name() const override { return name.c_str(); }
};

// --- fake framework/command-line/factory seam ------------------------------------------------------
class FakeHost final : public nGZLog::ILogServiceHost {
public:
    std::map<std::string, std::string> switches;
    int tickAdded = 0;
    int tickRemoved = 0;
    cIGZSystemService* lastTick = nullptr;
    std::vector<FakeBackend*> defaultBackends;
    std::vector<FakeBackend*> streamBackends;
    std::string lastStreamName;
    cIGZOStream* lastStream = nullptr;
    bool lastAppend = false;

    bool GetCommandLineSwitch(const char* name, std::string& value) override {
        auto it = switches.find(name);
        if (it == switches.end()) return false;
        value = it->second;
        return true;
    }
    bool AddToTick(cIGZSystemService* service) override {
        ++tickAdded;
        lastTick = service;
        return true;
    }
    bool RemoveFromTick(cIGZSystemService* service) override {
        ++tickRemoved;
        lastTick = service;
        return true;
    }
    nGZLog::ILogOutputBackEndHost* CreateDefaultOutputBackEnd() override {
        auto* backend = new FakeBackend();
        backend->name = "BackEnd" + std::to_string(defaultBackends.size());
        defaultBackends.push_back(backend);
        return backend;
    }
    nGZLog::ILogOutputBackEndHost* CreateOutputStreamBackEnd(const char* name, cIGZOStream* stream,
                                                             bool append) override {
        lastStreamName = name ? name : "";
        lastStream = stream;
        lastAppend = append;
        auto* backend = new FakeBackend();
        backend->name = name ? name : "";   // the original ctor assigns the name at +0x30
        streamBackends.push_back(backend);
        return backend;
    }
};

struct Fixture {
    FakeHost host;
    nGZLog::cService* service = nullptr;

    Fixture() {
        nGZLog::SetLogServiceHost(&host);
        service = new nGZLog::cService();
        service->AddRef();   // test's own reference
    }
    ~Fixture() {
        // Free the two name maps even for tests that never initialised (the original frees them only in
        // Shutdown; a second Init/Shutdown pair is harmless and keeps the test leak-free).
        service->Init();
        service->Shutdown();
        service->Release();
        nGZLog::SetLogServiceHost(nullptr);
    }
};

void TestIdentity() {
    Fixture f;
    CHECK_EQ(f.service->GetServiceID(), 0x9b69c4eu);
    CHECK_EQ(f.service->GetServicePriority(), 8999999);
    CHECK_EQ(sizeof(cRZSystemService), static_cast<std::size_t>(0x28));
    CHECK_EQ(reinterpret_cast<char*>(static_cast<nGZLog::cIService*>(f.service)) -
                 reinterpret_cast<char*>(f.service),
             static_cast<std::ptrdiff_t>(0x28));

    void* out = nullptr;
    CHECK(f.service->QueryInterface(nGZLog::kGZIID_cIService, &out));
    CHECK(out == static_cast<nGZLog::cIService*>(f.service));
    CHECK(f.service->QueryInterface(kGZIID_cIGZSystemService, &out));
    CHECK(out == static_cast<cIGZSystemService*>(f.service));
    CHECK(f.service->QueryInterface(kGZIID_cIGZUnknown, &out));
    CHECK(reinterpret_cast<char*>(out) == reinterpret_cast<char*>(f.service) + 8);
    CHECK(!f.service->QueryInterface(0x12345678u, &out));
    // Three successful QueryInterface calls took three references.
    f.service->Release();
    f.service->Release();
    f.service->Release();
}

void TestLevelStrings() {
    Fixture f;
    CHECK(f.service->Init());

    // Filter levels: none=0x7fffffff, all=0, names 1..7, number+1.
    CHECK_EQ(f.service->FilterLevelFromString("none"), 0x7fffffff);
    CHECK_EQ(f.service->FilterLevelFromString("all"), 0);
    CHECK_EQ(f.service->FilterLevelFromString("errors"), 1);
    CHECK_EQ(f.service->FilterLevelFromString("errorsPlus"), 2);
    CHECK_EQ(f.service->FilterLevelFromString("debug"), 7);
    CHECK_EQ(f.service->FilterLevelFromString("ERRORS"), 1);   // strcasecmp
    CHECK_EQ(f.service->FilterLevelFromString("3"), 4);        // atoi + 1
    CHECK_EQ(f.service->FilterLevelFromString("3x"), 4);       // atoi stops at 'x'
    CHECK_EQ(f.service->FilterLevelFromString(""), -1);
    CHECK_EQ(f.service->FilterLevelFromString(nullptr), -1);
    CHECK_EQ(f.service->FilterLevelFromString("nonsense"), -1);

    CHECK_EQ(std::strcmp(f.service->StringFromFilterLevel(0x7fffffff), "none"), 0);
    CHECK_EQ(std::strcmp(f.service->StringFromFilterLevel(0), "all"), 0);
    CHECK_EQ(std::strcmp(f.service->StringFromFilterLevel(1), "errors"), 0);
    CHECK_EQ(std::strcmp(f.service->StringFromFilterLevel(7), "debug"), 0);
    CHECK_EQ(std::strcmp(f.service->StringFromFilterLevel(8), "invalid filter level"), 0);
    CHECK_EQ(std::strcmp(f.service->StringFromFilterLevel(-1), "invalid filter level"), 0);

    // Log levels: none=0, all=0x7fffffff, names 2..8, number+1.
    CHECK_EQ(f.service->LogLevelFromString("none"), 0);
    CHECK_EQ(f.service->LogLevelFromString("all"), 0x7fffffff);
    CHECK_EQ(f.service->LogLevelFromString("errors"), 2);
    CHECK_EQ(f.service->LogLevelFromString("debug"), 8);
    CHECK_EQ(f.service->LogLevelFromString("0"), 1);
    CHECK_EQ(f.service->LogLevelFromString(""), -1);
    CHECK_EQ(f.service->LogLevelFromString("nonsense"), -1);

    CHECK_EQ(std::strcmp(f.service->StringFromLogLevel(0), "none"), 0);
    CHECK_EQ(std::strcmp(f.service->StringFromLogLevel(0x7fffffff), "all"), 0);
    CHECK_EQ(std::strcmp(f.service->StringFromLogLevel(2), "errors"), 0);
    CHECK_EQ(std::strcmp(f.service->StringFromLogLevel(8), "debug"), 0);
    CHECK_EQ(std::strcmp(f.service->StringFromLogLevel(1), "invalid filter level"), 0);
    CHECK_EQ(std::strcmp(f.service->StringFromLogLevel(9), "invalid filter level"), 0);

    CHECK(f.service->Shutdown());
}

void TestBackendRegistration() {
    Fixture f;
    CHECK(f.service->Init());   // creates the unnamed default backend
    CHECK_EQ(f.host.defaultBackends.size(), static_cast<std::size_t>(1));
    // The default backend is in the list but not in the +0xa0 name set.
    CHECK_EQ(f.service->OutputBackEndList()->size(), static_cast<std::size_t>(0));

    // Configure a group level before adding a backend; AddOutputBackEnd replays it.
    f.service->SetOutputLevel(nullptr, "grp", 5);

    auto* alpha = new FakeBackend();
    alpha->AddRef();   // caller reference
    CHECK(f.service->AddOutputBackEnd("alpha", alpha));
    CHECK_EQ(alpha->name, std::string("alpha"));
    CHECK_EQ(alpha->refs, 2u);   // caller + list
    CHECK_EQ(alpha->defaultLevel, 0x7fffffff);
    CHECK_EQ(alpha->levels["grp"], 5);
    CHECK(!f.service->AddOutputBackEnd("alpha", alpha));   // exact duplicate
    CHECK_EQ(alpha->refs, 2u);
    CHECK_EQ(f.service->OutputBackEndList()->size(), static_cast<std::size_t>(1));
    CHECK_EQ(f.service->OutputBackEndList()->at("alpha"), 0);

    // GetOutputBackEnd queries the backend with iid 0x49b84afb and returns a new reference.
    nGZLog::ILogOutputBackEndHost* got = nullptr;
    CHECK(f.service->GetOutputBackEnd("alpha", &got));
    CHECK(got == alpha);
    CHECK_EQ(alpha->refs, 3u);
    CHECK_EQ(alpha->queryCalls, 1);
    CHECK_EQ(alpha->lastIid, nGZLog::kGZIID_cIOutputBackEnd);
    got->Release();
    CHECK_EQ(alpha->refs, 2u);

    // The name-set lookup is case-sensitive, so the list is not even searched for "Alpha".
    CHECK(!f.service->GetOutputBackEnd("Alpha", &got));
    CHECK(!f.service->GetOutputBackEnd("missing", &got));

    // Removing an existing name compares Name() with strcasecmp and erases the map entry.
    CHECK(f.service->RemoveOutputBackEnd("alpha"));
    CHECK_EQ(alpha->refs, 1u);
    CHECK_EQ(f.service->OutputBackEndList()->size(), static_cast<std::size_t>(0));
    CHECK(!f.service->RemoveOutputBackEnd("alpha"));
    CHECK(!f.service->GetOutputBackEnd("alpha", &got));

    // Register with a mixed-case key, remove through the exact key: list search is case-insensitive.
    auto* beta = new FakeBackend();
    beta->AddRef();
    CHECK(f.service->AddOutputBackEnd("Beta", beta));
    CHECK(f.service->RemoveOutputBackEnd("Beta"));
    CHECK_EQ(beta->refs, 1u);
    CHECK(!f.service->RemoveOutputBackEnd("beta"));   // map lookup is case-sensitive

    // RemoveAllOutputBackEnds clears the list but leaves the +0xa0 names taken (quirk).
    auto* gamma = new FakeBackend();
    gamma->AddRef();
    CHECK(f.service->AddOutputBackEnd("Gamma", gamma));
    f.service->RemoveAllOutputBackEnds();
    CHECK_EQ(gamma->refs, 1u);
    CHECK_EQ(f.service->OutputBackEndList()->find("Gamma") != f.service->OutputBackEndList()->end(), true);
    CHECK(!f.service->GetOutputBackEnd("Gamma", &got));
    auto* gamma2 = new FakeBackend();
    CHECK(!f.service->AddOutputBackEnd("Gamma", gamma2));   // name still present
    delete gamma2;                                          // never referenced by the service

    alpha->Release();
    beta->Release();
    gamma->Release();

    CHECK(f.service->Shutdown());
}

void TestAddOutputStream() {
    Fixture f;
    CHECK(f.service->Init());
    RecordingStream stream;

    nGZLog::ILogOutputBackEndHost* out = nullptr;
    CHECK(f.service->AddOutputStream("log.txt", &stream, true, &out));
    CHECK(out != nullptr);
    CHECK_EQ(f.host.lastStreamName, std::string("log.txt"));
    CHECK(f.host.lastStream == &stream);
    CHECK_EQ(f.host.lastAppend, true);
    CHECK_EQ(out->Name(), std::string("log.txt"));
    CHECK_EQ(f.service->OutputBackEndList()->size(), static_cast<std::size_t>(1));
    CHECK_EQ(f.service->OutputBackEndList()->at("log.txt"), 0);

    auto* backend = f.host.streamBackends.back();
    CHECK_EQ(backend->refs, 2u);   // list + out
    CHECK_EQ(backend->defaultLevel, 0x7fffffff);

    out->Release();
    CHECK_EQ(backend->refs, 1u);

    // A duplicate name makes AddOutputBackEnd fail, but AddOutputStream still hands out the object.
    nGZLog::ILogOutputBackEndHost* out2 = nullptr;
    CHECK(f.service->AddOutputStream("log.txt", &stream, false, &out2));
    CHECK(out2 != nullptr);   // not in the list; the caller owns the only reference
    CHECK_EQ(f.host.streamBackends.back()->refs, 1u);
    out2->Release();

    CHECK(f.service->Shutdown());
}

void TestOutputAndQueues() {
    Fixture f;
    CHECK(f.service->Init());
    FakeBackend* backend = f.host.defaultBackends[0];

    // Before Init nothing is emitted (separate service, same host).
    {
        nGZLog::cService* cold = new nGZLog::cService();
        cold->AddRef();
        cold->Output("early", "ignored");
        CHECK_EQ(cold->GroupList()->size(), static_cast<std::size_t>(0));
        CHECK(cold->Init());
        CHECK(cold->Shutdown());
        cold->Release();
    }

    f.service->Output("grp", "hello %d", 7);
    CHECK_EQ(backend->outputs.size(), static_cast<std::size_t>(1));
    CHECK_EQ(backend->outputs[0].group, std::string("grp"));
    CHECK_EQ(backend->outputs[0].level, 0);
    CHECK_EQ(backend->outputs[0].message, std::string("hello 7"));
    CHECK_EQ(f.service->GroupList()->size(), static_cast<std::size_t>(1));
    CHECK(f.service->GroupList()->find("grp") != f.service->GroupList()->end());

    f.service->OutputLevel("grp", 2, "level %s", "two");
    CHECK_EQ(backend->outputs.size(), static_cast<std::size_t>(2));
    CHECK_EQ(backend->outputs[1].level, 2);
    CHECK_EQ(backend->outputs[1].message, std::string("level two"));

    // A message longer than 1024 bytes is formatted into the 0x401 heap buffer and truncated there
    // (the original bug: the retry buffer is not sized from vsnprintf's result).
    std::string longMessage(1500, 'x');
    f.service->Output("big", "%s", longMessage.c_str());
    CHECK_EQ(backend->outputs.size(), static_cast<std::size_t>(3));
    CHECK_EQ(backend->outputs[2].message.size(), static_cast<std::size_t>(0x400));

    // Re-entrant logging from inside Output is queued (+0x30) and then delivered by the drain; because
    // the original copies, recurses and only then pops, the nested entry is delivered twice.
    backend->outputs.clear();
    backend->onOutput = [&f](FakeBackend& b) {
        if (b.outputs.size() == 1 && b.outputs[0].message == "outer") {
            f.service->Output("nested", "inner");
        }
    };
    f.service->Output("outer", "outer");
    CHECK_EQ(backend->outputs.size(), static_cast<std::size_t>(3));
    CHECK_EQ(backend->outputs[0].message, std::string("outer"));
    CHECK_EQ(backend->outputs[1].message, std::string("inner"));
    CHECK_EQ(backend->outputs[2].message, std::string("inner"));
    backend->onOutput = nullptr;

    // Another thread's messages go to the +0x48 queue and are drained by OnTick.
    backend->outputs.clear();
    std::thread worker([&f] { f.service->Output("worker", "from worker %d", 7); });
    worker.join();
    CHECK_EQ(backend->outputs.size(), static_cast<std::size_t>(0));
    CHECK(f.service->OnTick(0));
    CHECK_EQ(backend->outputs.size(), static_cast<std::size_t>(1));
    CHECK_EQ(backend->outputs[0].group, std::string("worker"));
    CHECK_EQ(backend->outputs[0].message, std::string("from worker 7"));
    CHECK(f.service->GroupList()->find("worker") != f.service->GroupList()->end());

    CHECK(f.service->Shutdown());
}

void TestSetOutputLevel() {
    Fixture f;
    CHECK(f.service->Init());
    FakeBackend* backend = f.host.defaultBackends[0];

    f.service->SetOutputLevel(nullptr, "g", 4);
    CHECK_EQ(backend->levels["g"], 4);

    // The backend-specific form only finds names registered in the +0xa0 set: Init's default backend is
    // not registered, so setting a level through its name is a no-op (quirk).
    f.service->SetOutputLevel(backend->name.c_str(), "only", 9);
    CHECK_EQ(backend->levels.find("only"), backend->levels.end());

    auto* named = new FakeBackend();
    named->AddRef();
    CHECK(f.service->AddOutputBackEnd("named", named));
    f.service->SetOutputLevel("named", "only", 9);
    CHECK_EQ(named->levels["only"], 9);
    CHECK_EQ(backend->levels.find("only"), backend->levels.end());
    CHECK_EQ(f.service->GroupList()->size(), static_cast<std::size_t>(0));   // backend-local

    f.service->Output("g", "x");   // records the group name
    CHECK_EQ(f.service->GroupList()->size(), static_cast<std::size_t>(1));

    f.service->SetOutputLevel(nullptr, nullptr, 0);
    CHECK_EQ(backend->defaultLevel, 0);
    CHECK(backend->levels.empty());   // null group clears the backend's map
    CHECK(f.service->GroupList()->size() == 1);   // but not the +0x98 group-name history

    // Unknown backend name: no backend touched, no crash.
    f.service->SetOutputLevel("nope", "g", 1);

    named->Release();
    CHECK(f.service->Shutdown());
}

void TestDumpOutputLevels() {
    Fixture f;
    CHECK(f.service->Init());
    FakeBackend* backend = f.host.defaultBackends[0];

    f.service->SetOutputLevel(nullptr, nullptr, 0);   // default -> log level 0 -> "none"
    f.service->SetOutputLevel(nullptr, "g", 1);       // filter level 1 prints as invalid filter level
    f.service->SetOutputLevel(nullptr, "h", 2);       // filter level 2 prints as errors

    RecordingStream stream;
    f.service->DumpOutputLevels(stream);
    CHECK_EQ(stream.text,
             std::string("All Backends:\n"
                         "-------------\n"
                         "All unspecified groups showing none\n"
                         "Group 'g' showing >= invalid filter level\n"
                         "Group 'h' showing >= errors\n"
                         "-------------\n"
                         "backend:BackEnd0\n"));
    CHECK_EQ(backend->dumpCalls, 1);

    // Initial default level 0x7fffffff prints "all" with no ">= " prefix.
    f.service->SetOutputLevel(nullptr, nullptr, 0x7fffffff);
    f.service->SetOutputLevel(nullptr, nullptr, 0x7fffffff);
    RecordingStream stream2;
    f.service->SetOutputLevel(nullptr, "z", 0);   // "all" (filter scale 0) also has no prefix
    f.service->SetOutputLevel(nullptr, nullptr, 0x7fffffff);   // clears group map again
    f.service->SetOutputLevel(nullptr, "z", 0);
    f.service->DumpOutputLevels(stream2);
    CHECK(stream2.text.find("All unspecified groups showing all\n") != std::string::npos);
    CHECK(stream2.text.find("Group 'z' showing none\n") != std::string::npos);

    // The +0x78 map compares group names case-insensitively (nGZLog::cLessRZString): the second spelling
    // updates the first entry instead of adding one, so only the original key is printed.
    f.service->SetOutputLevel(nullptr, nullptr, 0);
    f.service->SetOutputLevel(nullptr, "Grp", 2);
    f.service->SetOutputLevel(nullptr, "GRP", 3);
    RecordingStream stream3;
    f.service->DumpOutputLevels(stream3);
    CHECK(stream3.text.find("Group 'Grp' showing >= errorsPlus\n") != std::string::npos);
    CHECK(stream3.text.find("Group 'GRP'") == std::string::npos);

    CHECK(f.service->Shutdown());
}

void TestInitShutdown() {
    Fixture f;
    f.host.switches["filterLevel"] = "warnings:main=errors";
    f.host.switches["logLevel"] = "info:chat=debug";

    CHECK(f.service->Init());
    CHECK_EQ(f.host.tickAdded, 1);
    CHECK(f.host.lastTick == static_cast<cIGZSystemService*>(f.service));
    CHECK_EQ(f.host.defaultBackends.size(), static_cast<std::size_t>(1));

    // filterLevel: default warnings -> 3, main=errors -> 1.
    // logLevel:    default info -> 6 (clears the group map), chat=debug -> 8.
    // GroupList is the +0x98 history filled by Output/OutputLevel, not the +0x78 level map: Init alone
    // leaves it empty. The levels are visible through the backend, which receives every SetOutputLevel.
    CHECK_EQ(f.service->GroupList()->size(), static_cast<std::size_t>(0));

    FakeBackend* backend = f.host.defaultBackends[0];
    CHECK_EQ(backend->defaultLevel, 6);
    CHECK(backend->levels.find("main") == backend->levels.end());
    CHECK_EQ(backend->levels["chat"], 8);

    // A second Init is a no-op.
    CHECK(f.service->Init());
    CHECK_EQ(f.host.tickAdded, 1);
    CHECK_EQ(f.host.defaultBackends.size(), static_cast<std::size_t>(1));

    CHECK(f.service->Shutdown());
    CHECK_EQ(f.host.tickRemoved, 1);
    CHECK(f.service->GroupList() == nullptr);
    CHECK(f.service->OutputBackEndList() == nullptr);
    // Post-Shutdown logging is inert in the port (the original would dereference the deleted maps).
    f.service->Output("late", "ignored");
    CHECK(f.service->Shutdown());   // second Shutdown
    CHECK_EQ(f.host.tickRemoved, 1);
}

void TestBackendRefcounts() {
    // The default backend created by Init starts at count 0; after AddRef+push its list holds one ref.
    Fixture f;
    CHECK(f.service->Init());
    FakeBackend* backend = f.host.defaultBackends[0];
    CHECK_EQ(backend->refs, 1u);
    CHECK(f.service->Shutdown());   // list cleared -> backend deleted
}

}  // namespace

int main() {
    TestIdentity();
    TestLevelStrings();
    TestBackendRegistration();
    TestAddOutputStream();
    TestOutputAndQueues();
    TestSetOutputLevel();
    TestDumpOutputLevels();
    TestInitShutdown();
    TestBackendRefcounts();

    if (failures == 0) {
        std::printf("gz_logservice_test: all checks passed\n");
        return 0;
    }
    std::printf("gz_logservice_test: %d failure(s)\n", failures);
    return 1;
}

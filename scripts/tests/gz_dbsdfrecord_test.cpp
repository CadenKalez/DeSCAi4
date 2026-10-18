#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "gz/cGZDBSDFRecord.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
std::string ReadAll(const std::string& path) {
    std::string out;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[64];
        std::size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}
void WriteAll(const std::string& path, const std::string& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}
}  // namespace

int main() {
    const char* tmp = std::getenv("TMPDIR");
    const std::string dir = std::string(tmp ? tmp : "/tmp") + "/";
    const std::string pathA = dir + "sdf_test_a.dat", pathB = dir + "sdf_test_b.dat";
    std::remove(pathA.c_str());
    std::remove(pathB.c_str());
    cGZPersistResourceKey key{0x11111111, 0x22222222, 0x33333333};

    // Create (mode 3): write only. The file must already exist (the open flag set has no create bit).
    WriteAll(pathA, "");
    {
        auto* rec = new cGZDBSDFRecord(nullptr);
        rec->AddRef();
        CHECK(rec->GetSize() == 0xffffffffu && rec->GetPosition() == 0xffffffffu);
        std::uint32_t pos = 7;
        CHECK(!rec->Tell(pos) && pos == 0);
        cIGZString* s = nullptr;
        CHECK(!rec->GetPath(&s) && !rec->GetName(&s) && !rec->GetExtension(&s) && !s);
        CHECK(rec->Read(nullptr, 4) == 0 && rec->Write("x", 1) == 0);
        cRZString path(pathA.c_str());
        CHECK(rec->Init(key, path, cIGZPersistDBSDFRecord::kModeCreate));
        CHECK(rec->GetAccessFlags() == 2 && rec->GetKey().type == 0x11111111 && rec->GetKey().instance == 0x33333333);
        CHECK(!rec->Init(key, path, cIGZPersistDBSDFRecord::kModeCreate));   // already initialised
        CHECK(rec->SetFieldVoid("hello", 5) && rec->Write(" world", 6) == 6);
        char buf[8];
        CHECK(!rec->GetFieldVoid(buf, 1) && rec->Read(buf, 1) == 0);       // not readable in this mode
        CHECK(!rec->SeekAbsolute(0) && !rec->SeekRelative(0));              // seeking by field is read-only
        rec->Flush();
        std::uint32_t p = 0;
        CHECK(rec->Tell(p) && p == 11 && rec->GetSize() == 11 && rec->GetPosition() == 11);
        CHECK(rec->Seek(2, cIGZPersistDBSDFRecord::kSeekBegin) && rec->Tell(p) && p == 2);
        CHECK(!rec->Seek(99, cIGZPersistDBSDFRecord::kSeekBegin));
        CHECK(!rec->SetSize(3));
        CHECK(rec->CloseInternal() && !rec->CloseInternal());
        rec->Release();
    }
    CHECK(ReadAll(pathA) == "hello world");

    // Read (mode 1).
    {
        auto* rec = new cGZDBSDFRecord(nullptr);
        rec->AddRef();
        cRZString path(pathA.c_str());
        CHECK(rec->Init(key, path, cIGZPersistDBSDFRecord::kModeRead) && rec->GetAccessFlags() == 1);
        char buf[16] = {};
        CHECK(rec->GetFieldVoid(buf, 5) && std::string(buf, 5) == "hello");
        CHECK(!rec->SetFieldVoid("x", 1) && rec->Write("x", 1) == 0);
        CHECK(rec->SeekRelative(1) && rec->GetPosition() == 6);
        CHECK(rec->SeekAbsolute(0) && rec->Read(buf, 4) == 4 && std::string(buf, 4) == "hell");
        CHECK(rec->Read(buf, 100) == 7);                                     // returns the count actually read
        cIGZString* s = nullptr;
        CHECK(rec->GetPath(&s) && s && std::string(s->ToChar()) == pathA);
        s->Release();
        s = nullptr;
        CHECK(rec->GetName(&s) && std::string(s->ToChar()) == "sdf_test_a.dat");
        s->Release();
        s = nullptr;
        CHECK(rec->GetExtension(&s) && s && (std::string(s->ToChar()) == ".dat" || std::string(s->ToChar()) == "dat"));
        s->Release();
        void* raw = nullptr;
        CHECK(rec->QueryInterface(kGZIID_cIGZPersistDBSDFRecord, &raw) && raw == static_cast<cIGZPersistDBSDFRecord*>(rec));
        rec->Release();
        CHECK(rec->CloseInternal());
        rec->Release();
    }

    // Rewrite (mode 2) aborted: the original comes back; completed: the new data stays.
    {
        auto* rec = new cGZDBSDFRecord(nullptr);
        rec->AddRef();
        cRZString path(pathA.c_str());
        CHECK(rec->Init(key, path, cIGZPersistDBSDFRecord::kModeRewrite) && rec->GetAccessFlags() == 2);
        CHECK(rec->GetSize() == 0);                                           // truncated for the rewrite
        CHECK(rec->SetFieldVoid("new", 3));
        CHECK(rec->AbortInternal());
        rec->Release();
    }
    CHECK(ReadAll(pathA) == "hello world");
    {
        auto* rec = new cGZDBSDFRecord(nullptr);
        rec->AddRef();
        cRZString path(pathA.c_str());
        CHECK(rec->Init(key, path, cIGZPersistDBSDFRecord::kModeRewrite));
        CHECK(rec->SetFieldVoid("new", 3));
        CHECK(rec->CloseInternal());
        rec->Release();
    }
    CHECK(ReadAll(pathA) == "new");

    // A bad mode leaves nothing initialised; the flags still follow the mode.
    {
        auto* rec = new cGZDBSDFRecord(nullptr);
        rec->AddRef();
        cRZString path(pathA.c_str());
        CHECK(!rec->Init(key, path, cIGZPersistDBSDFRecord::kModeNone) && rec->GetSize() == 0xffffffffu);
        rec->Release();
        auto* missing = new cGZDBSDFRecord(nullptr);
        missing->AddRef();
        cRZString none((dir + "sdf_missing_dir/none.dat").c_str());
        CHECK(!missing->Init(key, none, cIGZPersistDBSDFRecord::kModeRead));
        missing->Release();
    }
    // Destroying an open record aborts it.
    {
        WriteAll(pathB, "keep");
        auto* rec = new cGZDBSDFRecord(nullptr);
        rec->AddRef();
        cRZString path(pathB.c_str());
        CHECK(rec->Init(key, path, cIGZPersistDBSDFRecord::kModeRewrite));
        rec->SetFieldVoid("zz", 2);
        rec->Release();
    }
    CHECK(ReadAll(pathB) == "keep");
    std::remove(pathA.c_str());
    std::remove(pathB.c_str());
    std::printf(failures ? "FAILED\n" : "gz_dbsdfrecord ok\n");
    return failures != 0;
}

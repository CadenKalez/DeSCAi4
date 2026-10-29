#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "dbpf/DBPFReader.h"
#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZPersistDBSegmentMultiPackedFiles.h"
#include "gz/cGZPersistResourceKeyList.h"
#include "rz/cRZString.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static cGZDBSegmentPackedFile* NewSegment(const std::string& path) {
    auto* seg = new cGZDBSegmentPackedFile();
    seg->AddRef();
    seg->Init();
    cRZString p(path.c_str());
    seg->SetPath(p);
    return seg;
}

static std::vector<std::uint8_t> Payload(std::size_t n, int mode, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i)
        v[i] = mode == 0 ? static_cast<std::uint8_t>(i % 13) : mode == 1 ? static_cast<std::uint8_t>(rng()) : 0x55;
    return v;
}

int main(int argc, char** argv) {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "sc4_packed_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string path = (dir / "test.dat").string();

    struct Rec { cGZPersistResourceKey key; std::vector<std::uint8_t> data; };
    std::vector<Rec> recs = {
        {{0x10, 0x20, 1}, Payload(10, 0, 1)},           // tiny, below the compression floor
        {{0x10, 0x20, 2}, Payload(300, 0, 2)},          // small, compressible, RAM-read
        {{0x11, 0x20, 3}, Payload(5000, 0, 3)},         // compressible, lazily read
        {{0x11, 0x21, 4}, Payload(5000, 1, 4)},         // incompressible: stored raw
        {{0x12, 0x20, 5}, Payload(70000, 2, 5)},        // very compressible
        {{0x12, 0x22, 6}, Payload(2100000, 0, 6)},      // spills to a temp file while being written
    };

    {   // create and write
        auto* seg = NewSegment(path);
        CHECK(seg->Open(true, true));
        CHECK(seg->IsOpen());
        for (const Rec& r : recs) {
            cIGZPersistDBRecord* rec = nullptr;
            CHECK(seg->CreateNewRecord(r.key, &rec));
            void* raw = nullptr;
            CHECK(rec && rec->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &raw));
            auto* serial = static_cast<cIGZPersistDBSerialRecord*>(raw);
            CHECK(serial->SetFieldVoid(r.data.data(), static_cast<std::uint32_t>(r.data.size())));
            CHECK(serial->GetSize() == r.data.size());
            serial->Release();
            CHECK(seg->CloseRecord(&rec));
        }
        CHECK(seg->Close());
        seg->Shutdown();
        seg->Release();
    }

    {   // reopen read-only: exact contents, sizes, compressed flags
        auto* seg = NewSegment(path);
        CHECK(seg->Open(true, false));
        CHECK(seg->GetRecordCount(nullptr) == recs.size() + 0 || seg->GetRecordCount(nullptr) == recs.size() + 1);
        for (const Rec& r : recs) {
            CHECK(seg->TestForRecord(r.key));
            CHECK(seg->GetRecordSize(r.key) == r.data.size());
            std::vector<std::uint8_t> out(r.data.size());
            std::uint32_t cap = static_cast<std::uint32_t>(out.size());
            CHECK(seg->ReadRecord(r.key, out.data(), cap) == r.data.size());
            CHECK(out == r.data);
            // via an opened record (RAM or lazy)
            cIGZPersistDBRecord* rec = nullptr;
            CHECK(seg->OpenRecord(r.key, &rec, 1));
            void* raw = nullptr;
            CHECK(rec->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &raw));
            auto* serial = static_cast<cIGZPersistDBSerialRecord*>(raw);
            CHECK(serial->GetSize() == r.data.size());
            std::vector<std::uint8_t> viaRecord(r.data.size());
            CHECK(serial->GetFieldVoid(viaRecord.data(), static_cast<std::uint32_t>(viaRecord.size())));
            CHECK(viaRecord == r.data);
            // a second reader of the same record is allowed, a writer is not
            cIGZPersistDBRecord* second = nullptr;
            CHECK(seg->OpenRecord(r.key, &second, 1));
            CHECK(second != nullptr);
            seg->CloseRecord(&second);
            serial->Release();
            seg->CloseRecord(&rec);
        }
        // compressed bookkeeping: repetitive data is stored compressed, random data is not
        CHECK(seg->GetRecordCompressed(recs[4].key));
        CHECK(!seg->GetRecordCompressed(recs[3].key));
        CHECK(seg->GetRecordSizeRaw(recs[4].key) < recs[4].data.size() / 4);
        CHECK(seg->GetRecordSizeRaw(recs[3].key) == recs[3].data.size());
        // key listing excludes the DIR record and honours filters
        auto* list = new cGZPersistResourceKeyList();
        list->AddRef();
        cGZPersistResourceKeyFilterByType byType(0x11);
        byType.AddRef();
        CHECK(seg->GetResourceKeyList(list, &byType) == 2 && list->Size() == 2);
        list->Release();
        seg->Close();
        seg->Shutdown();
        seg->Release();
    }

    {   // independent reader (DBPFReader) agrees
        dbpf::Package pkg;
        CHECK(pkg.Open(path));
        for (const Rec& r : recs) {
            const dbpf::Entry* e = pkg.Find(r.key.type, r.key.group, r.key.instance);
            CHECK(e != nullptr);
            std::vector<std::uint8_t> out;
            CHECK(e && pkg.Read(*e, out) && out == r.data);
        }
    }

    {   // modify, delete, free-space reuse, compaction
        auto* seg = NewSegment(path);
        CHECK(seg->Open(true, true));
        CHECK(seg->DeleteRecord(recs[5].key));
        CHECK(!seg->TestForRecord(recs[5].key));
        cIGZPersistDBRecord* rec = nullptr;
        CHECK(seg->OpenRecord(recs[2].key, &rec, 2));
        cIGZPersistDBRecord* again = nullptr;
        CHECK(!seg->OpenRecord(recs[2].key, &again, 1));   // exclusive while open for write
        void* raw = nullptr;
        CHECK(rec->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &raw));
        auto* serial = static_cast<cIGZPersistDBSerialRecord*>(raw);
        recs[2].data = Payload(8000, 0, 99);
        CHECK(serial->SetSize(0));
        CHECK(serial->SeekAbsolute(0));
        CHECK(serial->SetFieldVoid(recs[2].data.data(), 8000));
        serial->Release();
        CHECK(seg->CloseRecord(&rec));
        std::uint32_t holes = 0, free = 0;
        CHECK(seg->GetFreeSpaceMetrics(holes, free) && holes >= 1 && free > 0);
        CHECK(seg->Flush());
        CHECK(seg->CompactDatabase());
        CHECK(seg->GetFreeSpaceMetrics(holes, free) && holes == 0);
        seg->Close();
        seg->Shutdown();
        seg->Release();
        recs.pop_back();
    }
    {
        auto* seg = NewSegment(path);
        CHECK(seg->Open(true, false));
        for (const Rec& r : recs) {
            std::vector<std::uint8_t> out(r.data.size());
            std::uint32_t cap = static_cast<std::uint32_t>(out.size());
            CHECK(seg->ReadRecord(r.key, out.data(), cap) == r.data.size());
            CHECK(out == r.data);
        }
        CHECK(!seg->TestForRecord({0x12, 0x22, 6}));
        seg->Close();
        seg->Shutdown();
        seg->Release();
    }

    // typed record fields round-trip through a package
    {
        std::string p2 = (dir / "fields.dat").string();
        auto* seg = NewSegment(p2);
        CHECK(seg->Open(true, true));
        cGZPersistResourceKey k{1, 2, 3};
        cIGZPersistDBRecord* rec = nullptr;
        CHECK(seg->CreateNewRecord(k, &rec));
        void* raw = nullptr;
        rec->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &raw);
        auto* s = static_cast<cIGZPersistDBSerialRecord*>(raw);
        CHECK(s->SetFieldUint32(0xdeadbeef) && s->SetFieldFloat32(1.5f) && s->SetFieldUint16(0x1234) &&
              s->SetFieldString(cRZString("hello")) && s->SetFieldResKey({7, 8, 9}));
        s->Release();
        seg->CloseRecord(&rec);
        seg->Close();
        CHECK(seg->Open(true, false));
        CHECK(seg->OpenRecord(k, &rec, 1));
        rec->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &raw);
        s = static_cast<cIGZPersistDBSerialRecord*>(raw);
        std::uint32_t u = 0; float f = 0; std::uint16_t h = 0; cRZString str; cGZPersistResourceKey rk;
        CHECK(s->GetFieldUint32(u) && u == 0xdeadbeef && s->GetFieldFloat32(f) && f == 1.5f &&
              s->GetFieldUint16(h) && h == 0x1234 && s->GetFieldString(str) && !std::strcmp(str.ToChar(), "hello") &&
              s->GetFieldResKey(rk) && rk.type == 7 && rk.group == 8 && rk.instance == 9);
        CHECK(!s->GetFieldUint8(*reinterpret_cast<std::uint8_t*>(&u)));   // past the end
        s->Release();
        seg->CloseRecord(&rec);
        seg->Close();
        seg->Shutdown();
        seg->Release();
    }

    // a real game package: every record agrees with the independent reader
    if (argc > 1) {
        auto* seg = NewSegment(argv[1]);
        dbpf::Package pkg;
        CHECK(pkg.Open(argv[1]));
        CHECK(seg->Open(true, false));
        int checked = 0;
        for (const auto& e : pkg.Entries()) {
            if (e.id.type == 0xe86b1eef) continue;
            cGZPersistResourceKey key{e.id.type, e.id.group, e.id.instance};
            std::vector<std::uint8_t> want;
            if (!pkg.Read(e, want)) continue;
            CHECK(seg->GetRecordSize(key) == want.size());
            std::vector<std::uint8_t> got(want.size());
            std::uint32_t cap = static_cast<std::uint32_t>(got.size());
            CHECK(seg->ReadRecord(key, got.data(), cap) == want.size());
            CHECK(got == want);
            ++checked;
        }
        std::printf("real package records verified: %d\n", checked);
        CHECK(checked > 100);
        seg->Close();
        seg->Shutdown();
        seg->Release();
    }

    // directory union: later (alphabetical) packages override earlier ones
    {
        std::filesystem::path root = dir / "plugins";
        std::filesystem::create_directories(root / "sub");
        auto build = [&](const std::filesystem::path& file, std::uint32_t instance, std::uint8_t fill) {
            auto* seg = NewSegment(file.string());
            CHECK(seg->Open(true, true));
            std::vector<std::uint8_t> data(64, fill);
            CHECK(seg->WriteRecord({5, 5, instance}, data.data(), 64));
            CHECK(seg->WriteRecord({6, 6, 77}, data.data(), 64));
            seg->Close();
            seg->Shutdown();
            seg->Release();
        };
        build(root / "a.dat", 1, 0xaa);
        build(root / "sub" / "b.DAT", 2, 0xbb);
        build(root / "c.dat", 3, 0xcc);
        std::ofstream(root / "ignored.txt") << "x";
        auto* multi = new cGZPersistDBSegmentMultiPackedFiles();
        multi->AddRef();
        multi->Init();
        CHECK(!multi->Open(true, true));                       // write is refused
        cRZString rootStr(root.string().c_str());
        multi->SetPath(rootStr);
        CHECK(multi->Open(true, false));
        CHECK(multi->GetSegmentCount() == 3);
        CHECK(multi->TestForRecord({5, 5, 1}) && multi->TestForRecord({5, 5, 3}) && !multi->TestForRecord({5, 5, 9}));
        std::vector<std::uint8_t> out(64);
        std::uint32_t cap = 64;
        CHECK(multi->ReadRecord({6, 6, 77}, out.data(), cap) == 64 && out[0] == 0xbb);  // sub/b.DAT sorts last and wins
        CHECK(multi->GetRecordSize({5, 5, 2}) == 64);
        CHECK(multi->GetRecordSize({5, 5, 99}) == 0);
        CHECK(!multi->WriteRecord({1, 1, 1}, out.data(), 64) && !multi->DeleteRecord({5, 5, 1}));
        CHECK(multi->GetRecordCount(nullptr) == 4);
        multi->Close();
        multi->Shutdown();
        multi->Release();
    }

    {   // cSCResFilterFixedGroupAndTypeId is the type-and-group filter: first field = first argument, second field = second
        cSCResFilterFixedGroupAndTypeId filter(0x6534284a, 0x89c2a517);
        cGZPersistResourceKey hit{0x6534284a, 0x89c2a517, 1}, wrongGroup{0x6534284a, 1, 1}, wrongType{1, 0x89c2a517, 1};
        CHECK(filter.IsKeyIncluded(hit) && !filter.IsKeyIncluded(wrongGroup) && !filter.IsKeyIncluded(wrongType));
    }
    std::filesystem::remove_all(dir);
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("gz_packedfile: all tests passed");
    return 0;
}

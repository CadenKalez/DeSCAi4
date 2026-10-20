// Test of cGZDBSegmentDirectoryFiles / cGZRESIDtoExtensionMap / cGZFileNameToResKeyConverterDefault with real temp files.
// Expected values are derived by hand from the decompiled logic (file naming "%8.8x" / "%8.8x%8.8x", hex-run parsing
// rules of GetKeyFromFileName, the registry category callback grammar, the AddFile / DoFileChangeCheck behaviour).
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

#include "gz/cGZDBSegmentDirectoryFiles.h"
#include "rz/nRZHash.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
void WriteAll(const std::string& path, const std::string& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
}
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
bool Exists(const std::string& path) { struct stat st; return ::stat(path.c_str(), &st) == 0; }

struct FakeRegistry : cIGZRegistryCategories {
    int calls = 0;
    bool result = true;
    std::string category;
    DirectoryFilesCategoryCallback callback = nullptr;
    void* user = nullptr;
    // Feeds entries to the registered callback the way the registry does.
    std::vector<std::pair<std::string, std::string>> entries;
    bool RegisterCategoryCallback(const char* cat, DirectoryFilesCategoryCallback cb, void* u) override {
        ++calls;
        category = cat;
        callback = cb;
        user = u;
        if (!result) return false;
        for (auto& e : entries) cb(cRZString(e.first.c_str()), cRZString(e.second.c_str()), u);
        return true;
    }
};

struct KeyList : cIGZPersistResourceKeyList {
    std::vector<cGZPersistResourceKey> keys;
    bool Insert(const cGZPersistResourceKey& k) override { keys.push_back(k); return true; }
    bool Insert(const cIGZPersistResourceKeyList&) override { return false; }
    bool Erase(const cGZPersistResourceKey&) override { return false; }
    bool EraseAll() override { keys.clear(); return true; }
    void EnumKeys(ResourceKeyEnumFn, void*) const override {}
    bool IsPresent(const cGZPersistResourceKey&) override { return false; }
    std::uint32_t Size() const override { return static_cast<std::uint32_t>(keys.size()); }
    const cGZPersistResourceKey* GetKey(std::uint32_t i) const override { return &keys[i]; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
};
struct OnlyType : cIGZPersistResourceKeyFilter {
    std::uint32_t type;
    explicit OnlyType(std::uint32_t t) : type(t) {}
    bool IsKeyIncluded(const cGZPersistResourceKey& k) override { return k.type == type; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
};

void TestExtensionMap() {
    cGZRESIDtoExtensionMap* map = new cGZRESIDtoExtensionMap(1);
    CHECK(map->Size() == 0);
    CHECK(map->AddRESIDtoExtensionAssociation(0x10, cRZString("png")));
    CHECK(map->AddRESIDtoExtensionAssociation(0x20, cRZString("Exemplar")));
    CHECK(map->AddRESIDtoExtensionAssociation(0x10, cRZString("jpg")));      // duplicate keys are kept
    CHECK(map->Size() == 3);
    cRZString ext;
    std::uint32_t id = 0x10;
    CHECK(map->RESIDToExtension(id, ext) && ext.str() == "png");              // first one wins
    id = 0x99;
    CHECK(!map->RESIDToExtension(id, ext));
    std::uint32_t resid = 0;
    CHECK(map->ExtensionToRESID(cRZString("EXEMPLAR"), resid) && resid == 0x20);   // case-insensitive
    CHECK(map->ExtensionToRESID(cRZString(".exemplar"), resid) && resid == 0x20);  // a leading dot is ignored
    CHECK(map->ExtensionToRESID(cRZString("jpg"), resid) && resid == 0x10);
    CHECK(!map->ExtensionToRESID(cRZString("tga"), resid));
    CHECK(!map->ExtensionToRESID(cRZString("..exemplar"), resid));
    cIGZPersistRESIDtoExtensionMap* copy = nullptr;
    CHECK(map->QueryInterface(0xa56c8cfd, reinterpret_cast<void**>(&copy)) && copy);   // AddRef -> 2
    cGZRESIDtoExtensionMap other(5);
    other.Copy(map);
    CHECK(other.Size() == 3);
    other.EraseAll();
    CHECK(other.Size() == 0);
    void* none = nullptr;
    CHECK(!map->QueryInterface(0x1234, &none));
    CHECK(copy->Release() == 1);                                              // 2 -> 1
    CHECK(map->Release() == 0);                                               // deletes
}

void TestConverter() {
    cGZFileNameToResKeyConverterDefault conv;
    cGZPersistResourceKey key{1, 2, 3};
    CHECK(!conv.FileNameToKey(cRZString("0123456789abcdef.png"), key) && key.type == 1);
    CHECK(!conv.IsFileNameValid(cRZString("12345678.png")));
    cRZString unused;
    CHECK(!conv.KeyToFileName(key, unused));
}
}  // namespace

int main() {
    setenv("TMPDIR", "/tmp", 0);   // rewrite mode copies the record to a temp file (cRZFile::MakeTempPathName)
    TestExtensionMap();
    TestConverter();

    const char* tmp = std::getenv("TMPDIR");
    const std::string base = std::string(tmp ? tmp : "/tmp") + "/sdfseg_test_" + std::to_string(::getpid());
    const std::string dir = base + "/data/";
    mkdir(base.c_str(), 0755);
    mkdir((base + "/data").c_str(), 0755);

    // ---- files -----------------------------------------------------------------------------------------------
    WriteAll(dir + "00000001.png", "AAAA");                       // instance 1, default group
    WriteAll(dir + "0000000200000009.png", "BBBBBB");             // instance 2, group 9 (prefix form)
    WriteAll(dir + "0000000300000004.tga", "C");                  // type tga
    WriteAll(dir + "notes.txt", "x");                             // not a key
    WriteAll(dir + "short.png", "x");                             // < 8 chars of hex -> not a key (text keys off)

    // ---- registry / services ----------------------------------------------------------------------------------
    FakeRegistry registry;
    registry.entries = {{"00000007", " 11 :png, 22:TGA ,"}, {"00000008", "33:zzz"}};
    DirectoryFilesServices services;
    services.registry = &registry;
    services.dataDirectory = [&]() -> const char* { return base.c_str(); };
    services.languageDirectory = [](cRZString& out) { out.AssignFromCString("English"); };
    SetDirectoryFilesServices(services);

    {
        auto* seg = new cGZDBSegmentDirectoryFiles();
        seg->AddRef();
        CHECK(!seg->IsOpen() && seg->GetSegmentID() == 0 && seg->GroupId() == 0);
        CHECK(!seg->Open(true, true));                                     // not initialised
        CHECK(!seg->DoFileChangeCheck());

        // Init(): "%DataDirectory%" is replaced; the text in front is dropped.
        cRZString path("ignored%DataDirectory%/data");
        CHECK(seg->Init(7, &path, false));
        CHECK(registry.calls == 2 && registry.category == "CN_DBSDF");     // Init + Open
        CHECK(seg->IsOpen());
        CHECK(seg->GetSegmentID() == 7 && seg->GroupId() == 7);
        CHECK(seg->Path().str() == dir.substr(0, dir.size() - 1) + "/" || seg->Path().str() == base + "/data/");
        CHECK(!seg->Init(7, &path, false));                                // already initialised
        // the callback fed the extension map for group 7 only
        std::uint32_t resid = 0;
        CHECK(seg->ExtensionMap()->ExtensionToRESID(cRZString("png"), resid) && resid == 0x11);
        CHECK(seg->ExtensionMap()->ExtensionToRESID(cRZString("tga"), resid) && resid == 0x22);
        CHECK(!seg->ExtensionMap()->ExtensionToRESID(cRZString("zzz"), resid));
        CHECK(seg->ExtensionMap()->Size() == 4);   // Init and Open both feed the multimap from the registry (no duplicate check, Open does not clear it)

        // BuildKeyList read the directory: type png (0x11) group 7 inst 1; png group 9 inst 2; tga g=4 inst 3
        // (the group of "0000000300000004" is 4).
        KeyList keys;
        CHECK(seg->GetResourceKeyList(&keys, nullptr) == 3);
        // ordering is (type, group, instance): png(0x11) 7:1 and 9:2 first, then tga(0x22) 4:3
        CHECK(keys.keys.size() == 3);
        CHECK(keys.keys[0].type == 0x11 && keys.keys[0].group == 7 && keys.keys[0].instance == 1);
        CHECK(keys.keys[1].type == 0x11 && keys.keys[1].group == 9 && keys.keys[1].instance == 2);
        CHECK(keys.keys[2].type == 0x22 && keys.keys[2].group == 4 && keys.keys[2].instance == 3);
        OnlyType onlyTga(0x22);
        CHECK(seg->GetRecordCount(&onlyTga) == 1 && seg->GetRecordCount(nullptr) == 3);

        // sizes / reads
        cGZPersistResourceKey k1{0x11, 7, 1}, k2{0x11, 9, 2}, kMissing{0x11, 7, 99};
        CHECK(seg->TestForRecord(k1) && !seg->TestForRecord(kMissing));
        CHECK(seg->GetRecordSize(k1) == 4 && seg->GetRecordSize(k2) == 6 && seg->GetRecordSize(kMissing) == 0xffffffffu);
        char buf[16] = {};
        std::uint32_t size = 16;
        CHECK(seg->ReadRecord(k2, buf, size) == 6 && size == 6 && std::string(buf, 6) == "BBBBBB");
        size = 3;
        CHECK(seg->ReadRecord(k2, buf, size) == 3 && size == 3 && std::string(buf, 3) == "BBB");   // clamped to the capacity
        size = 16;
        CHECK(seg->ReadRecord(k2, nullptr, size) == 6 && size == 0);            // size query
        size = 16;
        CHECK(seg->ReadRecord(kMissing, buf, size) == 0xffffffffu && size == 0);
        CHECK(!seg->WriteRecord(k1, buf, 1));                                   // not implemented

        // names
        cRZString name;
        CHECK(seg->GetFileNameFromKey(k1, name) && name.str() == "00000001.png");
        CHECK(!seg->GetFileNameFromKey(kMissing, name));
        cRZString full;
        CHECK(seg->GetFilePathFromKey(k2, full) && full.str() == seg->Path().str() + "0000000200000009.png");
        cRZString shortName, longName, ext;
        cGZPersistResourceKey tmpKey{0x11, 7, 0xABCDEF};                        // default group -> "%8.8x"
        CHECK(seg->getFileNameFromKey(tmpKey, shortName, ext) && shortName.str() == "00abcdef" && ext.str() == "png");
        tmpKey.group = 5;                                                       // other group -> instance then group
        CHECK(seg->getFileNameFromKey(tmpKey, longName, ext) && longName.str() == "00abcdef00000005");
        tmpKey.type = 0x77;                                                     // type without extension
        CHECK(!seg->getFileNameFromKey(tmpKey, longName, ext));

        // key parsing rules
        cGZPersistResourceKey parsed{};
        CHECK(seg->GetKeyFromFileName(cRZString("0000abcd"), cRZString("png"), parsed) &&
              parsed.type == 0x11 && parsed.group == 7 && parsed.instance == 0xabcd);
        CHECK(seg->GetKeyFromFileName(cRZString("deadbeef00000002"), cRZString(".PNG"), parsed) &&
              parsed.group == 2 && parsed.instance == 0xdeadbeef);
        CHECK(seg->GetKeyFromFileName(cRZString("0000abcdXYZ"), cRZString("png"), parsed) && parsed.instance == 0xabcd);   // prefix
        CHECK(!seg->GetKeyFromFileName(cRZString("0000abc"), cRZString("png"), parsed));      // 7 digits
        CHECK(!seg->GetKeyFromFileName(cRZString("zz0000abcd"), cRZString("png"), parsed));   // suffix form is off
        CHECK(!seg->GetKeyFromFileName(cRZString("00000001"), cRZString("bad"), parsed));     // unknown extension
        seg->SetAllowedIDPositions(false, true);
        CHECK(seg->GetKeyFromFileName(cRZString("zz0000abcd"), cRZString("png"), parsed) && parsed.instance == 0xabcd && parsed.group == 7);
        CHECK(seg->GetKeyFromFileName(cRZString("pp1111111122222222"), cRZString("png"), parsed) &&
              parsed.instance == 0x11111111 && parsed.group == 0x22222222);                   // 16 digits: instance then group
        CHECK(!seg->GetKeyFromFileName(cRZString("0000abcdXY"), cRZString("png"), parsed));   // prefix form now off, no trailing hex
        seg->SetAllowedIDPositions(true, false);
        seg->SetAllowTextAsKey(true);
        CHECK(seg->GetKeyFromFileName(cRZString("notes"), cRZString("png"), parsed) &&
              parsed.instance == (nRZHash::CRC24("notes", false) | 0xff000000u));
        seg->SetAllowTextAsKey(false);
        CHECK(seg->IsFileNameValid(cRZString("/some/dir/00000005.png")) && seg->IsFileNameValid(cRZString("00000005.TGA")));
        CHECK(!seg->IsFileNameValid(cRZString("00000005.exe")) && !seg->IsFileNameValid(cRZString("nothing")));

        // GenerateNewKey: counts up from 1 and skips used instances (type 0x11 group 7 instance 1 exists)
        cGZPersistResourceKey generated{};
        CHECK(seg->GenerateNewKey(0x11, generated) && generated.type == 0x11 && generated.group == 7 && generated.instance == 2);
        CHECK(seg->GenerateNewKey(0x11, generated) && generated.instance == 3);

        // open for read: two readers share, a writer is refused; reading a record being written is refused
        cIGZPersistDBRecord* r1 = nullptr;
        cIGZPersistDBRecord* r2 = nullptr;
        CHECK(seg->OpenRecord(k1, &r1, 1) && r1);
        CHECK(seg->OpenRecord(k1, &r2, 1) && r2);
        CHECK(!seg->DeleteRecord(k1));                                           // open handles
        cIGZPersistDBRecord* w = nullptr;
        CHECK(!seg->OpenRecord(k1, &w, 2) && !w);
        CHECK(r1->GetSize() == 4);
        char rb[4] = {};
        CHECK(r1->GetFieldVoid(rb, 4) && std::string(rb, 4) == "AAAA");
        CHECK(seg->CloseRecord(&r1) && seg->CloseRecord(&r2));
        CHECK(!seg->OpenRecord(kMissing, &r1, 1));                               // reading a missing key fails
        CHECK(!seg->OpenRecord(k1, &r1, 0));

        // rewrite: backup copy, new content replaces the file
        CHECK(seg->OpenRecord(k1, &w, 2) && w);
        CHECK(seg->GetRecordSize(k1) == 0);                                      // truncated, writer knows its size
        CHECK(w->SetFieldVoid("NEWDATA", 7));
        CHECK(seg->GetRecordSize(k1) == 7);
        size = 16;
        CHECK(seg->ReadRecord(k1, buf, size) == 0xffffffffu && size == 0);       // single writer blocks reads
        cIGZPersistDBRecord* rd = nullptr;
        CHECK(!seg->OpenRecord(k1, &rd, 1));                                     // writer present
        CHECK(seg->CloseRecord(&w));
        CHECK(ReadAll(dir + "00000001.png") == "NEWDATA");

        // rewrite then abort restores the original
        CHECK(seg->OpenRecord(k2, &w, 2) && w);
        CHECK(w->SetFieldVoid("zz", 2));
        CHECK(seg->AbortRecord(&w));
        CHECK(ReadAll(dir + "0000000200000009.png") == "BBBBBB");

        // create: the key is not yet in the segment; the file must already exist (no create bit) -> fails otherwise
        cGZPersistResourceKey fresh{0x11, 7, 0x55};
        cIGZPersistDBRecord* c = nullptr;
        CHECK(!seg->CreateNewRecord(fresh, &c) && !c);
        CHECK(!seg->TestForRecord(fresh));
        WriteAll(dir + "00000055.png", "");
        CHECK(seg->CreateNewRecord(fresh, &c) && c);
        CHECK(c->SetFieldVoid("fresh", 5));
        CHECK(seg->TestForRecord(fresh));
        CHECK(seg->CloseRecord(&c));
        CHECK(ReadAll(dir + "00000055.png") == "fresh");
        CHECK(seg->GetRecordSize(fresh) == 5);

        // delete removes the file
        CHECK(seg->DeleteRecord(fresh) && !seg->TestForRecord(fresh) && !Exists(dir + "00000055.png"));
        CHECK(!seg->DeleteRecord(fresh));

        // EnumerateRecordFiles
        CHECK(seg->EnumerateRecordFiles(nullptr, 0) == 3);
        cRZString a, b;
        cIGZString* slots[3] = {&a, &b, nullptr};
        CHECK(seg->EnumerateRecordFiles(slots, 5) == 2);                          // stops at the first null slot
        CHECK(a.str() == "00000001.png" && b.str() == "0000000200000009.png");
        CHECK(seg->EnumerateRecordFiles(slots, 1) == 1);

        // AddFile: adding answers false but registers; removal answers true
        WriteAll(dir + "00000009.png", "n");
        CHECK(!seg->AddFile(cRZString("00000009.png"), true));
        cGZPersistResourceKey added{0x11, 7, 9};
        CHECK(seg->TestForRecord(added));
        CHECK(seg->AddFile(cRZString("00000009.png"), false) && !seg->TestForRecord(added));
        CHECK(!seg->AddFile(cRZString("00000009.png"), false));

        // file change check: a new file appears, an old one disappears
        WriteAll(dir + "0000000A.PNG", "n");                                      // upper case extension, new
        std::remove((dir + "0000000300000004.tga").c_str());                      // gone
        CHECK(seg->DoFileChangeCheck());
        CHECK(seg->TestForRecord({0x11, 7, 0xA}));
        CHECK(!seg->TestForRecord({0x22, 4, 3}));
        CHECK(!seg->DoFileChangeCheck());                                         // nothing changed now

        // change-detection switch
        CHECK(!seg->GetFileChangeDetection());
        CHECK(seg->SetFileChangeDetection(false, 1));                             // interval clamps to 15
        CHECK(seg->SetFileChangeDetection(true, 300));                            // arms the timer (one-shot, 300..600 s)
        CHECK(seg->GetFileChangeDetection());
        CHECK(seg->SetFileChangeDetection(false, 0));
        CHECK(!seg->GetFileChangeDetection());

        // extension map copy-out
        cIGZPersistRESIDtoExtensionMap* mapCopy = nullptr;
        CHECK(seg->GetRESIDtoExtensionMap(&mapCopy) && mapCopy && mapCopy->Size() == 4);   // (Init and Open each fed the map)
        mapCopy->Release();
        seg->AddExtensionAssociation(0x44, cRZString("dat"));
        CHECK(seg->ExtensionMap()->Size() == 5);

        // interfaces
        cIGZPersistDBSegmentDirectoryFiles* dirIf = nullptr;
        CHECK(seg->QueryInterface(0x856c8cf7, reinterpret_cast<void**>(&dirIf)) && dirIf);
        CHECK(dirIf->AsIDBSegment() == static_cast<cIGZPersistDBSegment*>(seg));
        dirIf->Release();
        cIGZPersistDBSegment* segIf = nullptr;
        CHECK(seg->QueryInterface(0x656b8f0c, reinterpret_cast<void**>(&segIf)) && segIf == static_cast<cIGZPersistDBSegment*>(seg));
        segIf->Release();
        CHECK(seg->Flush());

        // SetPath only while closed
        CHECK(!seg->SetPath(cRZString("/elsewhere")));
        CHECK(seg->Close() && !seg->IsOpen());
        CHECK(!seg->TestForRecord(k1));                                           // everything dropped
        CHECK(seg->SetPath(cRZString("a\\b")) && seg->Path().str() == "a/b/");
        cRZString got;
        seg->GetPath(got);
        CHECK(got.str() == "a/b/");
        CHECK(seg->Shutdown());
        CHECK(seg->Path().str().empty() && seg->GroupId() == 0 && seg->GetSegmentID() == 0);   // Shutdown clears +0x68 and +0x6c together
        CHECK(seg->Release() == 0);                                               // FinalRelease deletes
    }

    // ---- Init variations ---------------------------------------------------------------------------------------
    {
        services.registry = nullptr;                                              // no registry service -> still works
        SetDirectoryFilesServices(services);
        auto* seg = new cGZDBSegmentDirectoryFiles();
        seg->AddRef();
        // "%Language%" is replaced by the language directory (prefix kept)
        mkdir((base + "/English").c_str(), 0755);
        cRZString path((base + "/%Language%").c_str());
        CHECK(seg->Init(3, &path, true));
        CHECK(seg->Path().str() == base + "/English/");
        CHECK(seg->IsOpen() && seg->GetRecordCount(nullptr) == 0);
        CHECK(seg->Release() == 0);
    }
    {
        // a failing registry registration returns false from Init (and Open) but leaves it initialised
        FakeRegistry failing;
        failing.result = false;
        services.registry = &failing;
        SetDirectoryFilesServices(services);
        auto* seg = new cGZDBSegmentDirectoryFiles();
        seg->AddRef();
        cRZString path(dir.c_str());
        CHECK(!seg->Init(5, &path, false) && failing.calls == 1 && !seg->IsOpen());
        CHECK(seg->Open(true, false) == false && failing.calls == 2 && seg->IsOpen());   // flags stay set
        CHECK(seg->Release() == 0);
    }
    {
        // a missing directory: write open is refused, read open succeeds as far as the flags go but lists nothing
        services.registry = nullptr;
        SetDirectoryFilesServices(services);
        auto* seg = new cGZDBSegmentDirectoryFiles();
        seg->AddRef();
        cRZString path((base + "/nothere").c_str());
        CHECK(!seg->Init(2, &path, false) && !seg->IsOpen());                     // Open(true,true) fails -> Shutdown
        CHECK(seg->Path().str().empty());
        CHECK(seg->Init(2, &path, false) == false);
        CHECK(seg->Release() == 0);
    }

    {
        // ":filenameasinstanceid" switches text keys on for the segment of the entry's group
        FakeRegistry text;
        text.entries = {{"00000004", "11:png:filenameasinstanceid"}};
        services.registry = &text;
        SetDirectoryFilesServices(services);
        auto* seg = new cGZDBSegmentDirectoryFiles();
        seg->AddRef();
        cRZString path(dir.c_str());
        CHECK(seg->Init(4, &path, false) && seg->IsOpen());
        cGZPersistResourceKey parsed{};
        CHECK(seg->GetKeyFromFileName(cRZString("short"), cRZString("png"), parsed) &&
              parsed.type == 0x11 && parsed.group == 4 &&
              parsed.instance == (nRZHash::CRC24("short", false) | 0xff000000u));
        CHECK(seg->TestForRecord(parsed));                  // short.png was listed by BuildKeyList
        CHECK(seg->GetRecordCount(nullptr) >= 1);
        CHECK(seg->Release() == 0);
    }

    SetDirectoryFilesServices(DirectoryFilesServices{});
    std::system(("rm -rf '" + base + "'").c_str());
    if (failures == 0) std::printf("ok\n");
    return failures == 0 ? 0 : 1;
}

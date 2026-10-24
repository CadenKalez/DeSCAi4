// cSC4Nation — tests the recovered behaviour: the three vtable groups and the field offsets, the COM plumbing
// (QueryInterface ids, the Release-below-two path), StaticInit's directory strings, LoadRegions /
// CopyNewUserRegions, the preferences lookup of the active region, the economy curves and GetEconomyModifier,
// SetActiveRegion / DeleteRegion / AddNewRegion, the version-only persistence, DoMessage and ResetTutorialCities.
// Every neighbour is a fake INationHost (and a fake IRegionHost for the regions it creates).
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <list>
#include <map>
#include <string>
#include <vector>

#include "gz/cGZDate.h"
#include "rz/cRZDirectory.h"
#include "sc4/Nation.h"
#include "sc4/Region.h"
#include "sc4/SCProperty.h"
#include "sc4/SCPropertyHelp.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace fs = std::filesystem;

//---------------------------------------------------------------------------------------------------------------------
// fakes
//---------------------------------------------------------------------------------------------------------------------

struct FakeCity final : cISC4RegionalCity {
    FakeCity(const char* name, int px, int pz) : cityName(name), x(px), z(pz) {}
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 1 || iid == kGZIID_cISC4RegionalCity) { *out = this; AddRef(); return true; }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override {
        if (--refs == 0) { delete this; return 0; }
        return refs;
    }
    bool Init() override { return true; }
    bool Shutdown() override { ++shutdowns; return true; }
    void SetupCity(cISC4City*) override {}
    std::uint32_t GetCitySerialNumber() const override { return serial; }
    void GetCitySaveFilePath(cIGZString& out) const override { out.AssignFromIGZString(savePath); }

    cRZString cityName;
    cRZString savePath;
    int x = 0, z = 0;
    std::uint32_t serial = 0;
    std::int32_t tutorialGUID = -1;
    std::uint32_t refs = 1;
    int shutdowns = 0;
};

struct FakeRegionHost final : IRegionHost {
    cRZString regionDirectory, downloadDirectory;
    FakeCity* readResult = nullptr;
    std::vector<std::string> renamed, written;
    std::uint32_t serialSeed = 0x1000;

    void GetRegionDirectory(cRZString& out) override { out = regionDirectory; }
    void GetRegionDownloadDirectory(cRZString& out) override { out = downloadDirectory; }

    bool ReadRegionalCity(const cIGZString& path, cISC4RegionalCity** out) override {
        readPaths.push_back(path.ToChar());
        if (!readResult) return false;
        readResult->AddRef();
        *out = readResult;
        return true;
    }
    bool WriteRegionalCity(const cIGZString& path, cISC4RegionalCity*) override {
        written.push_back(path.ToChar());
        std::ofstream(path.ToChar()).put('x');
        return true;
    }
    void CreateSavePathForCity(const cIGZString& directory, const cIGZString& cityName, std::uint32_t serial,
                               cRZString& out) override {
        char buffer[1024];
        std::snprintf(buffer, sizeof buffer, "%s%s-%08x.sc4", directory.ToChar(), cityName.ToChar(), serial);
        out = cRZString(buffer);
    }
    void CreateSavePathForCity(const cIGZString& cityName, std::uint32_t serial, cRZString& out) override {
        CreateSavePathForCity(downloadDirectory, cityName, serial, out);
    }
    std::uint32_t CreateCitySerialNumber() override { return serialSeed; }
    int ReadBinaryRecordToFile(const cGZPersistResourceKey&, const cIGZString&) override { return -1; }
    bool HasResource(const cGZPersistResourceKey&) override { return false; }
    std::uint32_t RandomUint32UniformBelow(std::uint32_t) override { return 0; }
    void SetTutorialActive(int guid, bool active) override {
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%d:%d", guid, active ? 1 : 0);
        tutorialCalls.push_back(buffer);
    }
    FakeCity* City(const cISC4RegionalCity* city) { return static_cast<FakeCity*>(const_cast<cISC4RegionalCity*>(city)); }
    void GetCityPosition(const cISC4RegionalCity* city, int& x, int& z) override { x = City(city)->x; z = City(city)->z; }
    void GetCitySize(const cISC4RegionalCity* city, int& width, int& height) override {
        width = 1;
        height = 1;
        (void)city;
    }
    void SetCityPosition(cISC4RegionalCity* city, int x, int z, bool) override { City(city)->x = x; City(city)->z = z; }
    std::uint32_t GetCitySerialNumber(const cISC4RegionalCity* city) override { return City(city)->serial; }
    void SetCitySerialNumber(cISC4RegionalCity* city, std::uint32_t serial) override { City(city)->serial = serial; }
    void GetCitySaveFilePath(const cISC4RegionalCity* city, cRZString& out) override { out = City(city)->savePath; }
    void SetCitySaveFilePath(cISC4RegionalCity* city, const cIGZString& path) override {
        City(city)->savePath.AssignFromIGZString(path);
    }
    void GetCityName(const cISC4RegionalCity* city, cRZString& out) override { out = City(city)->cityName; }
    std::uint32_t GetTutorialGUID(const cISC4RegionalCity* city) override {
        return static_cast<std::uint32_t>(City(city)->tutorialGUID);
    }
    void SetTutorialGUID(cISC4RegionalCity* city, std::uint32_t guid) override {
        City(city)->tutorialGUID = static_cast<std::int32_t>(guid);
    }

    std::vector<std::string> readPaths, tutorialCalls;
};

struct FakeNationHost final : INationHost {
    cRZString regionDirectory, dataDirectory, languageDirectory;
    std::vector<std::uint8_t> preferences = std::vector<std::uint8_t>(0x1734, 0);
    bool applicationPresent = true, cityPresent = false, firstRun = false, regionScreenCurrent = false;
    int tutorialLanguageIndex = 3, difficulty = 0, lastTutorialLanguage = -1;
    std::list<cRZString> tutorialDirectories;
    cRZString defaultTutorialDirectory;
    std::map<std::string, std::list<NationTutorialItem>> tutorialItems;
    sc4::cISCPropertyHolder* exemplar = nullptr;
    cGZDate simDate{1u, 1u, 2000u};
    FakeCity* readCity = nullptr;
    int readBinaryRecordResult = 0;
    int failFirstReads = 0;
    int destroyedScreens = 0, createdScreens = 0;

    std::vector<std::uint32_t> added, removed;
    std::vector<std::string> records, tutorialActive, citySavePaths, cityPositions;

    bool ApplicationPresent() override { return applicationPresent; }
    void RegionsDirectory(cIGZString& out) override { out.AssignFromIGZString(regionDirectory); }
    std::uint8_t* Preferences() override { return preferences.data(); }
    bool IsRunFirstTimeAfterInstall() override { return firstRun; }
    int TutorialLanguageIndex() override { return tutorialLanguageIndex; }
    void TutorialRegionDirectoryNames(std::list<cRZString>& out) override { out = tutorialDirectories; }
    cRZString TutorialRegionDefaultDirName(int languageIndex) override {
        lastTutorialLanguage = languageIndex;
        return defaultTutorialDirectory;
    }
    void TutorialItems(std::list<NationTutorialItem>& out, cISC4Region* region) override {
        const cRZString name(*region->GetDirectoryName());
        auto it = tutorialItems.find(name.ToChar());
        if (it != tutorialItems.end()) out = it->second;
    }
    void SetTutorialActive(std::int32_t guid, bool active) override {
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%d:%d", guid, active ? 1 : 0);
        tutorialActive.push_back(buffer);
    }
    bool IsRegionScreenCurrent() override { return regionScreenCurrent; }
    void DestroyCurrentScreen() override { ++destroyedScreens; }
    void CreateRegionScreen() override { ++createdScreens; }
    void SimStartDate(cIGZDate& out) override { out.Set(1u, 1u, 2000u); }
    cIGZDate* SimDate() override { return &simDate; }
    bool CityPresent() override { return cityPresent; }
    int CityDifficultyLevel() override { return difficulty; }
    sc4::cISCPropertyHolder* LoadExemplar(const cGZPersistResourceKey& key) override {
        lastExemplarKey = key;
        return exemplar;
    }
    void ReleaseExemplar(sc4::cISCPropertyHolder*) override {}
    void AddNotification(cIGZMessageTarget2*, std::uint32_t message) override { added.push_back(message); }
    void RemoveNotification(cIGZMessageTarget2*, std::uint32_t message) override { removed.push_back(message); }
    void DataDirectory(cIGZString& out) override { out.AssignFromIGZString(dataDirectory); }
    bool LanguageDirectoryName(cIGZString& out, std::uint32_t language) override {
        out.AssignFromIGZString(languageDirectory);
        languageSeen = static_cast<int>(language);
        return languageDirectory.Strlen() != 0;
    }
    int ReadBinaryRecord(const cGZPersistResourceKey& key, const cIGZString& path) override {
        lastRecordKey = key;
        records.push_back(path.ToChar());
        if (failFirstReads > 0) {
            --failFirstReads;
            return -1;
        }
        if (readBinaryRecordResult == -1) return -1;
        std::ofstream(path.ToChar()).put('x');
        return readBinaryRecordResult;
    }
    bool ReadRegionalCity(const cIGZString& path, cISC4RegionalCity*& out) override {
        readPaths.push_back(path.ToChar());
        if (!readCity) return false;
        readCity->AddRef();
        out = readCity;
        return true;
    }
    void CityPosition(cISC4RegionalCity* city, int& x, int& z) override {
        x = static_cast<FakeCity*>(city)->x;
        z = static_cast<FakeCity*>(city)->z;
        char buffer[64];
        std::snprintf(buffer, sizeof buffer, "%d,%d", x, z);
        cityPositions.push_back(buffer);
    }
    void SetCitySaveFilePath(cISC4RegionalCity* city, const cIGZString& path) override {
        static_cast<FakeCity*>(city)->savePath.AssignFromIGZString(path);
        citySavePaths.push_back(path.ToChar());
    }
    std::int32_t CityTutorialGUID(cISC4RegionalCity* city) override {
        return static_cast<FakeCity*>(city)->tutorialGUID;
    }

    cGZPersistResourceKey lastExemplarKey{}, lastRecordKey{};
    int languageSeen = -1;
    std::vector<std::string> readPaths;
};

//---------------------------------------------------------------------------------------------------------------------
// helpers
//---------------------------------------------------------------------------------------------------------------------

static void WriteFile(const fs::path& path, const char* text) {
    std::ofstream out(path);
    out << text;
}

static bool FileExists(const fs::path& path) { return fs::exists(path); }

static void ResetStatics() {
    cSC4Nation::msRegionDirectory = cRZString();
    cSC4Nation::msRegionDownloadDirectory = cRZString();
    cSC4Nation::mbStaticInitialized = false;
}

// A region directory with "Alpha" and "Beta" (both with a region.ini) plus the "Downloads" sub-directory.
static void MakeRegionTree(const fs::path& root, const char* first = "Alpha", const char* second = "Beta") {
    fs::create_directories(root / first);
    fs::create_directories(root / second);
    fs::create_directories(root / "Downloads");
    WriteFile(root / first / "region.ini", "[Regional Settings]\nMinWaterHeight=40\nMaxWaterHeight=100\n");
    WriteFile(root / second / "region.ini", "[Regional Settings]\nMinWaterHeight=41\nMaxWaterHeight=101\n");
}

static int IndexOfRegion(cISC4Nation& nation, const char* directoryName) {
    for (std::uint32_t i = 0; i < nation.GetRegionCount(); ++i) {
        cISC4Region* region = nation.GetRegion(i);
        if (region && std::strcmp(region->GetDirectoryName()->ToChar(), directoryName) == 0) return static_cast<int>(i);
    }
    return -1;
}

static std::uint32_t CountNotifications(const std::vector<std::uint32_t>& values, std::uint32_t message) {
    std::uint32_t count = 0;
    for (std::uint32_t value : values) if (value == message) ++count;
    return count;
}

//---------------------------------------------------------------------------------------------------------------------
// the vtable groups: the number of function slots in each group, read straight out of the objects' vtables.
//---------------------------------------------------------------------------------------------------------------------

// The words of one vtable group: [offset-to-top][typeinfo][slots...]. The compiler emits the class's three
// groups one after another, so the primary group's first 23 slots are followed by the -8 group's header and the
// +8 view's 4 slots, then by the -0x10 group's header and the +0x10 view's 6 slots.
static const std::int64_t* VtableWords(void** addressPoint) {
    return reinterpret_cast<const std::int64_t*>(addressPoint);
}

int main() {
    const fs::path root = fs::temp_directory_path() / "sc4_nation_test";
    fs::remove_all(root);
    fs::create_directories(root);

    // ---------------------------------------------------------------------------------------------------------
    // 0. no host at all (the boot-probe case): Init degrades to an empty nation and must not crash
    // ---------------------------------------------------------------------------------------------------------
    {
        ResetStatics();
        const fs::path directory = root / "scenario0";
        fs::create_directories(directory / "alpha");
        WriteFile(directory / "alpha" / "region.ini", "[Regional Settings]\n");
        SetNationHost(nullptr);
        FakeRegionHost regionHost;
        regionHost.regionDirectory = cRZString((directory.string() + "/").c_str());
        SetRegionHost(&regionHost);
        cSC4Nation::msRegionDirectory = regionHost.regionDirectory;   // what StaticInit would have set

        cSC4Nation nation;
        CHECK(nation.Init());                     // no INationHost: everything outside the class stays empty
        CHECK(nation.GetRegionCount() == 1);      // LoadRegions still works, the regions are recovered code
        CHECK(nation.GetRegion(0) != nullptr);
        CHECK(nation.GetActiveRegionIndex() == 0);
        CHECK(nation.GetActiveRegion() == nation.GetRegion(0));
        CHECK(nation.GetEconomyModifier(0) == 0.0f);
        int index = -1;
        CHECK(!nation.GetIndexOfCurrentRegionSettingInPreferences(index));
        CHECK(nation.DoMessage(nullptr));
        CHECK(nation.Tunables().size() == 3);
        CHECK(nation.Shutdown());
    }

    // ---------------------------------------------------------------------------------------------------------
    // 1. layout and COM plumbing
    // ---------------------------------------------------------------------------------------------------------
    {
        const fs::path directory = root / "scenario1";
        fs::create_directories(directory / "alpha");
        WriteFile(directory / "alpha" / "region.ini", "[Regional Settings]\n");
        FakeNationHost host;
        host.applicationPresent = false;   // StaticInit stays out of the host
        host.regionDirectory = cRZString((directory.string() + "/").c_str());
        SetNationHost(&host);
        FakeRegionHost regionHost;
        regionHost.regionDirectory = host.regionDirectory;
        SetRegionHost(&regionHost);
        cSC4Nation::msRegionDirectory = host.regionDirectory;

        CHECK(sizeof(cSC4Nation) == 0x70);

        cSC4Nation* nation = new cSC4Nation();
        CHECK(nation->initialisedFieldOffset() == 0x18);
        CHECK(nation->refCountFieldOffset() == 0x1c);
        CHECK(nation->regionsFieldOffset() == 0x20);
        CHECK(nation->activeRegionIndexFieldOffset() == 0x38);
        CHECK(nation->economyCurvesFieldOffset() == 0x40);
        CHECK(nation->tunablesFieldOffset() == 0x58);
        CHECK(nation->GetActiveRegionIndex() == 0);   // the ctor zeroes the active index; Init sets -1
        CHECK(nation->GetRegionCount() == 0);
        CHECK(nation->GetRegionDirectory() == &cSC4Nation::msRegionDirectory);
        CHECK(nation->GetRegionDownloadDirectory() == &cSC4Nation::msRegionDownloadDirectory);
        CHECK(nation->GetGZCLSID() == 0xc9dc0477);

        // the three base subobjects sit at +0, +8 and +0x10 (the message target is the +8 view)
        const char* base = reinterpret_cast<const char*>(nation);
        CHECK(reinterpret_cast<const char*>(static_cast<cISC4Nation*>(nation)) - base == 0x00);
        CHECK(reinterpret_cast<const char*>(static_cast<cIGZMessageTarget2*>(nation)) - base == 0x08);
        CHECK(reinterpret_cast<const char*>(static_cast<cIGZSerializable*>(nation)) - base == 0x10);

        // the primary group has 23 function slots before the next group's -8 header, the +8 view 4, the +0x10
        // view 6 (the original's vtable 0x100a8ff10)
        void** primary = *reinterpret_cast<void***>(nation);
        void** messageView = *reinterpret_cast<void***>(static_cast<cIGZMessageTarget2*>(nation));
        void** serialView = *reinterpret_cast<void***>(static_cast<cIGZSerializable*>(nation));
        const std::int64_t* words = VtableWords(primary);
        CHECK(words[-2] == 0);                     // offset-to-top of the primary group
        const std::int64_t typeinfo = words[-1];
        CHECK(typeinfo != 0);
        CHECK(words[23] == -8);                    // the +8 group's offset-to-top
        CHECK(words[24] == typeinfo);
        CHECK(messageView == primary + 25);        // 2 header words + 23 slots
        CHECK(words[29] == -0x10);                 // the +0x10 group's offset-to-top
        CHECK(words[30] == typeinfo);
        CHECK(serialView == primary + 31);

        // QueryInterface: 1 and 0x89d2c366 answer the object, 0x452294aa the +8 view, 0xe4fda3d4 nothing
        void* out = nullptr;
        CHECK(nation->QueryInterface(1, &out) && out == static_cast<cISC4Nation*>(nation));
        CHECK(nation->RefCount() == 1);
        out = nullptr;
        CHECK(nation->QueryInterface(0x89d2c366, &out) && out == static_cast<cISC4Nation*>(nation));
        CHECK(nation->RefCount() == 2);
        out = nullptr;
        CHECK(nation->QueryInterface(0x452294aa, &out) && out == static_cast<cIGZMessageTarget2*>(nation));
        CHECK(nation->RefCount() == 3);
        out = reinterpret_cast<void*>(1);
        CHECK(!nation->QueryInterface(0xe4fda3d4, &out));
        CHECK(!nation->QueryInterface(0x12345678, &out));
        CHECK(nation->AddRef() == 4);
        CHECK(nation->Release() == 3);

        // Release below two shuts the object down and deletes it
        CHECK(nation->Init());
        CHECK(host.added.size() == 3);     // the fake host always has a message server
        while (nation->RefCount() > 1) nation->Release();
        CHECK(nation->Release() == 0);     // Shutdown + delete this
    }

    // ---------------------------------------------------------------------------------------------------------
    // 2. StaticInit / LoadRegions
    // ---------------------------------------------------------------------------------------------------------
    {
        ResetStatics();
        SetRegionHost(nullptr);
        const fs::path directory = root / "scenario2";
        fs::create_directories(directory);
        MakeRegionTree(directory);

        FakeNationHost host;
        host.regionDirectory = cRZString((directory.string() + "/").c_str());
        SetNationHost(&host);

        cSC4Nation nation;
        CHECK(nation.StaticInit());
        CHECK(std::string(cSC4Nation::msRegionDirectory.ToChar()) == directory.string() + "/");
        CHECK(std::string(cSC4Nation::msRegionDownloadDirectory.ToChar()) == directory.string() + "/Downloads/");
        CHECK(FileExists(directory / "Downloads"));

        // LoadRegions finds the two region directories and skips "Downloads"
        CHECK(nation.LoadRegions());
        CHECK(nation.GetRegionCount() == 2);
        bool alpha = false, beta = false;
        for (std::uint32_t i = 0; i < nation.GetRegionCount(); ++i) {
            cISC4Region* region = nation.GetRegion(i);
            CHECK(region != nullptr);
            const std::string name(region->GetDirectoryName()->ToChar());
            if (name == "Alpha") {
                alpha = true;
                CHECK(std::string(region->GetName()->ToChar()) == "Alpha");
            }
            if (name == "Beta") beta = true;
        }
        CHECK(alpha && beta);
        CHECK(nation.GetRegion(7) == nullptr);

        // a second LoadRegions appends the same two regions (the original does not clear the vector)
        CHECK(nation.LoadRegions());
        CHECK(nation.GetRegionCount() == 4);
        nation.Shutdown();
    }

    // ---------------------------------------------------------------------------------------------------------
    // 3. Init: the active region comes from the preferences, the messages are registered, a second Init is a no-op
    // ---------------------------------------------------------------------------------------------------------
    {
        ResetStatics();
        SetRegionHost(nullptr);
        const fs::path directory = root / "scenario3";
        fs::create_directories(directory);
        MakeRegionTree(directory);

        FakeNationHost host;
        host.regionDirectory = cRZString((directory.string() + "/").c_str());
        host.defaultTutorialDirectory = cRZString("Beta");
        std::snprintf(reinterpret_cast<char*>(host.preferences.data() + 0xebc), 0x40, "Beta");
        SetNationHost(&host);

        cSC4Nation nation;
        CHECK(nation.Init());
        CHECK(nation.GetRegionCount() == 2);
        CHECK(nation.GetActiveRegionIndex() >= 0);
        CHECK(nation.GetActiveRegion() != nullptr);
        CHECK(std::string(nation.GetActiveRegion()->GetName()->ToChar()) == "Beta");
        CHECK(host.lastTutorialLanguage == 3);
        CHECK(CountNotifications(host.added, kNationMessage1) == 1);
        CHECK(CountNotifications(host.added, kNationMessage2) == 1);
        CHECK(CountNotifications(host.added, kNationResetTutorialCitiesMessage) == 1);

        // the second Init returns at once
        CHECK(nation.Init());
        CHECK(CountNotifications(host.added, kNationMessage1) == 1);

        // Write / Read: a single u16 version word, and a wrong version fails
        {
            struct FakeOStream final : cIGZOStream {
                std::vector<std::uint16_t> words;
                bool error = false;
                bool QueryInterface(std::uint32_t, void**) override { return false; }
                std::uint32_t AddRef() override { return 1; }
                std::uint32_t Release() override { return 0; }
                void Flush() override {}
                bool SetSint8(std::int8_t) override { return true; }
                bool SetUint8(std::uint8_t) override { return true; }
                bool SetSint16(std::int16_t) override { return true; }
                bool SetUint16(std::uint16_t v) override { words.push_back(v); return true; }
                bool SetSint32(std::int32_t) override { return true; }
                bool SetUint32(std::uint32_t) override { return true; }
                bool SetSint64(std::int64_t) override { return true; }
                bool SetUint64(std::uint64_t) override { return true; }
                bool SetFloat32(float) override { return true; }
                bool SetFloat64(double) override { return true; }
                bool SetRZCharStr(const char*) override { return true; }
                bool SetGZStr(const cIGZString&) override { return true; }
                bool SetGZSerializable(const cIGZSerializable&) override { return true; }
                bool SetVoid(const void*, std::uint32_t) override { return true; }
                bool GetError() const override { return error; }
                void SetUserData(cIGZVariant*) override {}
                cIGZVariant* GetUserData() override { return nullptr; }
            } stream;
            CHECK(nation.Write(stream));
            CHECK(stream.words.size() == 1 && stream.words[0] == 1);
        }
        {
            struct FakeIStream final : cIGZIStream {
                std::vector<std::uint16_t> words;
                bool error = false;
                bool QueryInterface(std::uint32_t, void**) override { return false; }
                std::uint32_t AddRef() override { return 1; }
                std::uint32_t Release() override { return 0; }
                bool Skip(std::uint32_t) override { return true; }
                bool GetSint8(std::int8_t&) override { return true; }
                bool GetUint8(std::uint8_t&) override { return true; }
                bool GetSint16(std::int16_t&) override { return true; }
                bool GetUint16(std::uint16_t& v) override {
                    if (words.empty()) { error = true; return false; }
                    v = words.front();
                    words.erase(words.begin());
                    return true;
                }
                bool GetSint32(std::int32_t&) override { return true; }
                bool GetUint32(std::uint32_t&) override { return true; }
                bool GetSint64(std::int64_t&) override { return true; }
                bool GetUint64(std::uint64_t&) override { return true; }
                bool GetFloat32(float&) override { return true; }
                bool GetFloat64(double&) override { return true; }
                bool GetRZCharStr(char*, std::uint32_t) override { return true; }
                bool GetGZStr(cIGZString&) override { return true; }
                bool GetGZSerializable(cIGZSerializable&) override { return true; }
                bool GetVoid(void*, std::uint32_t) override { return true; }
                bool GetError() const override { return error; }
                void SetUserData(cIGZVariant*) override {}
                cIGZVariant* GetUserData() override { return nullptr; }
            } good, bad;
            good.words = {1};
            CHECK(nation.Read(good));
            bad.words = {2};
            CHECK(!nation.Read(bad));
        }

        // DoMessage: only the reset-tutorial-cities message does something (the registry is empty here)
        {
            struct FakeMessage final : cIGZMessage2 {
                explicit FakeMessage(std::uint32_t t) : type(t) {}
                bool QueryInterface(std::uint32_t, void**) override { return false; }
                std::uint32_t AddRef() override { return 1; }
                std::uint32_t Release() override { return 0; }
                bool Create(std::uint32_t, void**) const override { return false; }
                std::uint32_t GetType() const override { return type; }
                void SetType(std::uint32_t t) override { type = t; }
                bool IsEqual(const cIGZMessage2&) const override { return false; }
                bool IsLess(const cIGZMessage2&) const override { return false; }
                std::uint32_t type;
            } other(0x1234), reset(kNationResetTutorialCitiesMessage);
            CHECK(nation.DoMessage(&other));
            CHECK(host.destroyedScreens == 0 && host.createdScreens == 0);
            CHECK(nation.DoMessage(&reset));
            CHECK(host.destroyedScreens == 0 && host.createdScreens == 0);   // not the current screen
        }

        // Shutdown removes the three notifications again and refuses a second time
        const std::size_t removedBefore = host.removed.size();
        CHECK(nation.Shutdown());
        CHECK(host.removed.size() == removedBefore + 3);
        CHECK(CountNotifications(host.removed, kNationResetTutorialCitiesMessage) == 1);
        CHECK(nation.Shutdown());   // no further removals
        CHECK(host.removed.size() == removedBefore + 3);
    }

    // ---------------------------------------------------------------------------------------------------------
    // 4. the economy curves, the tunables and GetEconomyModifier
    // ---------------------------------------------------------------------------------------------------------
    {
        ResetStatics();
        SetRegionHost(nullptr);
        const fs::path directory = root / "scenario4";
        fs::create_directories(directory);
        MakeRegionTree(directory);

        sc4::cSCPropertyHolder holder;
        const std::vector<std::uint32_t> demandTypes{0, 3};
        sc4::cSCPropertyHelp::SetPropertyValue(&holder, kNationTuningDemandTypes, demandTypes);
        // curve 0: (0,0) (24,240)  -> 10 per month; curve 1 (key 3): (0,100) -> flat 100
        sc4::cSCPropertyHelp::SetPropertyValue(&holder, kNationTuningCurveBase,
                                              std::vector<float>{0.0f, 0.0f, 24.0f, 240.0f});
        sc4::cSCPropertyHelp::SetPropertyValue(&holder, kNationTuningCurveBase + 1,
                                              std::vector<float>{0.0f, 100.0f});
        sc4::cSCPropertyHelp::SetPropertyValue(&holder, kNationTuningTunables, std::vector<float>{1.0f, 2.0f, 3.0f});

        FakeNationHost host;
        host.regionDirectory = cRZString((directory.string() + "/").c_str());
        host.exemplar = &holder;
        host.defaultTutorialDirectory = cRZString("Alpha");
        std::snprintf(reinterpret_cast<char*>(host.preferences.data() + 0xebc), 0x40, "Alpha");
        SetNationHost(&host);

        cSC4Nation nation;
        CHECK(nation.Init());
        CHECK(host.lastExemplarKey.type == kNationTuningType);
        CHECK(host.lastExemplarKey.group == kNationTuningGroup);
        CHECK(host.lastExemplarKey.instance == kNationTuningInstance);
        CHECK(nation.EconomyCurve(0) != nullptr);
        CHECK(nation.EconomyCurve(3) != nullptr);
        CHECK(nation.EconomyCurve(9) == nullptr);
        CHECK(nation.Tunables().size() == 3);
        CHECK(nation.Tunables()[1] == 2.0f);

        // 12 months after 2000-01: curve 0 -> 120, plus the difficulty-1 tunable (2.0)
        host.cityPresent = true;
        host.simDate = cGZDate(1u, 1u, 2001u);
        host.difficulty = 1;
        CHECK(nation.GetEconomyModifier(0) == 122.0f);
        CHECK(nation.GetEconomyModifier(3) == 102.0f);    // flat 100 + 2
        // a missing demand type falls back to the curve with key 0
        CHECK(nation.GetEconomyModifier(7) == 122.0f);
        // no city: no tunable
        host.cityPresent = false;
        CHECK(nation.GetEconomyModifier(0) == 120.0f);
        // 24 months: the end of curve 0
        host.simDate = cGZDate(1u, 1u, 2002u);
        CHECK(nation.GetEconomyModifier(0) == 240.0f);
        nation.Shutdown();
    }

    // ---------------------------------------------------------------------------------------------------------
    // 5. the preferences lookup (GetIndexOfCurrentRegionSettingInPreferences)
    // ---------------------------------------------------------------------------------------------------------
    {
        ResetStatics();
        SetRegionHost(nullptr);
        const fs::path directory = root / "scenario5";
        fs::create_directories(directory);
        MakeRegionTree(directory);

        FakeNationHost host;
        host.regionDirectory = cRZString((directory.string() + "/").c_str());
        host.defaultTutorialDirectory = cRZString("nothing");
        SetNationHost(&host);

        cSC4Nation nation;
        CHECK(nation.StaticInit());
        CHECK(nation.LoadRegions());
        CHECK(nation.GetRegionCount() == 2);
        const int alphaIndex = IndexOfRegion(nation, "Alpha");
        const int betaIndex = IndexOfRegion(nation, "Beta");
        CHECK(alphaIndex >= 0 && betaIndex >= 0 && alphaIndex != betaIndex);

        int index = -1;
        CHECK(!nation.GetIndexOfCurrentRegionSettingInPreferences(index));   // empty field
        CHECK(index == 0);

        std::snprintf(reinterpret_cast<char*>(host.preferences.data() + 0xebc), 0x40, "Alpha");
        CHECK(nation.GetIndexOfCurrentRegionSettingInPreferences(index));
        CHECK(index == alphaIndex);
        std::snprintf(reinterpret_cast<char*>(host.preferences.data() + 0xebc), 0x40, "Beta");
        CHECK(nation.GetIndexOfCurrentRegionSettingInPreferences(index));
        CHECK(index == betaIndex);
        // a name with trailing garbage is not a match (the whole 0x40-byte field is compared)
        std::snprintf(reinterpret_cast<char*>(host.preferences.data() + 0xebc), 0x40, "BetaXX");
        CHECK(!nation.GetIndexOfCurrentRegionSettingInPreferences(index));
        // the field is cut at its first NUL; bytes *after* that NUL are kept, so this one does not match
        std::memset(host.preferences.data() + 0xebc, 0, 0x40);
        std::memcpy(host.preferences.data() + 0xebc, "Beta", 4);
        std::memset(host.preferences.data() + 0xebc + 4, 'x', 6);
        std::memset(host.preferences.data() + 0xebc + 10, 0, 1);
        std::memset(host.preferences.data() + 0xebc + 11, 'y', 4);
        CHECK(!nation.GetIndexOfCurrentRegionSettingInPreferences(index));
        // "Beta" followed only by NULs is the same name
        std::memset(host.preferences.data() + 0xebc, 0, 0x40);
        std::memcpy(host.preferences.data() + 0xebc, "Beta", 4);
        CHECK(nation.GetIndexOfCurrentRegionSettingInPreferences(index));
        CHECK(index == betaIndex);
    }

    // ---------------------------------------------------------------------------------------------------------
    // 6. SetActiveRegion / DeleteRegion / AddNewRegion
    // ---------------------------------------------------------------------------------------------------------
    {
        ResetStatics();
        SetRegionHost(nullptr);
        const fs::path directory = root / "scenario6";
        fs::create_directories(directory);
        MakeRegionTree(directory);
        FakeNationHost host;
        host.regionDirectory = cRZString((directory.string() + "/").c_str());
        std::snprintf(reinterpret_cast<char*>(host.preferences.data() + 0xebc), 0x40, "Alpha");
        SetNationHost(&host);
        FakeRegionHost regionHost;
        regionHost.regionDirectory = host.regionDirectory;
        regionHost.downloadDirectory = cRZString((directory.string() + "/Downloads/").c_str());
        SetRegionHost(&regionHost);

        cSC4Nation nation;
        CHECK(nation.Init());
        CHECK(nation.GetRegionCount() == 2);
        const int alphaIndex = IndexOfRegion(nation, "Alpha");
        const int betaIndex = IndexOfRegion(nation, "Beta");
        CHECK(alphaIndex >= 0 && betaIndex >= 0 && alphaIndex != betaIndex);
        // the preferences named Alpha, so that one is active
        CHECK(nation.GetActiveRegionIndex() == alphaIndex);
        CHECK(std::string(nation.GetActiveRegion()->GetDirectoryName()->ToChar()) == "Alpha");

        nation.SetActiveRegion(static_cast<std::uint32_t>(betaIndex));
        CHECK(nation.GetActiveRegionIndex() == betaIndex);
        CHECK(std::string(nation.GetActiveRegion()->GetDirectoryName()->ToChar()) == "Beta");
        nation.SetActiveRegion(static_cast<std::uint32_t>(betaIndex));   // no-op
        CHECK(nation.GetActiveRegionIndex() == betaIndex);

        // AddNewRegion creates a directory and appends the region
        const std::uint32_t added = nation.AddNewRegion(cRZString("Gamma"));
        CHECK(added == 2);
        CHECK(nation.GetRegionCount() == 3);
        CHECK(std::string(nation.GetRegion(2)->GetDirectoryName()->ToChar()) == "Gamma");
        CHECK(FileExists(directory / "Gamma"));

        // DeleteRegion removes the active region and activates the previous one
        nation.SetActiveRegion(added);
        nation.DeleteRegion(added);
        CHECK(nation.GetRegionCount() == 2);
        CHECK(nation.GetActiveRegionIndex() == static_cast<int>(added) - 1);

        // deleting a region below the active index shifts the index down; the active region stays the same
        const int lowIndex = alphaIndex < betaIndex ? alphaIndex : betaIndex;
        const int highIndex = alphaIndex < betaIndex ? betaIndex : alphaIndex;
        const std::string highName(nation.GetRegion(static_cast<std::uint32_t>(highIndex))->GetDirectoryName()->ToChar());
        nation.SetActiveRegion(static_cast<std::uint32_t>(highIndex));
        nation.DeleteRegion(static_cast<std::uint32_t>(lowIndex));
        CHECK(nation.GetRegionCount() == 1);
        CHECK(nation.GetActiveRegionIndex() == highIndex - 1);
        CHECK(std::string(nation.GetActiveRegion()->GetDirectoryName()->ToChar()) == highName);
        nation.Shutdown();
    }

    // ---------------------------------------------------------------------------------------------------------
    // 7. CopyNewUserRegions copies the data directory's "Regions" folder into the user's region directory
    // ---------------------------------------------------------------------------------------------------------
    {
        ResetStatics();
        SetRegionHost(nullptr);
        const fs::path directory = root / "scenario7";
        const fs::path data = root / "scenario7_data";
        fs::create_directories(directory);
        fs::create_directories(data / "Regions" / "Copied");
        WriteFile(data / "Regions" / "Copied" / "region.ini", "[Regional Settings]\n");
        fs::create_directories(directory / "Downloads");

        FakeNationHost host;
        host.regionDirectory = cRZString((directory.string() + "/").c_str());
        host.dataDirectory = cRZString((data.string() + "/").c_str());
        SetNationHost(&host);

        cSC4Nation nation;
        CHECK(nation.StaticInit());
        CHECK(nation.CopyNewUserRegions());
        CHECK(FileExists(directory / "Copied"));   // the language directory is empty -> the data directory copy
        CHECK(nation.GetRegionCount() == 1);
        CHECK(std::string(nation.GetRegion(0)->GetDirectoryName()->ToChar()) == "Copied");
        CHECK(FileExists(directory / "Copied" / "region.ini"));

        // no download directory string -> refuse
        cSC4Nation::msRegionDownloadDirectory = cRZString();
        CHECK(!nation.CopyNewUserRegions());
        nation.Shutdown();
    }

    // ---------------------------------------------------------------------------------------------------------
    // 8. ResetTutorialCities: the registry's region directories, the tutorial items and the record files
    // ---------------------------------------------------------------------------------------------------------
    {
        ResetStatics();
        SetRegionHost(nullptr);
        const fs::path directory = root / "scenario8";
        const fs::path data = root / "scenario8_data";
        fs::create_directories(directory);
        MakeRegionTree(directory, "alpha", "beta");
        fs::create_directories(data / "Regions" / "alpha");
        WriteFile(data / "Regions" / "alpha" / "extra.ini", "x");

        FakeNationHost host;
        host.regionDirectory = cRZString((directory.string() + "/").c_str());
        host.dataDirectory = cRZString((data.string() + "/").c_str());
        host.regionScreenCurrent = true;
        host.defaultTutorialDirectory = cRZString("beta");
        host.tutorialDirectories = {cRZString("alpha"), cRZString("beta")};
        // beta carries a city file and a region.ini, so it is kept (alpha has none and is rebuilt);
        // two tutorial items, the first one's record cannot be read
        WriteFile(directory / "beta" / "city.sc4", "x");
        NationTutorialItem unreadable{};
        unreadable.guid = 0x1234;
        NationTutorialItem readable{};
        readable.guid = 0x2345;
        host.tutorialItems["beta"] = {unreadable, readable};

        SetNationHost(&host);
        FakeRegionHost regionHost;
        regionHost.regionDirectory = host.regionDirectory;
        regionHost.downloadDirectory = cRZString((directory.string() + "/Downloads/").c_str());
        SetRegionHost(&regionHost);

        cSC4Nation nation;
        CHECK(nation.StaticInit());
        CHECK(nation.LoadRegions());
        const int betaIndex = IndexOfRegion(nation, "beta");
        CHECK(betaIndex >= 0);
        nation.SetActiveRegion(static_cast<std::uint32_t>(betaIndex));

        // the first record read fails, the second one succeeds with a city at (3,4)
        FakeCity city("Tutorial", 3, 4);
        city.AddRef();
        host.readCity = &city;
        host.failFirstReads = 1;   // the first record cannot be read, the second one can
        nation.ResetTutorialCities();

        CHECK(host.destroyedScreens == 1);
        CHECK(host.createdScreens == 1);
        // both tutorial items were visited; the failing one deactivates the tutorial
        CHECK(host.tutorialActive.size() == 2);
        CHECK(host.tutorialActive[0] == "4660:0");
        CHECK(host.records.size() == 2);
        CHECK(host.lastRecordKey.type == kRegionTutorialCityRecordType);
        CHECK(host.lastRecordKey.group == kRegionTutorialCityRecordGroup);
        CHECK(host.lastRecordKey.instance == 0x2345);
        CHECK(host.records[0] == std::string(directory.string()) + "/beta/Tutorial-City-" + "00001234.sc4");
        CHECK(host.records[1] == std::string(directory.string()) + "/beta/Tutorial-City-" + "00002345.sc4");
        // alpha had no city files, so it was deleted and created again, and the data directory's files were copied
        CHECK(FileExists(directory / "alpha" / "extra.ini"));
        // the city was handed to the region through MoveCity
        CHECK(host.citySavePaths.size() == 1);
        CHECK(host.cityPositions.size() == 1 && host.cityPositions[0] == "3,4");
    }

    fs::remove_all(root);
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("sc4_nation_test: all checks passed\n");
    return 0;
}

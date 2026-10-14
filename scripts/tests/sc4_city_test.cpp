#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

#include "gz/cIGZInitShutdown.h"
#include "rz/cRZString.h"
#include "sc4/AutoRecord.h"
#include "sc4/DBSegment.h"
#include "sc4/cSC4City.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {

std::vector<std::uint32_t> g_initLog;       // class ids in Init order
std::vector<std::uint32_t> g_shutdownLog;
std::vector<cSC4City*> g_cities;

// A component stand-in: answers every interface, serializes one dword (its class id).
class Stub : public cIGZSerializable, public cIGZInitShutdown {
public:
    explicit Stub(std::uint32_t classId) : classId_(classId) {}
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cIGZInitShutdown) *out = static_cast<cIGZInitShutdown*>(this);
        else *out = static_cast<cIGZSerializable*>(this);
        AddRef();
        return true;
    }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override {
        if (refs_ > 1) return --refs_;
        delete this;
        return 0;
    }
    bool Write(cIGZOStream& s) const override { return s.SetUint32(classId_); }
    bool Read(cIGZIStream& s) override { return s.GetUint32(readBack); }
    std::uint32_t GetGZCLSID() const override { return classId_; }
    bool Init() override { g_initLog.push_back(classId_); return true; }
    bool Shutdown() override { g_shutdownLog.push_back(classId_); return true; }
    std::uint32_t readBack = 0;
private:
    std::uint32_t classId_;
    std::uint32_t refs_ = 0;
};

class FakeHost : public ICityHost, public ISerializableFactory {
public:
    std::uint32_t CreateCitySerialNumber() override { return 0x1234; }
    void GetCurrentLanguageAndCountry(int& l, int& c) override { l = 1; c = 1; }
    cIGZUnknown* CreateComponent(std::uint32_t classId, std::uint32_t) override {
        auto* s = new Stub(classId);
        s->AddRef();
        return static_cast<cIGZSerializable*>(s);
    }
    void PostMessage(cIGZMessage2* m) override { posted.push_back(m->GetType()); }
    void SendMessage(cIGZMessage2* m) override { sent.push_back(m->GetType()); }
    std::uint32_t GetSimulatorDate() override { return 0x777; }
    void OnComponentCreated(CityComponent c, cIGZUnknown*, float, float) override { created.push_back(c); }
    void LoadDefaultCityName(cIGZString& n) override { n.AssignFromCString("New City"); }
    void SaveCheatCodeState(std::vector<char>& out) override { out = {'c', 'h', 'e', 'a', 't'}; }
    void LoadCheatCodeState(const void* d, std::uint32_t n) override { loaded.assign(static_cast<const char*>(d), n); }
    cIGZSerializable* CreateSerializable(std::uint32_t classId) override {
        auto* s = new Stub(classId);
        s->AddRef();
        return s;
    }
    std::vector<std::uint32_t> posted, sent;
    std::vector<CityComponent> created;
    std::string loaded;
};

// The city is reference counted (messages hold references to it), so it lives on the heap.
cSC4City* MakeCity(ICityHost* host) {
    auto* city = new cSC4City(host);
    city->AddRef();
    g_cities.push_back(city);
    return city;
}

}  // namespace

int main() {
    FakeHost host;
    SetSerializableFactory(&host);
    const std::string path = "/tmp/sc4_city_test_" + std::to_string(getpid()) + ".sc4";
    const cGZPersistResourceKey key{0xca63e2a3, 0x1234, 0x5678};

    // --- geometry and identity ---
    {
        cSC4City& city = *MakeCity(&host);
        CHECK(city.GetCitySerialNumber() == 0x1234);
        CHECK(city.SetSize(1024.0f, 1024.0f));
        CHECK(city.CellCountX() == 64 && city.GetCitySizeType() == cSC4City::kSizeSmall);
        int cx, cz;
        city.PositionToCell(100.5f, 15.9f, cx, cz);
        CHECK(cx == 6 && cz == 0);
        city.PositionToCell(16.0f, 32.0f, cx, cz);
        CHECK(cx == 1 && cz == 2);                    // exact cell borders belong to the next cell
        float x, z;
        city.CellCenterToPosition(2, 3, x, z);
        CHECK(x == 40.0f && z == 56.0f);
        CHECK(city.LocationIsInBounds(0.0f, 1023.9f) && !city.LocationIsInBounds(1024.0f, 0.0f));
        CHECK(city.CellIsInBounds(63, 63) && !city.CellIsInBounds(64, 0) && city.CellCornerIsInBounds(64, 64));
        city.SetWorldPosition(500.0f, -95.0f);
        float lon, lat;
        city.GetWorldPosition(lon, lat);
        CHECK(lon == 180.0f && lat == -90.0f && city.GetWorldHemisphere() == 2);

        cRZString name("A very long name ...");
        city.SetCityName(name);
        CHECK(host.posted.size() == 1 && host.posted[0] == kCityNameChangedMessage);
        cRZString got;
        city.GetCityName(got);
        CHECK(std::string(got.ToChar()) == "A very long name ...");
        cRZString longName(std::string(300, 'x').c_str());
        city.SetMayorName(longName);
        city.GetMayorName(got);
        CHECK(got.Strlen() == 0x80);
        city.SetCityDescription(longName);
        city.GetCityDescription(got);
        CHECK(got.Strlen() == 0x100);
        CHECK(!city.GetEstablished());
        city.SetEstablished(true);
        CHECK(host.sent.size() == 1 && host.sent[0] == 0x26d31ec4);
        city.SetEstablished(true);
        CHECK(host.sent.size() == 1);              // no change, no message
        city.DisableSave();
        city.DisableSave();
        city.EnableSave();
        CHECK(city.IsSaveDisabled());
    }

    // --- lifecycle, component creation and ordering ---
    host.posted.clear();
    host.sent.clear();
    {
        cSC4City& city = *MakeCity(&host);
        CHECK(city.SetSize(4096.0f, 4096.0f) && city.CellCountX() == 256);
        city.SetDifficultyLevel(cSC4City::kDifficultyHard);
        cRZString name("Testville");
        city.SetCityName(name);
        cRZString mayor("Mayor Q");
        city.SetMayorName(mayor);
        city.SetEstablished(true);
        CHECK(city.Init());
        CHECK(!city.SetSize(1.0f, 1.0f));          // size is frozen once initialized
        CHECK(city.GetBirthDate() == 0x777);
        CHECK(city.GetComponent(CityComponent::Terrain) != nullptr);
        CHECK(city.GetComponent(CityComponent::LotManager) == city.GetComponent(CityComponent::ZoneDeveloper));
        CHECK(g_initLog.size() > 50);
        CHECK(g_initLog.front() == GetCityComponentInfo(CityComponent::Simulator).classId);
        CHECK(g_initLog[1] == GetCityComponentInfo(CityComponent::HistoryWarehouse).classId);
        CHECK(host.created.front() == CityComponent::Simulator);

        // save through a database segment
        cSC4DBSegment* seg = new cSC4DBSegment();
        seg->AddRef();
        seg->Init();
        cRZString p(path.c_str());
        seg->SetPath(p);
        CHECK(seg->Open(false, true));
        cISC4DBSegmentOStream* os = nullptr;
        CHECK(seg->OpenOStream(key, &os, true));
        CHECK(city.Write(*os));
        CHECK(seg->CloseOStream(os));
        os->Release();
        CHECK(seg->Close());
        seg->Release();
        g_shutdownLog.clear();
        CHECK(city.Shutdown());
        CHECK(g_shutdownLog.size() > 50);
        CHECK(g_shutdownLog.back() == GetCityComponentInfo(CityComponent::Simulator).classId);
        CHECK(g_shutdownLog.front() == GetCityComponentInfo(CityComponent::TutorialSystem).classId);
    }

    // --- load it back ---
    {
        cSC4DBSegment* seg = new cSC4DBSegment();
        seg->AddRef();
        seg->Init();
        cRZString p(path.c_str());
        seg->SetPath(p);
        CHECK(seg->Open(true, false));
        CHECK(seg->LoadClassObjects(0x48));
        cISC4DBSegmentIStream* is = nullptr;
        CHECK(seg->OpenIStream(key, &is));
        cSC4City& loaded = *MakeCity(&host);
        CHECK(loaded.Read(*is));
        CHECK(loaded.GetCitySerialNumber() == 0x1234);
        CHECK(loaded.GetEstablished());
        CHECK(loaded.GetDifficultyLevel() == cSC4City::kDifficultyHard);
        CHECK(loaded.CellCountX() == 256 && loaded.CellCountZ() == 256 && loaded.SizeX() == 4096.0f);
        cRZString got;
        loaded.GetCityName(got);
        CHECK(std::string(got.ToChar()) == "Testville");
        loaded.GetMayorName(got);
        CHECK(std::string(got.ToChar()) == "Mayor Q");
        CHECK(loaded.GetBirthDate() == 0x777);
        CHECK(loaded.GetComponent(CityComponent::Terrain) != nullptr);
        CHECK(loaded.GetComponent(CityComponent::Unknown250) && loaded.GetComponent(CityComponent::NetworkManager));
        CHECK(host.loaded == "cheat");              // the cheat-code sub-record round trip
        seg->CloseIStream(is);
        is->Release();
        seg->Close();
        seg->Release();
    }
    for (cSC4City* c : g_cities) c->Release();
    unlink(path.c_str());
    std::printf(failures ? "FAILED (%d)\n" : "PASS sc4_city\n", failures);
    return failures != 0;
}

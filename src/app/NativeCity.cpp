// Loading a city file in the native application (see NativeCity.h). The factory and city host are the ones the city probe
// (src/tools/city_probe.cpp) proved against the retail files.
#include "app/NativeCity.h"

#include <cstdio>
#include <set>

#include "gz/GZCOM.h"
#include "sc4/App.h"
#include "sc4/AutoHelpers.h"
#include "sc4/DBSegment.h"
#include "sc4/ISC4City.h"
#include "sc4/SaveGame.h"
#include "sc4/cSC4City.h"

cIGZSerializable* CreateCityProbeSerializable(std::uint32_t classId);   // city_probe_creators.cpp
cIGZSerializable* CreateCityProbeSerializable2(std::uint32_t classId);
cIGZSerializable* CreateCityProbeSerializable3(std::uint32_t classId);
cIGZSerializable* CreateCityProbeSerializable4(std::uint32_t classId);
cIGZSerializable* CreateCityProbeSerializable5(std::uint32_t classId);
cIGZSerializable* CreateCityProbeSerializable6(std::uint32_t classId);
cIGZSerializable* CreateCityProbeSerializable7(std::uint32_t classId);
void InstallCityProbeBindings();                                          // city_probe_bindings.cpp

namespace native {
namespace {

// The city's host services. The game implements this with the framework, the message server and GZCOM; the probe only
// needs the serial number and the language, and everything optional stays a no-op so the load path itself runs.
struct CityHost final : sc4::ICityHost {
    std::uint32_t CreateCitySerialNumber() override { return sc4::SC4CreateCitySerialNumber(); }
    void GetCurrentLanguageAndCountry(int& language, int& country) override { language = 0; country = 0; }
    cIGZUnknown* CreateComponent(std::uint32_t, std::uint32_t) override { return nullptr; }
    void PostMessage(cIGZMessage2*) override {}
    void SendMessage(cIGZMessage2*) override {}
};

// Every serialized object of a city file is created from its class id, as the game's serializer does through GZCOM
// (cSC4COMSerializer::OnGetGZSerializable -> GZCOM()->CreateInstance(classId, cIGZSerializable)). The probe goes
// through the generated GZCOM creator table (gz/GZCOM.h); classes whose creator is not linked or not recovered come
// back null and are listed, so the output names the next class LoadCity needs.
struct Factory final : sc4::ISerializableFactory {
    cIGZSerializable* CreateSerializable(std::uint32_t classId) override {
        if (classId == 0x8990c372) {                        // cSC4City: the probe's own host
            auto* city = new sc4::cSC4City(&host);
            city->AddRef();
            return city;
        }
        // Classes the decompilation kept as plain C++ (no cIGZUnknown base) have a GZCOM creator that returns a raw
        // object, which is not a COM pointer; the probe's wrapper takes priority for those.
        if (cIGZSerializable* wrapped = CreateCityProbeSerializable(classId)) return wrapped;
        for (auto create : {CreateCityProbeSerializable2, CreateCityProbeSerializable3, CreateCityProbeSerializable4, CreateCityProbeSerializable5, CreateCityProbeSerializable6, CreateCityProbeSerializable7})
            if (cIGZSerializable* wrapped = create(classId)) return wrapped;
        const gzcom::ClassFactoryInfo* info = gzcom::FindClassFactory(classId);
        void* object = info && info->create ? info->create() : nullptr;
        cIGZSerializable* serializable = nullptr;
        if (object) {
            auto* unknown = static_cast<cIGZUnknown*>(object);   // the creators hand out the primary (cIGZUnknown) pointer
            unknown->AddRef();
            if (!unknown->QueryInterface(sc4::kGZIID_cIGZSerializable, reinterpret_cast<void**>(&serializable))) serializable = nullptr;
            unknown->Release();
        }
        if (!serializable && missing.insert(classId).second)
            std::printf("  class %08x %s: %s\n", classId, info ? info->className : "(not in the GZCOM table)",
                        !info ? "unknown id" : object ? "no cIGZSerializable" : "creator returns null");
        return serializable;
    }
    CityHost host;
    std::set<std::uint32_t> missing;
};

}  // namespace

bool LoadCityFile(cSC4App& app, const std::string& path) {
    static CityHost cityHost;
    static Factory factory;
    static bool installed = false;
    if (!installed) {
        installed = true;
        sc4::SetSerializableFactory(&factory);
        auto& hooks = app.CityHooks();
        hooks.createCity = []() -> cISC4City* { auto* city = new sc4::cSC4City(&cityHost); city->AddRef(); return city; };
        hooks.createCitySegment = []() -> cIGZPersistDBSegment* {
            auto* segment = new sc4::cSC4DBSegment();
            segment->AddRef();
            return segment->AsIGZPersistDBSegment();
        };
        hooks.readLoadSaveVersion = [](cIGZPersistDBSegment* segment) { return sc4::SC4ReadAppLoadSaveVersion(segment); };
        hooks.notify = [](const std::string& title, const std::string& text) { std::printf("notify: %s - %s\n", title.c_str(), text.c_str()); };
        hooks.loadText = [](std::uint32_t, std::uint32_t) { return std::string(); };
        hooks.busy = [](bool) {};
        hooks.flushGraphics = [] {};
        hooks.destroyCurrentScreen = [] {};
        hooks.createCityScreen = [] {};
        hooks.createRegionScreen = [] {};
        InstallStubHosts();
        InstallCityProbeBindings();
    }
    const bool loaded = app.LoadCity(cRZString(path.c_str()), nullptr);
    std::printf("city: %s %s (%zu classes missing)\n", path.c_str(), loaded ? "loaded" : "FAILED", factory.missing.size());
    return loaded;
}

}  // namespace native

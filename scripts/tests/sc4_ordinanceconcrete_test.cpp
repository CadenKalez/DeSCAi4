// The 20 concrete ordinances a saved city holds, one per GZCOM class id. A city file stores each as a
// cIGZSerializable with the concrete class id; the load path (cISC4DBSegmentIStream::ReadGZSerializable ->
// gzcom::FindClassFactory -> the creator) must produce an object whose GetGZCLSID is that id and that answers
// the two interfaces cSC4OrdinanceSimulator::Read asks for (cISC4Ordinance 0xaa5e2247 and cIGZSerializable
// 0xe4fda3d4). This test walks the whole set through that path. The income/condition overrides of the classes
// that differ from cSC4OrdinanceSimple are covered by sc4_ordinancetypes_test.cpp.
#include <cstdint>
#include <cstdio>

#include "gz/GZCOM.h"
#include "sc4/Ordinance.h"
#include "sc4/OrdinanceTypes.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {
constexpr std::uint32_t kGZIID_cIGZSerializable = 0xe4fda3d4;

struct ConcreteOrdinance {
    const char* name;
    std::uint32_t clsid;
    std::uint32_t allocSize;              // bytes the original creator allocates (base 0xe8, +0x8/+0x10 for
                                          // the cached budget/police/pollution/power/landfill service pointers)
    void* (*create)();
};

const ConcreteOrdinance kOrdinances[] = {
    {"cSC4OrdinanceCarpoolIncentive", 0xa2bf1ddc, 0xe8, &gzcom::GZCOM_CreatecSC4OrdinanceCarpoolIncentive},
    {"cSC4OrdinanceCleanAir", 0xa2bf1e43, 0xe8, &gzcom::GZCOM_CreatecSC4OrdinanceCleanAir},
    {"cSC4OrdinanceCommunityCPRTraining", 0x00d0723d, 0xe8, &gzcom::GZCOM_CreatecSC4OrdinanceCommunityCPRTraining},
    {"cSC4OrdinanceFreeClinics", 0x40d07236, 0xf0, &gzcom::GZCOM_CreatecSC4OrdinanceFreeClinics},
    {"cSC4OrdinanceJuniorSportsLeague", 0x815b4cef, 0xf0, &gzcom::GZCOM_CreatecSC4OrdinanceJuniorSportsLeague},
    {"cSC4OrdinanceLandfillGasRecovery", 0x22f6e80c, 0xf8, &gzcom::GZCOM_CreatecSC4OrdinanceLandfillGasRecovery},
    {"cSC4OrdinanceLegalizeGambling", 0xa0d07129, 0xe8, &gzcom::GZCOM_CreatecSC4OrdinanceLegalizeGambling},
    {"cSC4OrdinanceMandatorySmokeDetectors", 0x62bf1daa, 0xe8, &gzcom::GZCOM_CreatecSC4OrdinanceMandatorySmokeDetectors},
    {"cSC4OrdinanceNeighborhoodWatch", 0x62bf1db9, 0xf0, &gzcom::GZCOM_CreatecSC4OrdinanceNeighborhoodWatch},
    {"cSC4OrdinanceNuclearFreeZone", 0xe0d0722e, 0xe8, &gzcom::GZCOM_CreatecSC4OrdinanceNuclearFreeZone},
    {"cSC4OrdinancePaperReductionAct", 0x22f6e81b, 0xe8, &gzcom::GZCOM_CreatecSC4OrdinancePaperReductionAct},
    {"cSC4OrdinancePowerConservation", 0x82b9999b, 0xe8, &gzcom::GZCOM_CreatecSC4OrdinancePowerConservation},
    {"cSC4OrdinanceProReadingCampaign", 0xe0d07233, 0xf0, &gzcom::GZCOM_CreatecSC4OrdinanceProReadingCampaign},
    {"cSC4OrdinanceShuttleService", 0xa2bf1de5, 0xe8, &gzcom::GZCOM_CreatecSC4OrdinanceShuttleService},
    {"cSC4OrdinanceTireRecycling", 0xc2f6e81f, 0xe8, &gzcom::GZCOM_CreatecSC4OrdinanceTireRecycling},
    {"cSC4OrdinanceTouristPromotion", 0x62f6e7cf, 0xf0, &gzcom::GZCOM_CreatecSC4OrdinanceTouristPromotion},
    {"cSC4OrdinanceTrashPresort", 0x42bf1e18, 0xf8, &gzcom::GZCOM_CreatecSC4OrdinanceTrashPresort},
    {"cSC4OrdinanceVehicleEmissionStandard", 0xc2bf1e04, 0xe8, &gzcom::GZCOM_CreatecSC4OrdinanceVehicleEmissionStandard},
    {"cSC4OrdinanceWaterConservation", 0x02bf1dfa, 0xe8, &gzcom::GZCOM_CreatecSC4OrdinanceWaterConservation},
    {"cSC4OrdinanceYouthCurfew", 0xc2bf1dc5, 0xe8, &gzcom::GZCOM_CreatecSC4OrdinanceYouthCurfew},
};

int CheckOne(const ConcreteOrdinance& o) {
    std::printf("%-42s ", o.name);

    // The GZCOM class table the load path consults must carry the class.
    const gzcom::ClassFactoryInfo* factory = gzcom::FindClassFactory(o.clsid);
    CHECK(factory != nullptr);
    CHECK(factory && factory->clsid == o.clsid);
    CHECK(factory && factory->create != nullptr);
    CHECK(factory && factory->allocSize == o.allocSize);

    // The creator must return the concrete object, not the base or null.
    void* raw = o.create();
    CHECK(raw != nullptr);
    if (!raw) { std::printf("\n"); return 0; }
    auto* object = static_cast<cSC4OrdinanceSimple*>(raw);
    CHECK(object->GetGZCLSID() == o.clsid);

    // The two interfaces cSC4OrdinanceSimulator::Read (0xe4fda3d4) and its cISC4Ordinance lookup (0xaa5e2247)
    // demand. QueryInterface AddRefs, but the creator returns a raw `new` with refcount 0 and the port's
    // Release() deletes at <= 1, so leave the views un-released and free the object directly below.
    void* serializable = nullptr;
    CHECK(object->QueryInterface(kGZIID_cIGZSerializable, &serializable) && serializable != nullptr);
    void* ordinance = nullptr;
    CHECK(object->QueryInterface(kGZIID_cISC4Ordinance, &ordinance) && ordinance != nullptr);

    delete object;                       // the creator returns a plain `new`
    std::printf("\n");
    return 0;
}
}  // namespace

int main() {
    for (const ConcreteOrdinance& o : kOrdinances) CheckOne(o);
    CHECK(sizeof(kOrdinances) / sizeof(kOrdinances[0]) == 20);
    std::printf(failures ? "FAILED\n" : "sc4_ordinanceconcrete ok\n");
    return failures != 0;
}

// The land value simulator's maps for the growth developers' host: LandValueSimulator.h and GrowthDeveloper.h define SC4Point
// differently, so the lookups live in this TU (NativeGrowthDevelopers.h declares them).
#include "app/NativeGrowthDevelopers.h"

#include "app/NativeCity.h"
#include "sc4/LandValueSimulator.h"
#include "sc4/cSC4City.h"

namespace native {
namespace {
sc4::cSC4LandValueSimulator* LandValue() {
    sc4::cSC4City* city = HostCity();
    cIGZUnknown* object = city ? city->GetComponent(sc4::CityComponent::LandValueSimulator) : nullptr;
    void* raw = nullptr;
    if (!object || !object->QueryInterface(0xe4fda3d4, &raw) || !raw) return nullptr;
    auto* serializable = static_cast<cIGZSerializable*>(raw);
    auto* out = serializable->GetGZCLSID() == sc4::kGZCLSID_cSC4LandValueSimulator ? static_cast<sc4::cSC4LandValueSimulator*>(serializable) : nullptr;
    serializable->Release();
    return out;
}
}  // namespace

sc4::cSC4SimGrid<std::uint8_t>* NativeTotalLandValueMap() { auto* s = LandValue(); return s ? s->GetTotalLandValueMap() : nullptr; }
sc4::cSC4SimGrid<std::uint8_t>* NativeIntrinsicLandValueMap() { auto* s = LandValue(); return s ? s->GetIntrinsicLandValueMap() : nullptr; }
sc4::cSC4SimGrid<float>* NativeSlopeMap() { auto* s = LandValue(); return s ? s->GetSlopeMap() : nullptr; }

}  // namespace native

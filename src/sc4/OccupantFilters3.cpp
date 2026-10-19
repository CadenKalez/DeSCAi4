#include "sc4/OccupantFilters3.h"

namespace sc4 {

namespace {
ModelClassTest g_modelClassTest;
constexpr std::uint32_t kAnimatableClasses[2] = {kModelClassAnimatableA, kModelClassAnimatableB};
bool IsAnimatable(cISC4Occupant* occupant) {
    if (!g_modelClassTest || !g_modelClassTest(occupant, kAnimatableClasses, 2)) return false;
    return !occupant->AsPropertyHolder()->HasProperty(kPropertyNotAnimatable);
}
}  // namespace

void SetModelClassTest(ModelClassTest test) { g_modelClassTest = std::move(test); }

bool cSC4AnimatableOccupantFilter::IsOccupantIncluded(cISC4Occupant* occupant) { return IsAnimatable(occupant); }

bool cSC4VisibleAnimatableOccupantFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    return occupant->IsFlagSet(kOccupantFlagVisible) && IsAnimatable(occupant);
}

bool cSC4ActiveFireOccupantFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    if (occupant->GetType() != kOccupantTypeFire) return false;
    cSC4FireOccupant* fire = nullptr;
    if (!occupant->QueryInterface(kGZIID_cISC4FireOccupant, reinterpret_cast<void**>(&fire))) return false;
    const bool attached = fire->GetTarget() != nullptr;
    fire->Release();
    return attached;
}

bool cSC4AudioAmbienceManagerOccupantFilter::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != 0x46a80026) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4AudioAmbienceManagerOccupantFilter::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cSC4AudioAmbienceManagerOccupantFilter::IsOccupantTypeIncluded(std::uint32_t type) {
    switch (type) {
        case 0x897f7ec2: case kOccupantTypeProp: case kOccupantTypeNetwork: case 0xc7cc75f1:
        case 0x278128a0: case 0x480e6151: case 0x74758926:
            return true;
        default:
            return false;
    }
}

bool cSC4FloraBlockingOccupantFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    switch (occupant->GetType()) {
        case kOccupantTypeProp: case 0x278128a0: case 0x6a11bd2b: case 0x74758926:
            return true;
        case kOccupantTypeNetwork: {                // the interface lookup result is ignored: networks never block
            cIGZUnknown* unused = nullptr;
            if (occupant->QueryInterface(0xa821ef94, reinterpret_cast<void**>(&unused)) && unused) unused->Release();
            return false;
        }
        default:
            return false;
    }
}

}  // namespace sc4

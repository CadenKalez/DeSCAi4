#include "sc4/MySimFilters.h"

namespace sc4 {

namespace {
IMySimFilterHost* g_host = nullptr;
}
IMySimFilterHost* MySimFilterHost() { return g_host; }
void SetMySimFilterHost(IMySimFilterHost* host) { g_host = host; }

bool cMySimBuildingFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    if (occupant->GetType() != kOccupantTypeBuilding) return false;
    void* raw = nullptr;
    if (!occupant->QueryInterface(kIidMySimBuilding, &raw)) return false;
    auto* building = static_cast<IMySimBuildingFacts*>(raw);   // (the original never releases this reference)
    std::uint8_t stage = 0;
    const bool hasLot = g_host->LotStage(building->LotKey(), stage);
    if (hasLot && stage >= 4) return false;
    if (*building->Completion() < 1.0f) return false;
    if (onlyKindOne_ && *building->OccupancyKind() != 1) return false;
    return true;
}

bool cMySimPylonFilter::IsOccupantIncluded(cISC4Occupant* occupant) {
    if (occupant->GetType() != kOccupantTypePylon) return false;
    void* raw = nullptr;
    if (!occupant->QueryInterface(kIidMySimPylon, &raw)) return false;
    auto* pylon = static_cast<IMySimPylonFacts*>(raw);
    bool included = false;
    if (pylon->Kind() != 0 && !pylon->Blocked()) {
        if (cIGZUnknown* link = pylon->LinkedObject()) {
            void* probe = nullptr;
            included = link->QueryInterface(kIidPylonLink, &probe);
            if (probe) static_cast<cIGZUnknown*>(probe)->Release();
        }
    }
    pylon->Release();
    return included;
}

}  // namespace sc4

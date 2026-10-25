#include "sc4/ModelMakerUtility.h"

#include "sc4/BuildingOccupant.h"
#include "sc4/FloraOccupant.h"
#include "sc4/FoundationOccupant.h"
#include "sc4/LandfillOccupant.h"
#include "sc4/PropOccupant.h"
#include "sc4/SC4Utilities.h"
#include "sc4/SCPropertyHelp.h"

namespace sc4 {
namespace {
// nModelLODFromZoom (0x100b...): the model LOD (the 0x100 step of the instance id) of each zoom, one row per detail offset.
int ModelLODFromZoom(int zoom) {
    return zoom < 0 ? 0 : (zoom > 4 ? 4 : zoom);
}
bool Has(const cISCPropertyHolder* holder, std::uint32_t id) { return holder && holder->HasProperty(id); }
}  // namespace

std::uint32_t cSC4ModelMakerUtility::GetModelInstanceID(const std::vector<std::uint32_t>& v, std::uint32_t property,
                                                        std::uint32_t at, int zoom, int rotation, int state, bool lit,
                                                        std::uint32_t* outA, std::uint32_t* outB) {
    std::uint32_t a = 0, b = 0;
    int id = 0;
    auto get = [&](std::uint32_t i) -> std::uint32_t { return i < v.size() ? v[i] : 0u; };
    if (property - 0x27812820u < 4) {
        const int lod = ModelLODFromZoom(zoom);
        switch (property) {
            case 0x27812820:
                a = get(at); b = get(at + 1); id = static_cast<int>(get(at + 2));
                break;
            case 0x27812821: {
                a = get(at); b = get(at + 1);
                const int base = static_cast<int>(get(at + 2));
                if (base != 0) {
                    const int index = (lit ? 0 : state) + rotation * 3;
                    id = (index % 4) * 0x10 + lod * 0x100 + base;
                }
                break;
            }
            case 0x27812822: {
                const int index = SC4ZoomAndRotationToIndex(lod, ((lit ? 0 : state) + rotation * 3) % 4);
                a = get(at); b = get(at + 1);
                id = static_cast<int>(get(at + 2 + index));
                break;
            }
            case 0x27812823:
                a = get(at); b = get(at + 1);
                id = static_cast<int>(get(at + 2 + lod));
                break;
        }
    }
    if (outA) *outA = a;
    if (outB) *outB = b;
    return static_cast<std::uint32_t>(id);
}

void cSC4ModelMakerUtility::GetAllModelInstanceIDs(std::vector<std::uint32_t>& ids, std::vector<float>* transforms,
                                                   cISCPropertyHolder* holder, bool night, int zoom, int rotation, int state,
                                                   bool lit, std::uint32_t orientation, std::uint32_t* outA, std::uint32_t* outB) {
    ids.clear();
    if (transforms) transforms->clear();
    std::vector<std::uint32_t> v;
    std::uint32_t zoomMask = 0;
    if (cSCPropertyHelp::GetPropertyValue(holder, 0x0abfc024, zoomMask) && ((zoomMask >> (zoom & 0x1f)) & 1) == 0) return;
    const std::uint32_t night256 = night ? 0x100u : 0u;
    auto zeroOffset = [&] { if (transforms) transforms->assign(3, 0.0f); };
    auto set = [&](std::uint32_t a, std::uint32_t b) { if (outA) *outA = a; if (outB) *outB = b; };
    const int lod = ModelLODFromZoom(zoom);
    if (cSCPropertyHelp::GetPropertyValue(holder, night256 + 0x27812820, v)) {
        if (v.size() < 3) return;
        set(v[0], v[1]);
        ids.push_back(v[2]);
        zeroOffset();
    } else if (cSCPropertyHelp::GetPropertyValue(holder, night256 | 0x27812821, v)) {
        if (v.size() < 3) return;
        set(v[0], v[1]);
        std::uint32_t id = 0;
        if (v[2] != 0) id = v[2] + static_cast<std::uint32_t>((((lit ? 0 : state) + rotation * 3) % 4) * 0x10 + lod * 0x100);
        ids.push_back(id);
        zeroOffset();
    } else if (cSCPropertyHelp::GetPropertyValue(holder, night256 | 0x27812822, v)) {
        ids.push_back(GetModelInstanceID(v, 0x27812822, 0, zoom, rotation, state, lit, outA, outB));
        zeroOffset();
    } else if (cSCPropertyHelp::GetPropertyValue(holder, night256 | 0x27812823, v)) {
        if (v.size() < 3u + lod) return;
        set(v[0], v[1]);
        ids.push_back(v[2 + lod]);
        zeroOffset();
    } else if (cSCPropertyHelp::GetPropertyValue(holder, night256 | 0x27812824, v)) {
        const std::uint32_t count = static_cast<std::uint32_t>(v.size());
        if (count > 4) {
            std::uint32_t index = 0, next = 4;
            do {
                const std::uint32_t tag = v[index], x = v[index + 1], y = v[index + 2], z = v[index + 3];
                const std::uint32_t subtype = v[next];
                int payload = 3;
                if ((subtype | 1) != 0x27812821) {
                    payload = 0x1e;
                    if (subtype != 0x27812822) {
                        if (subtype != 0x27812823) break;
                        payload = 5;
                    }
                }
                const std::uint32_t start = index + 5;
                index = start + payload;
                if (count < index) break;
                if (tag == orientation) {
                    ids.push_back(GetModelInstanceID(v, subtype, start, zoom, rotation, state, lit, outA, outB));
                    if (transforms) {
                        transforms->push_back(static_cast<float>(static_cast<int>(x)) * 1.5258789e-05f);
                        transforms->push_back(static_cast<float>(static_cast<int>(y)) * 1.5258789e-05f);
                        transforms->push_back(static_cast<float>(static_cast<int>(z)) * 1.5258789e-05f);
                    }
                }
                next = index + 4;
            } while (next < count);
        }
    } else if (cSCPropertyHelp::GetPropertyValue(holder, night256 | 0x27812825, v)) {
        if (v.size() < 3) return;
        int id = 0;
        if (v[2] != 0) id = static_cast<int>(v[2]) + (((lit ? 0 : state) + rotation * 3) % 4) * 0x10 + lod * 0x100;
        set(v[0], v[1]);
        ids.push_back(static_cast<std::uint32_t>(id) + orientation * 0x10000u);
        zeroOffset();
    }
}

void cSC4ModelMakerUtility::GetAllModelInstanceIDs(std::vector<std::uint32_t>& ids, std::vector<float>* transforms,
                                                   cISC4Occupant* occupant, int zoom, int rotation, int state, bool lit,
                                                   std::uint32_t orientation, std::uint32_t* outA, std::uint32_t* outB) {
    GetAllModelInstanceIDs(ids, transforms, occupant->AsPropertyHolder(), occupant->IsFlagSet(0x80), zoom, rotation, state, lit,
                           orientation, outA, outB);
}

bool cSC4ModelMakerUtility::FoundationOccupantUsesBuildingStyleModels(cISC4Occupant* occupant) {
    cISCPropertyHolder* holder = occupant->AsPropertyHolder();
    if (Has(holder, 0xe9822d62)) return false;
    return !Has(holder, 0x68fcff37);
}

bool cSC4ModelMakerUtility::OccupantUsesBuildingStyleModels(cISC4Occupant* occupant) {
    cISCPropertyHolder* holder = occupant->AsPropertyHolder();
    if (Has(holder, 0xe9822d62)) return false;
    return Has(holder, 0x27812821) || Has(holder, 0x27812820) || Has(holder, 0x27812822) || Has(holder, 0x27812823) ||
           Has(holder, 0x27812824);
}

void cSC4ModelMakerUtility::GetOccupantOrientationAndState(cISC4Occupant* occupant, std::uint32_t& orientation, int& state) {
    orientation = 0;
    state = 0;
    void* raw = nullptr;
    if (occupant->QueryInterface(kGZIID_cISC4BuildingOccupant, &raw)) {
        auto* building = static_cast<cISC4BuildingOccupant*>(raw);
        state = building->GetOrientation();
        building->Release();
    } else if (auto* prop = dynamic_cast<cSC4PropOccupant*>(occupant)) {
        state = prop->GetOrientation();
        orientation = prop->GetState();
    } else if (auto* flora = dynamic_cast<cSC4FloraOccupant*>(occupant)) {
        state = static_cast<int>(flora->GetOrientation());
        orientation = static_cast<std::uint32_t>(flora->GetState());
    } else if (auto* landfill = dynamic_cast<cSC4LandfillOccupant*>(occupant)) {
        state = landfill->GetOrientation();
    } else if (auto* foundation = dynamic_cast<cSC4FoundationOccupant*>(occupant)) {
        state = foundation->GetOrientation();
    }
}

int cSC4ModelMakerUtility::GetOccupantModelClass(cISC4Occupant* occupant) {
    std::vector<std::uint32_t> v;
    cISCPropertyHolder* holder = occupant->AsPropertyHolder();
    if (!cSCPropertyHelp::GetPropertyValue(holder, 0xea123cef, v)) return 0;
    if (occupant->GetType() == 0xa823821e && !Has(holder, 0x4a70d491)) {
        auto* prop = dynamic_cast<cSC4PropOccupant*>(occupant);
        if (!prop) return 0;
        const std::uint32_t state = prop->GetState();
        if (v.size() == 1) return state == 0 ? static_cast<int>(v[0]) : 0;
        if (v.size() & 1) return 0;
        for (std::size_t i = 0; i < v.size(); i += 2)
            if (v[i] == state) return static_cast<int>(v[i + 1]);
        return 0;
    }
    return v.empty() ? 0 : static_cast<int>(v[0]);
}

}  // namespace sc4

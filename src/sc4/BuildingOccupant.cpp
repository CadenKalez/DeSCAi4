#include "sc4/BuildingOccupant.h"

#include <algorithm>

#include "rz/cRZMessage2Standard.h"
#include "sc4/SCPropertyHelp.h"

namespace sc4 {

namespace {
IBuildingDevelopmentHost* g_development = nullptr;
ExemplarStringLoader g_stringLoader = nullptr;
}  // namespace

IBuildingDevelopmentHost* BuildingDevelopment() { return g_development; }
void SetBuildingDevelopment(IBuildingDevelopmentHost* host) { g_development = host; }
void SetExemplarStringLoader(ExemplarStringLoader loader) { g_stringLoader = loader; }

bool cSC4BuildingOccupant::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISC4BuildingOccupant) {
        *out = static_cast<cISC4BuildingOccupant*>(this);
        AddRef();
        return true;
    }
    return cSC4Occupant::QueryInterface(iid, out);
}

bool cSC4BuildingOccupant::Init() {
    if (!IsInitialized()) cSC4Occupant::Init();
    return true;
}

bool cSC4BuildingOccupant::Shutdown() { return cSC4Occupant::Shutdown(); }

bool cSC4BuildingOccupant::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    if (!cSC4Occupant::Write(stream)) return false;
    stream.SetUint32(buildingType_);
    stream.SetUint8(litFlags_);
    for (float v : min_) stream.SetFloat32(v);
    for (float v : max_) stream.SetFloat32(v);
    stream.SetSint8(orientation_);
    stream.SetFloat32(completion_);
    return !stream.GetError();
}

bool cSC4BuildingOccupant::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 2 || !cSC4Occupant::Read(stream)) return false;
    stream.GetUint32(buildingType_);
    stream.GetUint8(litFlags_);
    for (float& v : min_) stream.GetFloat32(v);
    for (float& v : max_) stream.GetFloat32(v);
    std::int8_t orientation = 0;
    stream.GetSint8(orientation);
    orientation_ = orientation;
    stream.GetFloat32(completion_);
    return !stream.GetError();
}

void cSC4BuildingOccupant::GetPosition(float* xyz) {
    xyz[0] = (min_[0] + max_[0]) * 0.5f;
    xyz[1] = min_[1];
    xyz[2] = (min_[2] + max_[2]) * 0.5f;
}

void cSC4BuildingOccupant::SetPosition(const float* xyz) {
    const float halfX = (max_[0] - min_[0]) * 0.5f;
    const float oldY = min_[1];
    const float halfZ = (max_[2] - min_[2]) * 0.5f;
    min_[0] = xyz[0] - halfX;
    max_[0] = halfX + xyz[0];
    min_[1] = xyz[1];
    max_[1] = (max_[1] - oldY) + xyz[1];
    min_[2] = xyz[2] - halfZ;
    max_[2] = halfZ + xyz[2];
}

void cSC4BuildingOccupant::GetBoundingBox(float* minXyz, float* maxXyz) {
    std::copy(min_, min_ + 3, minXyz);
    std::copy(max_, max_ + 3, maxXyz);
}

void cSC4BuildingOccupant::SetBoundingBox(const float* min, const float* max) {
    std::copy(min, min + 3, min_);
    std::copy(max, max + 3, max_);
}

const cRZString& cSC4BuildingOccupant::GetExemplarName() const {
    if (g_development) return g_development->ExemplarName(buildingType_);
    static const cRZString empty;
    return empty;
}

const cRZString& cSC4BuildingOccupant::GetBuildingName() const {
    if (g_development) return g_development->BuildingName(buildingType_);
    static const cRZString empty;
    return empty;
}

const BuildingProfile& cSC4BuildingOccupant::GetBuildingProfile() const {
    if (g_development) return g_development->Profile(buildingType_);
    static const BuildingProfile empty;
    return empty;
}

bool cSC4BuildingOccupant::SetOrientation(std::int32_t orientation) {
    orientation_ = static_cast<std::int8_t>(std::clamp(orientation, 0, 3));
    return true;
}

void cSC4BuildingOccupant::SetLit(bool lit) {
    if ((((litFlags_ & 1) != 0) ^ lit) == 0) return;
    litFlags_ ^= 1;
    auto* message = new cRZMessage2Standard();
    message->AddRef();
    message->SetType(kMessageBuildingLitChanged);
    message->SetIGZUnknown(static_cast<cISC4Occupant*>(static_cast<cSC4Occupant*>(this)));   // AsOccupant()
    if (cIGZMessageServer2* server = DBSegmentMessageServer()) server->MessagePost(message, false);
    message->Release();
}

void cSC4BuildingOccupant::SetName(const cRZString& name) {
    cISCPropertyHolder* holder = AsPropertyHolder();
    if (cISCProperty* property = holder->GetProperty(kPropertyBuildingCustomName))
        property->GetPropertyValue()->SetValString(name);
    else
        holder->AddProperty(kPropertyBuildingCustomName, static_cast<const cIGZString&>(name), false);
    PostOccupantMessage(kMessageOccupantChanged, 0);
}

bool cSC4BuildingOccupant::GetName(cRZString& name) {
    cISCPropertyHolder* holder = AsPropertyHolder();
    if (cSCPropertyHelp::GetPropertyValue(holder, kPropertyBuildingCustomName, name)) return true;
    if (g_stringLoader && g_stringLoader(name, holder, kPropertyExemplarNameKey)) return true;
    return cSCPropertyHelp::GetPropertyValue(holder, kPropertyExemplarName, name);
}

void cSC4BuildingOccupant::PostOccupantMessage(std::uint32_t message, std::uint32_t data) {
    cSC4Occupant::PostOccupantMessage(message, data);
    if ((message | 1) != kMessageOccupantRemovedFromManager) return;      // inserted / removed only
    auto* msg = new cRZMessage2Standard();
    msg->AddRef();
    msg->SetType(message == kMessageOccupantInserted ? kMessageBuildingInserted : kMessageBuildingRemoved);
    cISC4Occupant* occupant = static_cast<cSC4Occupant*>(this);
    msg->SetVoid1(occupant);
    msg->SetIGZUnknown(occupant);
    msg->SetData2(IsFlagSet(kOccupantFlagVisible) ? 1 : 0);
    msg->SetData3(GetType());
    msg->SetData4(data);
    if (cIGZMessageServer2* server = DBSegmentMessageServer()) server->MessagePost(msg, false);
    msg->Release();
}

std::uint32_t cSC4BuildingOccupant::ConvertZoneToDensity(int zoneType) {
    static const std::uint32_t kDensity[9] = {1, 2, 3, 1, 2, 3, 3, 3, 3};
    return static_cast<std::uint32_t>(zoneType - 1) < 9u ? kDensity[zoneType - 1] : 0;
}

}  // namespace sc4

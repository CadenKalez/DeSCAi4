#include "sc4/Occupant.h"

#include <algorithm>
#include <cmath>

#include "rz/cRZMessage2Standard.h"
#include "sc4/DBSegment.h"
#include "sc4/SCPropertyHelp.h"

namespace sc4 {

namespace {
PlaceableObjectFactory g_placeableFactory;

void Send(cRZMessage2Standard& message, bool synchronous) {
    cIGZMessageServer2* server = DBSegmentMessageServer();
    if (!server) return;
    if (synchronous) server->MessageSend(&message);
    else server->MessagePost(&message, false);
}
}  // namespace

void SetPlaceableObjectFactory(PlaceableObjectFactory factory) { g_placeableFactory = std::move(factory); }

cSC4Occupant::~cSC4Occupant() {
    if (placeable_) placeable_->Release();
}

bool cSC4Occupant::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSerializable) {
        *out = static_cast<cIGZSerializable*>(this);
    } else if (iid == kGZIID_cISC4Occupant || iid == 1) {
        *out = static_cast<cISC4Occupant*>(this);
    } else {
        return cSCExemplarPropertyHolder::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

bool cSC4Occupant::Init() {
    if (!IsInitialized()) {
        state_ |= 1;
        if ((state_ & 2) == 0) PostOccupantMessage(kMessageOccupantInitialized, 0);
        else SetHighlight(0, false);       // (loaded from a save: re-announce through the highlight path)
    }
    return true;
}

bool cSC4Occupant::Shutdown() {
    if (IsInitialized()) {
        state_ &= 0xfe;
        PostOccupantMessage(kMessageOccupantShutdown, 0);
    }
    return true;
}

bool cSC4Occupant::Write(cIGZOStream& stream) const {
    stream.SetUint16(4);
    stream.SetUint16(removal_);
    stream.SetUint8(static_cast<std::uint8_t>(GetHighlight()));
    stream.SetUint8(flags_ & 0xdd);
    stream.SetUint32(type_);
    for (std::uint8_t cell : bbox_) stream.SetUint8(cell);
    cSCExemplarPropertyHolder::Write(stream);
    return !stream.GetError();
}

bool cSC4Occupant::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 4) return false;
    stream.GetUint16(removal_);
    std::uint8_t highlight = 0;
    stream.GetUint8(highlight);
    state_ = static_cast<std::uint8_t>(highlight << 4 | 2);
    stream.GetUint8(flags_);
    stream.GetUint32(type_);
    for (std::uint8_t& cell : bbox_) stream.GetUint8(cell);
    cSCExemplarPropertyHolder::Read(stream);
    return !stream.GetError();
}

void cSC4Occupant::GetBoundingBox(float* minXyz, float* maxXyz) {
    minXyz[0] = minXyz[1] = minXyz[2] = 0.0f;
    maxXyz[0] = maxXyz[1] = maxXyz[2] = 0.0f;
}

void cSC4Occupant::GetBoundingCityCells(SC4CellRect& rect) {
    float lo[3], hi[3];
    GetBoundingBox(lo, hi);
    // cells are 16 m; the 0.01 inset keeps boxes that end exactly on a cell edge out of the next cell
    rect.x0 = static_cast<std::int32_t>(std::floor((lo[0] + 0.01f) * 0.0625f));
    rect.z0 = static_cast<std::int32_t>(std::floor((lo[2] + 0.01f) * 0.0625f));
    rect.x1 = static_cast<std::int32_t>(std::floor((hi[0] - 0.01f) * 0.0625f));
    rect.z1 = static_cast<std::int32_t>(std::floor((hi[2] - 0.01f) * 0.0625f));
}

void cSC4Occupant::SetRemovalFlags(int flags) {
    std::uint8_t high = static_cast<std::uint8_t>(removal_ >> 8);
    if (flags & 1) {
        ++high;
        removal_ = static_cast<std::uint16_t>((removal_ & 0xff) | high << 8);
    }
    if (flags & 2) removal_ = static_cast<std::uint16_t>(((removal_ + 1) & 0xff) | high << 8);
}

void cSC4Occupant::UnsetRemovalFlags(int flags) {
    std::uint8_t high = static_cast<std::uint8_t>(removal_ >> 8);
    if (flags & 1) {
        --high;
        removal_ = static_cast<std::uint16_t>((removal_ & 0xff) | high << 8);
    }
    if (flags & 2) removal_ = static_cast<std::uint16_t>(((removal_ + 0xff) & 0xff) | high << 8);
}

bool cSC4Occupant::CanRemove(int flags) {
    if (flags & 1) return removal_ < 0x100;
    if (flags & 2) return (removal_ & 0xff) == 0;
    return true;
}

void cSC4Occupant::PostOccupantMessage(std::uint32_t message, std::uint32_t data) {
    auto* msg = new cRZMessage2Standard();
    msg->AddRef();
    msg->SetType(message);
    msg->SetVoid1(static_cast<cISC4Occupant*>(this));
    msg->SetIGZUnknown(static_cast<cISC4Occupant*>(this));
    msg->SetData2(IsFlagSet(kOccupantFlagVisible) ? 1 : 0);
    msg->SetData3(GetType());
    msg->SetData4(data);
    // a removal with a placeable is delivered synchronously so listeners can still read its geometry
    Send(*msg, message == kMessageOccupantRemoved && placeable_ != nullptr);
    msg->Release();
}

void cSC4Occupant::SetHighlight(int highlight, bool postNow) {
    const int previous = static_cast<int>(GetHighlight());
    if (previous == highlight) return;
    state_ = static_cast<std::uint8_t>((highlight << 4) | (state_ & 0xf));
    if (!IsInitialized()) return;
    if (postNow) {
        PostOccupantMessage(kMessageOccupantChanged, 0);
        return;
    }
    auto* msg = new cRZMessage2Standard();
    msg->AddRef();
    msg->SetType(kMessageOccupantHighlight);
    msg->SetVoid1(static_cast<cISC4Occupant*>(this));
    msg->SetIGZUnknown(static_cast<cISC4Occupant*>(this));
    msg->SetData2(static_cast<std::uint64_t>(previous));
    msg->SetData3(static_cast<std::uint64_t>(highlight));
    Send(*msg, false);
    msg->Release();
}

void cSC4Occupant::SetVisibility(bool visible, bool post) {
    if (visible) {
        if (flags_ & kOccupantFlagVisible) return;
        flags_ |= kOccupantFlagVisible;
    } else {
        if (!(flags_ & kOccupantFlagVisible)) return;
        flags_ &= ~kOccupantFlagVisible;
    }
    if (post) PostOccupantMessage(kMessageOccupantChanged, 0);
}

cISC43DPlaceableObject* cSC4Occupant::GetOrCreatePlaceableObject() {
    if (!placeable_ && g_placeableFactory) {
        cISC43DPlaceableObject* created = g_placeableFactory();   // already holds one reference
        placeable_ = created;
    }
    return placeable_;
}

void cSC4Occupant::SetPlaceableObject(cISC43DPlaceableObject* object) {
    cISC43DPlaceableObject* previous = placeable_;
    if (previous == object) return;
    if (object) object->AddRef();
    placeable_ = object;
    if (previous) previous->Release();
}

void cSC4Occupant::SetFlag(std::uint32_t flag) {
    if ((flag | flags_) == flags_) return;
    SetAllFlags(flags_ | flag);
}

void cSC4Occupant::ClearFlag(std::uint32_t flag) {
    if ((~flag & flags_) == flags_) return;
    SetAllFlags(~flag & flags_);
}

bool cSC4Occupant::GetOccupantManagerBBox(std::uint8_t* cells) const {
    if (bbox_[0] == 0xff) return false;
    std::copy(bbox_, bbox_ + 4, cells);
    return true;
}

void cSC4Occupant::SetOccupantManagerBBox(const std::uint8_t* cells) { std::copy(cells, cells + 4, bbox_); }

namespace {
bool GroupsContain(cISCProperty* property, std::uint32_t group) {
    if (!property) return false;
    cIGZVariant* value = property->GetPropertyValue();
    const std::uint32_t* data = value->RefUint32();
    const std::uint32_t count = value->GetCount();
    return std::find(data, data + count, group) != data + count;
}
}  // namespace

bool cSC4Occupant::IsOccupantGroup(std::uint32_t group) const {
    cISCPropertyHolder* holder = const_cast<cSC4Occupant*>(this)->AsPropertyHolder();
    return GroupsContain(holder->GetProperty(kPropertyOccupantGroups), group);
}

bool cSC4Occupant::GetOccupantGroups(std::set<std::uint32_t>& groups) const {
    groups.clear();
    std::vector<std::uint32_t> values;
    cSCPropertyHelp::GetPropertyValue(const_cast<cSC4Occupant*>(this)->AsPropertyHolder(), kPropertyOccupantGroups, values);
    for (std::uint32_t value : values)
        if (value != 0) groups.insert(value);
    return !groups.empty();
}

void cSC4Occupant::AddOccupantGroup(std::uint32_t group) {
    cISCPropertyHolder* holder = AsPropertyHolder();
    std::vector<std::uint32_t> values;
    cSCPropertyHelp::GetPropertyValue(holder, kPropertyOccupantGroups, values);
    if (std::find(values.begin(), values.end(), group) != values.end()) return;
    values.push_back(group);
    cSCPropertyHelp::SetPropertyValue(holder, kPropertyOccupantGroups, values);
    PostOccupantMessage(kMessageOccupantGroupAdded, group);
}

bool cSC4Occupant::IsOccupantGroup(const cGZPersistResourceKey& exemplarKey, std::uint32_t group) {
    cIGZPersistResourceManager* manager = PersistResourceManagerService();
    void* raw = nullptr;
    if (!manager || !manager->GetResource(exemplarKey, kGZIID_cISCResExemplar, &raw, 0, 0)) return false;
    auto* exemplar = static_cast<cISCResExemplar*>(raw);
    const bool found = GroupsContain(exemplar->AsISCPropertyHolder()->GetProperty(kPropertyOccupantGroups), group);
    exemplar->Release();
    return found;
}

void cSC4Occupant::GetOccupantGroups(const cGZPersistResourceKey& exemplarKey, std::vector<std::uint32_t>& groups) {
    groups.clear();
    cIGZPersistResourceManager* manager = PersistResourceManagerService();
    void* raw = nullptr;
    if (!manager || !manager->GetResource(exemplarKey, kGZIID_cISCResExemplar, &raw, 0, 0)) return;
    auto* exemplar = static_cast<cISCResExemplar*>(raw);
    if (cISCProperty* property = exemplar->AsISCPropertyHolder()->GetProperty(kPropertyOccupantGroups)) {
        cIGZVariant* value = property->GetPropertyValue();
        const std::uint32_t* data = value->RefUint32();
        groups.insert(groups.end(), data, data + value->GetCount());
    }
    exemplar->Release();
}

}  // namespace sc4

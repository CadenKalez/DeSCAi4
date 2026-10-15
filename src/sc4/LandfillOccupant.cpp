#include "sc4/LandfillOccupant.h"

#include "gz/StreamOps.h"
#include "sc4/Exemplar.h"

namespace sc4 {

cSC4LandfillOccupant::cSC4LandfillOccupant(std::uint32_t tileId, std::int32_t orientation)
    : tileId_(tileId), orientation_(orientation) {
    type_ = kOccupantTypeLandfill;
    SetFlag(kOccupantFlagVisible);
}

bool cSC4LandfillOccupant::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISC4LandfillOccupant) {
        *out = this;
    } else if (iid == kGZIID_cIGZSerializable) {
        *out = static_cast<cIGZSerializable*>(this);
    } else if (iid == kGZIID_cISC4Occupant || iid == 1) {
        *out = static_cast<cISC4Occupant*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

void cSC4LandfillOccupant::GetPosition(float* xyz) {
    xyz[0] = position_[0];
    xyz[1] = position_[1];
    xyz[2] = position_[2];
}

void cSC4LandfillOccupant::UpdateBounds() {
    boundsMin_[0] = position_[0] + -8.0f;
    boundsMin_[1] = position_[1];
    boundsMin_[2] = position_[2] + -8.0f;
    boundsMax_[0] = position_[0] + 8.0f;
    boundsMax_[1] = position_[1] + 16.0f;
    boundsMax_[2] = position_[2] + 8.0f;
}

void cSC4LandfillOccupant::SetPosition(const float* xyz) {
    position_[0] = xyz[0];
    position_[1] = xyz[1];
    position_[2] = xyz[2];
    UpdateBounds();
}

void cSC4LandfillOccupant::GetBoundingBox(float* minXyz, float* maxXyz) {
    for (int i = 0; i < 3; ++i) {
        minXyz[i] = boundsMin_[i];
        maxXyz[i] = boundsMax_[i];
    }
}

std::uint32_t cSC4LandfillOccupant::SetTileID(std::uint32_t tileId) {
    const std::uint32_t previous = tileId_;
    if (previous != tileId) {
        tileId_ = tileId;
        if (cISCResExemplar* exemplar = GetDefaultExemplar()) {
            cGZPersistResourceKey key;
            exemplar->GetKey(key);
            key.instance = tileId;
            exemplar->SetKey(key);
        }
    }
    return previous;
}

bool cSC4LandfillOccupant::Write(cIGZOStream& stream) const {
    stream.SetUint16(3);
    if (!cSC4Occupant::Write(stream)) return false;
    stream.SetUint32(tileId_);
    stream.SetSint32(orientation_);
    stream << cS3DVector3{position_[0], position_[1], position_[2]};
    stream.SetUint8(empty_ ? 1 : 0);
    return !stream.GetError();
}

bool cSC4LandfillOccupant::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 3 || !cSC4Occupant::Read(stream)) return false;
    stream.GetUint32(tileId_);
    stream.GetSint32(orientation_);
    cS3DVector3 position;
    stream >> position;
    position_[0] = position.x;
    position_[1] = position.y;
    position_[2] = position.z;
    std::uint8_t flag = 0;
    stream.GetUint8(flag);
    empty_ = flag == 1;
    UpdateBounds();
    return !stream.GetError();
}

}  // namespace sc4

#include "sc4/FoundationOccupant.h"

namespace sc4 {

bool cSC4FoundationOccupant::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISC4FoundationOccupant) {
        *out = this;
        AddRef();
        return true;
    }
    return cSC4Occupant::QueryInterface(iid, out);
}

void cSC4FoundationOccupant::GetPosition(float* xyz) {
    xyz[0] = position_.x;
    xyz[1] = position_.y;
    xyz[2] = position_.z;
}

void cSC4FoundationOccupant::SetPosition(const float* xyz) {
    position_.x = xyz[0];
    position_.y = xyz[1];
    position_.z = xyz[2];
}

void cSC4FoundationOccupant::GetBoundingBox(float* minXyz, float* maxXyz) {
    minXyz[0] = position_.x - size_.x * 0.5f;
    maxXyz[0] = size_.x * 0.5f + position_.x;
    minXyz[1] = position_.y - size_.y;
    maxXyz[1] = position_.y;
    minXyz[2] = position_.z - size_.z * 0.5f;
    maxXyz[2] = size_.z * 0.5f + position_.z;
}

bool cSC4FoundationOccupant::SetSize(const float* xyz) {
    size_.x = xyz[0];
    size_.y = xyz[1];
    size_.z = xyz[2];
    return true;
}

bool cSC4FoundationOccupant::SetOrientation(std::int32_t orientation) {
    std::int32_t value = orientation > -1 ? orientation : 0;
    if (value > 2) value = 3;
    orientation_ = static_cast<std::int8_t>(value);
    return true;
}

bool cSC4FoundationOccupant::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    if (cSC4Occupant::Write(stream)) {
        stream.SetUint32(foundationType_);
        stream.SetFloat32(maxHeight_);
        stream << position_;
        stream << size_;
        stream.SetSint8(usage_);
        stream.SetSint8(orientation_);
    }
    return !stream.GetError();
}

bool cSC4FoundationOccupant::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 2) return false;
    if (cSC4Occupant::Read(stream)) {
        stream.GetUint32(foundationType_);
        stream.GetFloat32(maxHeight_);
        stream >> position_;
        stream >> size_;
        stream.GetSint8(usage_);
        stream.GetSint8(orientation_);
    }
    return !stream.GetError();
}

}  // namespace sc4

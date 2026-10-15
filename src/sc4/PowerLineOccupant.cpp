#include "sc4/PowerLineOccupant.h"

#include "gz/StreamOps.h"
#include "sc4/DBSegment.h"

namespace sc4 {

bool cSC4PowerLineOccupant::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 0xa9764c8c || iid == kGZCLSID_cSC4PowerLineOccupant || iid == kGZIID_cISC4PowerLineOccupant) {
        *out = this;
        AddRef();
        return true;
    }
    return cSC4Occupant::QueryInterface(iid, out);
}

bool cSC4PowerLineOccupant::Shutdown() {
    SetConnectedPoles(nullptr, nullptr, false);
    return cSC4Occupant::Shutdown();
}

void cSC4PowerLineOccupant::GetPosition(float* xyz) {
    xyz[0] = position_[0];
    xyz[1] = position_[1];
    xyz[2] = position_[2];
}

void cSC4PowerLineOccupant::SetPosition(const float* xyz) {
    position_[0] = xyz[0];
    position_[1] = xyz[1];
    position_[2] = xyz[2];
    cellX_ = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<long>(xyz[0] * 0.0625f)));
    cellZ_ = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<long>(xyz[2] * 0.0625f)));
}

void cSC4PowerLineOccupant::GetBoundingBox(float* minXyz, float* maxXyz) {
    for (int i = 0; i < 3; ++i) minXyz[i] = maxXyz[i] = position_[i];
}

void cSC4PowerLineOccupant::GetConnectedPoles(cISC4PowerPoleOccupant*& first, cISC4PowerPoleOccupant*& second,
                                              bool& connected) const {
    if (pole0_) pole0_->AddRef();
    first = pole0_;
    if (pole1_) pole1_->AddRef();
    second = pole1_;
    connected = connected_;
}

void cSC4PowerLineOccupant::SetConnectedPoles(cISC4PowerPoleOccupant* first, cISC4PowerPoleOccupant* second,
                                              bool connected) {
    if (first) first->AddRef();
    if (pole0_) pole0_->Release();
    pole0_ = first;
    if (second) second->AddRef();
    if (pole1_) pole1_->Release();
    pole1_ = second;
    connected_ = connected;
}

bool cSC4PowerLineOccupant::Write(cIGZOStream& stream) const {
    stream.SetUint16(2);
    if (cSC4Occupant::Write(stream)) {
        stream.SetFloat32(position_[0]);
        stream.SetFloat32(position_[1]);
        stream.SetFloat32(position_[2]);
        stream.SetUint32(cellX_);
        stream.SetUint32(cellZ_);
        stream << connected_;
        SC4WriteGZSerializable(stream, pole0_);
        SC4WriteGZSerializable(stream, pole1_);
    }
    return !stream.GetError();
}

bool cSC4PowerLineOccupant::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 2) return false;
    if (cSC4Occupant::Read(stream)) {
        stream.GetFloat32(position_[0]);
        stream.GetFloat32(position_[1]);
        stream.GetFloat32(position_[2]);
        stream.GetUint32(cellX_);
        stream.GetUint32(cellZ_);
        stream >> connected_;
        SC4ReadGZSerializable(stream, kGZCLSID_cSC4PowerPoleOccupant, reinterpret_cast<void**>(&pole0_), false);
        SC4ReadGZSerializable(stream, kGZCLSID_cSC4PowerPoleOccupant, reinterpret_cast<void**>(&pole1_), false);
    }
    return !stream.GetError();
}

}  // namespace sc4

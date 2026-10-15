#include "sc4/AdviceSubject.h"

#include "sc4/DBSegment.h"

namespace sc4 {

static IAdviceSubjectHost* g_host = nullptr;
IAdviceSubjectHost* AdviceSubjectHost() { return g_host; }
void SetAdviceSubjectHost(IAdviceSubjectHost* host) { g_host = host; }

cSC4AdviceSubject::~cSC4AdviceSubject() {
    if (object_) {
        cIGZUnknown* old = object_;
        object_ = nullptr;
        old->Release();
    }
}

bool cSC4AdviceSubject::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSerializable) {
        *out = static_cast<cIGZSerializable*>(this);
    } else if (iid == kGZIID_cISC4AdviceSubject) {
        *out = static_cast<cISC4AdviceSubject*>(this);
    } else {
        return cRZUnknown::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

void cSC4AdviceSubject::SetLocation(float x, float z) {
    locationX_ = x;
    locationZ_ = z;
    hasLocation_ = true;
    ValidateLocation();
}

// Clamps the location into [0, city cells] on both axes.
void cSC4AdviceSubject::ValidateLocation() {
    if (!hasLocation_) return;
    const std::int32_t cellsX = g_host->CityCellsX();
    const std::int32_t cellsZ = g_host->CityCellsZ();
    if (locationX_ < 0.0f) locationX_ = 0.0f;
    if (locationZ_ < 0.0f) locationZ_ = 0.0f;
    if (static_cast<float>(cellsX) < locationX_) locationX_ = static_cast<float>(cellsX);
    if (static_cast<float>(cellsZ) < locationZ_) locationZ_ = static_cast<float>(cellsZ);
}

bool cSC4AdviceSubject::SetObject(cIGZUnknown* object) {
    if (object) object->AddRef();
    cIGZUnknown* old = object_;
    if (old != object) {
        if (object) object->AddRef();
        object_ = object;
        if (old) old->Release();
    }
    if (object) object->Release();
    return true;
}

bool cSC4AdviceSubject::Clone(cISC4AdviceSubject** out) {
    auto* copy = new cSC4AdviceSubject();
    copy->AddRef();
    if (HasLocation()) copy->SetLocation(GetLocationX(), GetLocationZ());
    copy->SetObject(GetObjectA());
    copy->SetData1(GetData1());
    copy->SetData2(GetData2());
    bool result = false;
    if (out) {
        *out = copy;
        result = true;
        copy->AddRef();
    }
    copy->Release();
    return result;
}

bool cSC4AdviceSubject::Write(cIGZOStream& stream) const {
    stream.SetUint16(4);
    stream.SetSint8(hasLocation_ ? 1 : 0);
    if (hasLocation_) {
        stream.SetFloat32(locationX_);
        stream.SetFloat32(locationZ_);
    }
    stream.SetUint32(data1_);
    stream.SetUint32(data2_);
    SC4WriteGZSerializable(stream, object_);
    return !stream.GetError();
}

bool cSC4AdviceSubject::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 4) return false;
    std::int8_t flag = 0;
    stream.GetSint8(flag);
    hasLocation_ = flag != 0;
    if (hasLocation_) {
        stream.GetFloat32(locationX_);
        stream.GetFloat32(locationZ_);
    }
    stream.GetUint32(data1_);
    stream.GetUint32(data2_);
    if (object_) {
        cIGZUnknown* old = object_;
        object_ = nullptr;
        old->Release();
    }
    SC4ReadGZSerializable(stream, kGZIID_cIGZUnknown, reinterpret_cast<void**>(&object_), false);
    return !stream.GetError();
}

}  // namespace sc4

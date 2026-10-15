#include "sc4/ToxicOccupant.h"

namespace sc4 {

static IToxicHost* g_toxicHost = nullptr;
IToxicHost* ToxicHost() { return g_toxicHost; }
void SetToxicHost(IToxicHost* host) { g_toxicHost = host; }

cSC4ToxicOccupant::~cSC4ToxicOccupant() {
    if (effect_) effect_->Release();
}

bool cSC4ToxicOccupant::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISC4ToxicOccupant) {
        *out = this;
        AddRef();
        return true;
    }
    return cSC4Occupant::QueryInterface(iid, out);
}

bool cSC4ToxicOccupant::Init() {
    if (IsInitialized()) return true;
    cSC4Occupant::Init();
    AddOccupantGroup(kOccupantGroupToxic);
    if (effect_) {
        effect_->Stop(0);
        IToxicEffect* old = effect_;
        effect_ = nullptr;
        old->Release();
    }
    effect_ = g_toxicHost->CreateEffect("toxic_disaster_fx");
    if (effect_) {
        UpdateVisualEffectTransform();
        effect_->Start(0);
        active_ = true;
    }
    return true;
}

void cSC4ToxicOccupant::UpdateVisualEffectTransform() {
    if (!effect_) return;
    cS3DTransform transform;
    transform.changeCounter = 1;
    transform.flags = 1;          // translation only
    transform.translation[0] = position_[0];
    transform.translation[1] = position_[1];
    transform.translation[2] = position_[2];
    effect_->SetTransform(transform);
}

bool cSC4ToxicOccupant::Shutdown() {
    if (IsInitialized()) {
        if (effect_) {
            effect_->Stop(0);
            IToxicEffect* old = effect_;
            effect_ = nullptr;
            old->Release();
        }
        active_ = false;
        cSC4Occupant::Shutdown();
    }
    return true;
}

void cSC4ToxicOccupant::SetHosed(std::int32_t delta) {
    std::uint32_t result;
    if (delta < 0)
        result = static_cast<std::uint32_t>(-delta) < hosed_ ? hosed_ + static_cast<std::uint32_t>(delta) : 0;
    else
        result = hosed_ + static_cast<std::uint32_t>(delta);
    hosed_ = result;
}

void cSC4ToxicOccupant::SetCell(int cellX, int cellZ) {
    float x = 0.0f, z = 0.0f;
    g_toxicHost->CellToWorld(cellX, cellZ, x, z);
    position_[0] = x;
    position_[1] = g_toxicHost->TerrainHeight(x, z);
    position_[2] = z;
    UpdateVisualEffectTransform();
    cellX_ = cellX;
    cellZ_ = cellZ;
}

void cSC4ToxicOccupant::GetBoundingBox(float* minXyz, float* maxXyz) {
    minXyz[0] = position_[0] + -16.0f;
    minXyz[1] = position_[1];
    minXyz[2] = position_[2] + -16.0f;
    maxXyz[0] = position_[0] + 16.0f;
    maxXyz[1] = position_[1] + 8.0f;
    maxXyz[2] = position_[2] + 16.0f;
}

void cSC4ToxicOccupant::GetPosition(float* xyz) {
    xyz[0] = position_[0];
    xyz[1] = position_[1];
    xyz[2] = position_[2];
}

bool cSC4ToxicOccupant::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    if (cSC4Occupant::Write(stream)) {
        stream.SetUint32(value68_);
        stream.SetUint32(hosed_);
    }
    return !stream.GetError();
}

bool cSC4ToxicOccupant::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 1) return false;
    if (cSC4Occupant::Read(stream)) {
        stream.GetUint32(value68_);
        stream.GetUint32(hosed_);
    }
    return !stream.GetError();
}

}  // namespace sc4

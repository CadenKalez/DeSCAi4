#include "sc4/FireOccupant.h"

#include <algorithm>
#include <cmath>

#include "rz/cRZRandom.h"
#include "sc4/DBSegment.h"
#include "sc4/SCPropertyHelp.h"

namespace sc4 {

namespace {
const char* const kStageEffects[5] = {"fire_1", "fire_2", "fire_3", "fire_4", "fire_5"};
const char* const kStageSideEffects[5] = {"wfire_1", "wfire_2", "wfire_3", "wfire_4", "wfire_5"};
cRZRandom& Random() {
    static cRZRandom random(0xffffffffu);   // the framework's sRandom
    return random;
}
}  // namespace

FireServices& GetFireServices() {
    static FireServices services;
    return services;
}

cSC4ResponseCurve* cSC4FireOccupant::mpTopFireCurve = nullptr;
cSC4ResponseCurve* cSC4FireOccupant::mpSideFireCurve = nullptr;

// ---- cSC4Flame ----------------------------------------------------------------------------------------

cSC4Flame::~cSC4Flame() {
    if (effect) effect->Release();
}

void cSC4Flame::Update(std::uint32_t stage) {
    if (effect) {
        effect->Stop(0);
        effect->Release();
        effect = nullptr;
    }
    if (stage > 4) return;                       // the original indexes a five-entry table without checking
    const char* name = side ? kStageSideEffects[stage] : kStageEffects[stage];
    IFlameEffect* created = GetFireServices().createEffect ? GetFireServices().createEffect(name) : nullptr;
    if (created) {
        effect = created;
        UpdateTransform();
        if (visible) effect->Start(0);
    }
}

// A turn about the vertical axis; the angle is quarterTurns * 90 passed to sincos as is (the original never converts
// it to radians).
void cSC4Flame::UpdateTransform() {
    if (!effect) return;
    float s, c;
    s = std::sin(static_cast<float>(quarterTurns) * 90.0f);
    c = std::cos(static_cast<float>(quarterTurns) * 90.0f);
    cS3DTransform t;
    t.changeCounter = 2;
    t.flags = 5;
    t.rotation[0][0] = c;  t.rotation[0][1] = -(c * 0.0f);  t.rotation[0][2] = s;
    t.rotation[1][0] = s * 0.0f + 0.0f;  t.rotation[1][1] = 1.0f - s * 0.0f * 0.0f;  t.rotation[1][2] = t.rotation[0][1];
    t.rotation[2][0] = 0.0f - s;  t.rotation[2][1] = t.rotation[1][0];  t.rotation[2][2] = c;
    t.translation[0] = position[0];
    t.translation[1] = position[1];
    t.translation[2] = position[2];
    t.scale = 1.0f;
    effect->SetTransform(t);
}

void cSC4Flame::Shutdown() {
    if (!active) return;
    if (effect) {
        if (effect->IsRunning()) effect->Stop(0);
        effect->Release();
        effect = nullptr;
    }
    active = false;
}

void cSC4Flame::SetVisible(bool v) {
    if (visible == v) return;
    visible = v;
    if (!effect) return;
    if (v) effect->Start(1);
    else effect->Stop(1);
}

bool cSC4Flame::Write(cIGZOStream& stream) const {
    stream.SetUint16(3);
    stream << side;
    stream.SetUint32(quarterTurns);
    stream << cS3DVector3{position[0], position[1], position[2]};
    stream << transform;
    return !stream.GetError();
}

bool cSC4Flame::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version < 2) return false;
    stream >> side;
    stream.GetUint32(quarterTurns);
    cS3DVector3 p;
    stream >> p;
    position[0] = p.x; position[1] = p.y; position[2] = p.z;
    if (version < 3) LoadLegacyS3DTransform(stream, transform);
    else stream >> transform;
    return !stream.GetError();
}

// ---- cSC4FireOccupant ---------------------------------------------------------------------------------

cSC4FireOccupant::cSC4FireOccupant() { type_ = kOccupantTypeFire; }

cSC4FireOccupant::~cSC4FireOccupant() {
    for (cSC4Flame* flame : flames_) delete flame;
    if (target_) target_->Release();
}

bool cSC4FireOccupant::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISC4FireOccupant) {
        *out = this;
        AddRef();
        return true;
    }
    return cSC4Occupant::QueryInterface(iid, out);
}

bool cSC4FireOccupant::Init() {
    if (!IsInitialized()) {
        cSC4Occupant::Init();
        AddOccupantGroup(kOccupantGroupFire);
        for (cSC4Flame* flame : flames_) {
            if (!flame->active) flame->active = true;
            flame->Update(stage_);
        }
    }
    return true;
}

void cSC4FireOccupant::ReleaseFlame(cSC4Flame* flame) {
    flame->Shutdown();
    delete flame;
}

bool cSC4FireOccupant::Shutdown() {
    if (IsInitialized()) {
        while (!flames_.empty()) {
            ReleaseFlame(flames_.front());
            flames_.erase(flames_.begin());
        }
        if (target_) {
            cSCPropertyHelp::SetPropertyValue(target_->AsPropertyHolder(), kPropertyOnFire, false);
            target_->UnsetRemovalFlags(2);
            if (GetFireServices().fireEnded) GetFireServices().fireEnded(target_);
            cISC4Occupant* target = target_;
            target_ = nullptr;
            target->Release();
        }
        cSC4Occupant::Shutdown();
    }
    return true;
}

void cSC4FireOccupant::SetHosed(int delta) {
    std::uint32_t result;
    if (delta < 0) result = static_cast<std::uint32_t>(-delta) < hosed_ ? hosed_ + delta : 0;
    else result = hosed_ + delta;
    hosed_ = result;
}

bool cSC4FireOccupant::SetTarget(cISC4Occupant* target) {
    cISC4Occupant* previous = target_;
    if (previous != target) {
        if (target) target->AddRef();
        target_ = target;
        if (previous) previous->Release();
    }
    if (!cSCPropertyHelp::GetPropertyValue(target->AsPropertyHolder(), kPropertyMaxFireStage, maxStage_)) maxStage_ = 2;
    float lo[3], hi[3];
    GetBoundingBox(lo, hi);
    float width = hi[0] - lo[0], height = hi[1] - lo[1], depth = hi[2] - lo[2];
    float sideArea = (depth * height + width * height) * 2.0f;
    float topIn = width * depth;
    float top = mpTopFireCurve ? mpTopFireCurve->GetOutputValue(topIn) : 0.0f;
    topFlames_ = static_cast<std::uint32_t>(std::lrintf(top));
    float sideOut = mpSideFireCurve ? mpSideFireCurve->GetOutputValue(sideArea) : 0.0f;
    sideFlames_ = static_cast<std::uint32_t>(std::lrintf(sideOut));
    cSCPropertyHelp::SetPropertyValue(target_->AsPropertyHolder(), kPropertyOnFire, true);
    target_->SetRemovalFlags(2);
    if (target_->GetType() == kOccupantTypeBuilding && GetFireServices().buildingOnFire) GetFireServices().buildingOnFire(target_);
    return true;
}

void cSC4FireOccupant::SetStage(std::uint32_t stage) {
    if (stage_ == stage) return;
    stage_ = stage;
    if (flames_.empty()) CreateFlames();
    for (cSC4Flame* flame : flames_) flame->Update(stage);
}

void cSC4FireOccupant::CreateFlames() {
    FireServices& services = GetFireServices();
    const int zoom = services.viewZoom ? services.viewZoom() : 1;
    const std::uint32_t total = topFlames_ + sideFlames_;
    std::vector<FirePoint> points;
    const bool havePoints = services.pointsOnModel && services.pointsOnModel(total, target_, points);
    auto hideWhenFar = [&](cSC4Flame* flame) {
        if (zoom != 1 && flame->visible) {
            flame->visible = false;
            if (flame->effect) flame->effect->Stop(1);
        }
    };
    if (havePoints) {
        flames_.assign(points.size(), nullptr);
        for (std::size_t i = 0; i < points.size(); ++i) {
            auto* flame = new cSC4Flame;
            if (!flame->active) flame->active = true;
            flame->side = points[i].side != 0;
            flame->position[0] = points[i].position[0];
            flame->position[1] = points[i].position[1];
            flame->position[2] = points[i].position[2];
            flame->UpdateTransform();
            flame->quarterTurns = points[i].quarterTurns;
            flame->UpdateTransform();
            hideWhenFar(flame);
            flames_[i] = flame;
        }
        fromModelPoints_ = true;
        return;
    }
    float box[6] = {1, 1, 1, 1, 1, 1};            // min x y z, max x y z
    GetBoundingBox(box, box + 3);
    for (cSC4Flame* old : flames_) ReleaseFlame(old);
    flames_.clear();
    // do { ... } while (count <= top + side): one flame more than the counts say
    while (flames_.size() <= total) {
        auto* flame = new cSC4Flame;
        if (!flame->active) flame->active = true;
        float x = (box[0] + box[3]) * 0.5f, z = (box[2] + box[5]) * 0.5f, y = box[1];
        if (target_->GetType() != kOccupantDefaultType + 1) {
            flame->side = topFlames_ <= flames_.size();
            if (!flame->side) {
                if (y <= box[4] - 1.0f) y = box[4] - 1.0f;
                if (topFlames_ > 1) {
                    x = static_cast<float>(Random().RandomDoubleRangeGaussian(box[0], box[3]));
                    z = static_cast<float>(Random().RandomDoubleRangeGaussian(box[2], box[5]));
                }
            } else {
                std::uint32_t wall = Random().RandomUint32Uniform(4);
                switch (wall) {
                    case 0: x = box[3]; z = static_cast<float>(Random().RandomDoubleRangeUniform(box[2], box[5])); break;
                    case 1: x = static_cast<float>(Random().RandomDoubleRangeUniform(box[0], box[3])); z = box[2]; break;
                    case 2: x = box[0]; z = static_cast<float>(Random().RandomDoubleRangeUniform(box[2], box[5])); break;
                    default: x = static_cast<float>(Random().RandomDoubleRangeUniform(box[0], box[3])); z = box[5]; break;
                }
                y = static_cast<float>(Random().RandomDoubleRangeUniform(box[1], box[4]));
                flame->quarterTurns = wall;
                flame->UpdateTransform();
            }
        }
        flame->position[0] = x;
        flame->position[1] = y;
        flame->position[2] = z;
        flame->UpdateTransform();
        hideWhenFar(flame);
        flames_.push_back(flame);
    }
}

void cSC4FireOccupant::ViewUpdate() {
    if (!fromModelPoints_) return;
    FireServices& services = GetFireServices();
    std::vector<FirePoint> points;
    const int zoom = services.viewZoom ? services.viewZoom() : 1;
    const std::uint32_t count = static_cast<std::uint32_t>(flames_.size());
    if (count == 0 || !services.pointsOnModel) return;
    if (!services.pointsOnModel(count, target_, points) || points.size() != count) return;
    for (std::uint32_t i = 0; i < count; ++i) {
        cSC4Flame* flame = flames_[i];
        flame->side = points[i].side != 0;
        flame->quarterTurns = points[i].quarterTurns;
        flame->UpdateTransform();
        flame->position[0] = points[i].position[0];
        flame->position[1] = points[i].position[1];
        flame->position[2] = points[i].position[2];
        flame->UpdateTransform();
        if (zoom != 1 && flame->visible) {
            flame->visible = false;
            if (flame->effect) flame->effect->Stop(1);
        }
    }
}

bool cSC4FireOccupant::Write(cIGZOStream& stream) const {
    stream.SetUint16(3);
    if (cSC4Occupant::Write(stream)) {
        stream.SetUint32(counter_);
        stream.SetUint32(stage_);
        stream.SetUint32(maxStage_);
        SC4WriteGZSerializable(stream, target_);
        stream.SetUint32(static_cast<std::uint32_t>(flames_.size()));
        for (const cSC4Flame* flame : flames_)
            if (!flame->Write(stream)) return false;
    }
    return !stream.GetError();
}

bool cSC4FireOccupant::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != 3) return false;
    if (cSC4Occupant::Read(stream)) {
        stream.GetUint32(counter_);
        stream.GetUint32(stage_);
        stream.GetUint32(maxStage_);
        if (target_) {
            cISC4Occupant* old = target_;
            target_ = nullptr;
            old->Release();
        }
        SC4ReadGZSerializable(stream, kGZIID_cISC4Occupant, reinterpret_cast<void**>(&target_), false);
        std::uint32_t count = 0;
        stream.GetUint32(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            auto* flame = new cSC4Flame;
            if (!flame->Read(stream)) {
                delete flame;
                return false;
            }
            flames_.push_back(flame);
        }
    }
    return !stream.GetError();
}

void cSC4FireOccupant::GetPosition(float* xyz) {
    if (target_) target_->GetPosition(xyz);
    else cSC4Occupant::GetPosition(xyz);
}

void cSC4FireOccupant::GetBoundingBox(float* minXyz, float* maxXyz) {
    if (target_) target_->GetBoundingBox(minXyz, maxXyz);
    else cSC4Occupant::GetBoundingBox(minXyz, maxXyz);
}

// Zero means idle: the first call stores the amount; a later call with a larger amount clears it.
std::uint32_t cSC4FireOccupant::Extinguish(std::uint32_t amount) {
    if (extinguish_ == 0) extinguish_ = amount;
    else if (extinguish_ < amount) extinguish_ = 0;
    return extinguish_;
}

void cSC4FireOccupant::SetVisible(bool visible) {
    for (cSC4Flame* flame : flames_) flame->SetVisible(visible);
}

}  // namespace sc4

#include "sc4/VisualEffect.h"

#include <algorithm>
#include <stdexcept>

#include "rz/cRZMessage2Standard.h"
#include "s3d/S3DMath.h"
#include "sc4/ServiceLocator.h"

namespace sc4 {

namespace {
constexpr int kRotTable[4] = {1, 0, -1, 0};     // nS3D::MakeTransformFromRotation::kRotTable

void SetIdentity(cS3DTransform& t) {
    const std::uint8_t counter = t.changeCounter;
    t = cS3DTransform{};
    t.changeCounter = counter;
}
// Stores `from` into `to` keeping to's change counter advanced by `bump` (the original increments it on every store).
void Store(cS3DTransform& to, const cS3DTransform& from, int bump = 1) {
    const std::uint8_t counter = static_cast<std::uint8_t>(to.changeCounter + bump);
    to = from;
    to.changeCounter = counter;
}
// The transform the original builds in a stack temporary: counter 0, the given flags.
cS3DTransform Temp(const cS3DTransform& t) {
    cS3DTransform r = t;
    r.changeCounter = 0;
    return r;
}
}  // namespace

VisualEffectServices& GetVisualEffectServices() {
    static VisualEffectServices services;
    return services;
}

cSC4VisualEffect::cSC4VisualEffect() = default;

cSC4VisualEffect::~cSC4VisualEffect() {
    if (manager_) manager_->Release();
    for (cComponentRec& rec : components_)
        if (rec.component) rec.component->Release();
}

bool cSC4VisualEffect::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISC4VisualEffect || iid == 1) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cSC4VisualEffect::Release() {
    if (--refCount_ != 0) return refCount_;
    delete this;
    return 0;
}

void cSC4VisualEffect::SetManager(IEffectsManagerLink* manager) {
    if (manager) manager->AddRef();
    if (manager_) manager_->Release();
    manager_ = manager;
}

void cSC4VisualEffect::Shutdown() {
    if (started_) Stop(1);
    if (manager_) {
        IEffectsManagerLink* manager = manager_;
        manager_ = nullptr;
        manager->Release();
    }
}

void cSC4VisualEffect::PostMessage(std::uint32_t type, std::uint32_t data1, std::uint32_t data2, bool withData2) {
    if (!SL::spMS2) return;
    auto* message = new cRZMessage2Standard;
    message->AddRef();
    message->SetType(type);
    message->SetData1(data1);
    if (withData2) {
        message->SetData2(data2);
        message->SetIGZUnknown(this);
    }
    SL::spMS2->MessagePost(message, false);
    message->Release();
}

void cSC4VisualEffect::Start(int transition) {
    if (!manager_) return;
    for (cComponentRec& rec : components_) rec.component->Start(transition);
    if (transition != 2 && slot_ >= 0) manager_->StartAncilliary(this);
    if (startMessage_[0] != 0) {
        if (SL::spMS2) {
            auto* message = new cRZMessage2Standard;
            message->AddRef();
            message->SetType(startMessage_[0]);
            message->SetData1(startMessage_[1]);
            message->SetData2(startMessage_[2]);
            message->SetIGZUnknown(this);
            SL::spMS2->MessagePost(message, false);
            message->Release();
        }
    }
    started_ = true;
}

void cSC4VisualEffect::Stop(int transition) {
    if (!manager_) return;
    if (transition == 0) transition = static_cast<int>((flags_ >> 2) & 1);
    for (cComponentRec& rec : components_) rec.component->Stop(transition);
    if (transition != 2) {
        if (slot_ >= 0) manager_->StopAncilliary(this);
        if (transition == 0) {
            stopping_ = true;           // fading out: still started until the components report they are done
            return;
        }
    }
    started_ = false;
    stopping_ = false;
}

void cSC4VisualEffect::SetEffectTransform(const cS3DTransform& effect) { SetTransforms(source_, effect); }
void cSC4VisualEffect::SetSourceTransform(const cS3DTransform& source) { SetTransforms(source, effect_); }

void cSC4VisualEffect::SetTransforms(const cS3DTransform& source, const cS3DTransform& effect) {
    const std::uint8_t sourceCounter = source_.changeCounter, effectCounter = effect_.changeCounter;
    Store(source_, source);
    Store(effect_, effect);
    if (flags_ & 0x40) {                         // rotations are ignored
        if (source_.flags & nS3D::kTransformRotation) {
            const std::uint8_t keep = source_.flags & ~nS3D::kTransformRotation;
            cS3DTransform identity;
            std::copy(&identity.rotation[0][0], &identity.rotation[0][0] + 9, &source_.rotation[0][0]);
            source_.flags = keep;
            source_.changeCounter = static_cast<std::uint8_t>(sourceCounter + 2);
        }
        if (effect_.flags & nS3D::kTransformRotation) {
            cS3DTransform identity;
            std::copy(&identity.rotation[0][0], &identity.rotation[0][0] + 9, &effect_.rotation[0][0]);
            effect_.flags &= ~nS3D::kTransformRotation;
            effect_.changeCounter = static_cast<std::uint8_t>(effectCounter + 2);
        }
    }
    if ((flags_ & 8) == 0) {
        Store(world_, effect_);
        Store(combined_, source_);
    } else {
        cS3DTransform merged = Temp(effect_);
        nS3D::MultiplyTransform(merged, source_);
        const std::uint8_t counter = static_cast<std::uint8_t>(world_.changeCounter + 1);
        world_ = merged;
        world_.changeCounter = counter;
        if (combined_.flags != 0) {
            SetIdentity(combined_);
            combined_.flags = 0;
            ++combined_.changeCounter;
        }
    }
    if (flags_ & 1) {                            // follow the view rotation
        int zoom = 0;
        std::uint32_t rotation = 0;
        if (GetVisualEffectServices().zoomAndRotation) GetVisualEffectServices().zoomAndRotation(zoom, rotation);
        const float c = static_cast<float>(kRotTable[rotation & 3]);
        const float s = static_cast<float>(kRotTable[(rotation + 1) & 3]);
        const float t = static_cast<float>(kRotTable[(rotation + 3) & 3]);
        const float* tr = combined_.translation;
        const auto& R = world_.rotation;
        const float scale = world_.scale;
        const float px = (R[0][2] * tr[2] + R[0][1] * tr[1] + R[0][0] * tr[0]) * scale + world_.translation[0];
        const float py = (R[1][2] * tr[2] + R[1][1] * tr[1] + R[1][0] * tr[0]) * scale + world_.translation[1];
        const float pz = (R[2][2] * tr[2] + R[2][1] * tr[1] + R[2][0] * tr[0]) * scale + world_.translation[2];
        cS3DTransform view;
        view.flags = 5;
        view.rotation[0][0] = c; view.rotation[0][1] = 0; view.rotation[0][2] = s;
        view.rotation[1][0] = 0; view.rotation[1][1] = 1; view.rotation[1][2] = 0;
        view.rotation[2][0] = t; view.rotation[2][1] = 0; view.rotation[2][2] = c;
        view.translation[0] = px - (pz * s + px * c + py * 0.0f);
        view.translation[1] = py - (pz * 0.0f + px * 0.0f + py);
        view.translation[2] = pz - (pz * c + px * t + py * 0.0f);
        view.scale = 1.0f;
        nS3D::MultiplyTransform(view, world_);
        const std::uint8_t counter = static_cast<std::uint8_t>(world_.changeCounter + 1);
        world_ = view;
        world_.changeCounter = counter;
    }
    for (cComponentRec& rec : components_) {
        cS3DTransform local = Temp(combined_);
        nS3D::MultiplyTransform(local, rec.transform);
        rec.component->SetTransforms(local, world_);
    }
}

void cSC4VisualEffect::ApplyEffect(float elapsed) {
    for (cComponentRec& rec : components_)
        if (rec.component->IsStarted()) rec.component->ApplyEffect(elapsed);
}

bool cSC4VisualEffect::SetModelFromOccupant(cISC4Occupant* occupant) {
    void* model = nullptr;
    cS3DTransform transform;
    if (!GetVisualEffectServices().firstModel || !GetVisualEffectServices().firstModel(occupant, model, transform)) return false;
    flags_ &= ~std::uint64_t{1};
    SetSourceTransform(transform);
    SetVoidParams(7, &model, 1);
    return true;
}

bool cSC4VisualEffect::SetVectorParams(std::uint32_t type, const cS3DVector3* values, int count) {
    return SetFloatParams(type, &values->x, count * 3);
}

bool cSC4VisualEffect::SetFloatParams(std::uint32_t type, const float* values, int count) {
    if (type > 0x13 || count < 1 || (flags_ & 0x10)) return false;
    if (paramRecs_.empty()) paramRecs_.resize(0x14);
    cParamRec& rec = paramRecs_[type];
    if (rec.count == 0) {
        rec.offset = static_cast<std::uint16_t>(params_.size());
        rec.count = static_cast<std::uint16_t>(count);
        params_.resize(static_cast<std::size_t>(count) + rec.offset);
    } else if (rec.count != count) {
        return false;
    }
    std::copy(values, values + count, params_.begin() + rec.offset);
    bool any = false;
    for (cComponentRec& component : components_) any |= component.component->SetFloatParams(static_cast<int>(type), values, count);
    return any;
}

bool cSC4VisualEffect::SetVoidParams(std::uint32_t type, void** values, int count) {
    if (flags_ & 0x10) return false;
    bool any = false;
    for (cComponentRec& rec : components_) any |= rec.component->SetVoidParams(static_cast<int>(type), values, count);
    return any;
}

void cSC4VisualEffect::GetEffectTransform(cS3DTransform& out) const {
    const std::uint8_t counter = static_cast<std::uint8_t>(out.changeCounter + 1);
    out = effect_;
    out.changeCounter = counter;
}

void cSC4VisualEffect::GetSourceTransform(cS3DTransform& out) const {
    const std::uint8_t counter = static_cast<std::uint8_t>(out.changeCounter + 1);
    out = source_;
    out.changeCounter = counter;
}

void cSC4VisualEffect::SetDescription(const cSC4EffectDescription& description) {
    flags_ = description.id;
    finishMessage_ = 0;
    ancilliaryHandle_ = 0;
    for (cS3DTransform* t : {&effect_, &source_, &world_, &combined_}) {
        if (t->flags != 0) {
            SetIdentity(*t);
            t->flags = 0;
            ++t->changeCounter;
        }
    }
    startMessage_[0] = description.values[0];
    startMessage_[1] = description.values[1];
    startMessage_[2] = description.values[2];
    paramRecs_.clear();
}

bool cSC4VisualEffect::CheckEffectFlag(std::uint32_t flag) const {
    if (flag >= 9) throw std::out_of_range("bitset::test argument out of range");
    return (flags_ >> flag) & 1;
}

void cSC4VisualEffect::ClearForReuse() {
    for (cComponentRec& rec : components_)
        if (rec.component) rec.component->Release();
    components_.clear();
    name_.clear();
    group_ = 0;
    type_ = 0;
    slot_ = -1;
}

void cSC4VisualEffect::ApplyParams() {
    for (cComponentRec& component : components_) {
        for (std::size_t type = 0; type < paramRecs_.size(); ++type) {
            if (paramRecs_[type].count != 0)
                component.component->SetFloatParams(static_cast<int>(type), params_.data() + paramRecs_[type].offset, paramRecs_[type].count);
        }
    }
}

bool cSC4VisualEffect::CheckIfStopped() {
    if (!started_ || slot_ < 0) return true;
    if (flags_ & 2) return false;
    for (cComponentRec& rec : components_)
        if (!(rec.flags & 4) && rec.component->IsStarted()) return false;
    if ((flags_ & 0x80) && !stopping_) return false;
    Stop(1);
    if (finishMessage_ != 0) PostMessage(finishMessage_, finishData_, 0, false);
    return true;
}

bool cSC4VisualEffect::AddComponentEffect(cISC4VisualEffectComponent* component, const cS3DTransform& transform) {
    components_.emplace_back();
    cComponentRec& added = components_.back();
    added.reserved40 = 0xffffffffull;
    added.transform = transform;
    added.transform.changeCounter = 1;
    if (added.component != component) {
        if (component) component->AddRef();
        added.component = component;
    }
    cS3DTransform local = Temp(combined_);
    nS3D::MultiplyTransform(local, added.transform);
    component->SetTransforms(local, world_);
    return true;
}

}  // namespace sc4

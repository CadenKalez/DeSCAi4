#include "sc4/PlaceableObject.h"

#include <algorithm>

namespace sc4 {

namespace {
IPlaceableHost* g_host = nullptr;
}
IPlaceableHost* PlaceableHost() { return g_host; }
void SetPlaceableHost(IPlaceableHost* host) { g_host = host; }

cSC43DPlaceableObject::~cSC43DPlaceableObject() { RemoveAll(); }   // @ 0x1001205a0

bool cSC43DPlaceableObject::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x100120d3e
    if (iid == kGZIID_cISC43DPlaceableObject || iid == 1) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cSC43DPlaceableObject::Release() {   // @ 0x100120cc0
    if (refCount_ > 1) return static_cast<std::uint32_t>(--refCount_);
    FinalRelease();
    return 0;
}

void cSC43DPlaceableObject::FinalRelease() {   // @ 0x100120cde
    for (cIS3DModelInstance* model : models_) model->Release();
    models_.clear();
    delete this;
}

std::uint32_t cSC43DPlaceableObject::GetModels(cIS3DModelInstance** out, std::uint32_t& count) {   // @ 0x1001206a8
    count = std::min<std::uint32_t>(count, static_cast<std::uint32_t>(models_.size()));
    for (std::uint32_t i = 0; i < count; ++i) out[i] = models_[i];
    return count;
}

bool cSC43DPlaceableObject::AddModel(cIS3DModelInstance* model) {   // @ 0x1001206ee
    model->AddRef();
    models_.push_back(model);
    if (flags_ & 2) PlaceableHost()->AddModelToRenderer(model);
    return true;
}

bool cSC43DPlaceableObject::RemoveModel(cIS3DModelInstance* model) {   // @ 0x1001207ba
    if (flags_ & 2) PlaceableHost()->RemoveModelFromRenderer(model);
    const auto it = std::find(models_.begin(), models_.end(), model);
    if (it == models_.end()) return false;
    model->Release();
    if (it == models_.begin()) {
        *it = models_.back();       // the first slot takes the last model
        models_.pop_back();
        if (models_.empty()) models_.shrink_to_fit();
    } else {
        models_.erase(it);
    }
    return true;
}

bool cSC43DPlaceableObject::AddOverlay(std::uint32_t overlayId) {   // @ 0x10012090a
    overlays_.push_back(overlayId);
    PlaceableHost()->SetOverlayVisible(overlayId, (flags_ & 1) ? IsVisible() : false);
    return true;
}

void cSC43DPlaceableObject::SetVisibility(bool visible) {   // @ 0x1001209c8
    if (IsVisible() == visible) return;
    flags_ = (flags_ & ~2u) | (visible ? 2u : 0u);
    DoOverlayVisibilityChange();
    if (visible) AddModelsToRenderer(); else RemoveModelsFromRenderer();
}

void cSC43DPlaceableObject::DoOverlayVisibilityChange() {   // @ 0x100120a16
    const bool visible = IsVisible() && (flags_ & 1);
    for (std::uint32_t id : overlays_) PlaceableHost()->SetOverlayVisible(id, visible);
}

bool cSC43DPlaceableObject::AddModelsToRenderer() {   // @ 0x100120a7e
    // The result is the first model's registration; later failures are ignored (as in the binary).
    if (models_.empty()) return true;
    const bool first = PlaceableHost()->AddModelToRenderer(models_[0]);
    for (std::size_t i = 1; i < models_.size(); ++i) PlaceableHost()->AddModelToRenderer(models_[i]);
    return first;
}

void cSC43DPlaceableObject::RemoveModelsFromRenderer() {   // @ 0x100120b04
    for (cIS3DModelInstance* model : models_) PlaceableHost()->RemoveModelFromRenderer(model);
}

void cSC43DPlaceableObject::SetOverlaysVisible(bool visible) {   // @ 0x100120b6c
    if (((flags_ & 1) != 0) == visible) return;
    flags_ = (visible ? 1u : 0u) | (flags_ & ~1u);
    if (IsVisible()) DoOverlayVisibilityChange();
}

void cSC43DPlaceableObject::RemoveAll() {   // @ 0x100120bc2
    if (flags_ & 2) RemoveModelsFromRenderer();
    for (cIS3DModelInstance* model : models_) {
        model->SetOwner(nullptr);
        model->Release();
    }
    models_.clear();
    for (std::uint32_t id : overlays_) PlaceableHost()->RemoveOverlay(id);
    overlays_.clear();
    SetZoomLevelsCreatedFor(0);
    SetRotationsCreatedFor(0);
}

}  // namespace sc4

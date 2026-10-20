#include "sc4/ViewInputControlEffect.h"

namespace sc4 {

static IEffectToolHost* g_effectHost = nullptr;
IEffectToolHost* EffectToolHost() { return g_effectHost; }
void SetEffectToolHost(IEffectToolHost* host) { g_effectHost = host; }

cSC4ViewInputControlEffect::cSC4ViewInputControlEffect() = default;

void cSC4ViewInputControlEffect::ReleaseEffect(IToolVisualEffect*& e, bool stop) {
    if (!e) return;
    if (stop) e->Stop(0);
    IToolVisualEffect* old = e;
    e = nullptr;
    old->Release();
}

cSC4ViewInputControlEffect::~cSC4ViewInputControlEffect() {
    if (initialized_) Shutdown();
    // the original releases these once more (they are normally already null after Shutdown)
    ReleaseEffect(inactive_, false);
    ReleaseEffect(active_, false);
    ReleaseEffect(single_, false);
    ReleaseEffect(control_, false);
    ReleaseEffect(button_, false);
    if (effectsManager_) { effectsManager_->Release(); effectsManager_ = nullptr; }
}

bool cSC4ViewInputControlEffect::Init() {
    if (!initialized_) {
        cSC4ViewInputControl::Init();
        IToolEffectsManager* manager = EffectToolHost()->EffectsManager();
        IToolEffectsManager* old = effectsManager_;
        if (old != manager) {
            if (manager) manager->AddRef();
            effectsManager_ = manager;
            if (old) old->Release();
        }
        mouseDown_ = false;
    }
    return true;
}

bool cSC4ViewInputControlEffect::Shutdown() {
    if (initialized_) {
        if (capturing_) {
            capturing_ = false;
            ReleaseCapture();
        }
        if (effectsManager_) {
            IToolEffectsManager* m = effectsManager_;
            effectsManager_ = nullptr;
            m->Release();
        }
        ReleaseEffect(button_, true);
        ReleaseEffect(active_, true);
        ReleaseEffect(inactive_, true);
        ReleaseEffect(single_, true);
        ReleaseEffect(control_, true);
        cSC4ViewInputControl::Shutdown();
    }
    return true;
}

bool cSC4ViewInputControlEffect::OnKeyDown(std::uint32_t key, std::uint32_t mods) {
    return !IsKeyToBePassed(key, mods);
}

// Copies the picked terrain point into the source transform's translation and marks the transform changed.
void cSC4ViewInputControlEffect::StampPick() {
    transform_.translation[0] = pickPos_.x;
    transform_.translation[1] = pickPos_.y;
    transform_.translation[2] = pickPos_.z;
    transform_.flags |= 1;
    transform_.changeCounter = static_cast<std::uint8_t>(transform_.changeCounter + 1);
}

bool cSC4ViewInputControlEffect::OnMouseDownL(int x, int y, std::uint32_t) {
    auto* view = static_cast<IEffectView*>(view3D_);
    if (!view) return false;
    if (!view->PickTerrain(x, y, &pickPos_, pickFlag_)) return true;   // quirk: still "handled"
    StampPick();
    if (hasTrigger_) PlayTriggerEffect(transform_);
    mouseDown_ = true;
    if (active_) {
        active_->SetSourceTransform(transform_);
        active_->Start(0);
    }
    if (inactive_) inactive_->Stop(0);
    if (single_) {
        single_->SetSourceTransform(transform_);
        if (single_->IsStarted()) single_->Stop(0);
        single_->Start(0);
    }
    float one = 1.0f;
    bool pressButton = true;
    if (!control_) {
        controlWasIdle_ = false;
    } else {
        bool started = control_->IsStarted();
        controlWasIdle_ = !started;
        if (!started) {
            pressButton = false;       // an idle control effect is driven on mouse move / release instead
        } else {
            control_->SetFloatParams(0xb, &one, 1);
            control_->SetVectorParams(10, &pickPos_, 1);
        }
    }
    if (pressButton && button_) {
        button_->SetFloatParams(0xb, &one, 1);
        button_->SetVectorParams(10, &pickPos_, 1);
    }
    capturing_ = true;
    SetCapture();
    SetCursor(cursorId_);
    return true;
}

void cSC4ViewInputControlEffect::PlayTriggerEffect(const cS3DTransform& transform) {
    (void)transform;   // the original uses the tool's own transform (+0x48)
    IToolVisualEffect* effect = nullptr;
    if (!name_.empty()) {
        std::string full = name_ + "_trigger";
        effectsManager_->CreateEffect(full.c_str(), &effect);
    } else if (groupId_ != 0) {
        effectsManager_->CreateEffect(groupId_, kTypeTrigger, &effect);
    }
    if (effect) {
        effect->SetSourceTransform(transform_);
        effect->Start(0);
        effect->Release();
    }
}

bool cSC4ViewInputControlEffect::OnMouseMove(int x, int y, std::uint32_t) {
    auto* view = static_cast<IEffectView*>(view3D_);
    if (!view->PickTerrain(x, y, &pickPos_, pickFlag_)) {
        if (inactive_) inactive_->Stop(0);
        return true;
    }
    StampPick();
    if (capturing_ && !controlWasIdle_) {
        if (control_) control_->SetVectorParams(10, &pickPos_, 1);
        if (button_) button_->SetVectorParams(10, &pickPos_, 1);
    }
    if (active_) active_->SetSourceTransform(transform_);
    if (inactive_) {
        if (!mouseDown_ && !inactive_->IsStarted()) inactive_->Start(0);
        inactive_->SetSourceTransform(transform_);
    }
    return true;
}

bool cSC4ViewInputControlEffect::OnMouseUpL(int, int, std::uint32_t mods) {
    if (!capturing_) return false;
    mouseDown_ = false;
    if (active_) active_->Stop(0);
    if (inactive_) {
        inactive_->SetSourceTransform(transform_);
        inactive_->Start(0);
    }
    float zero = 0;
    if (control_) control_->SetFloatParams(0xb, &zero, 1);
    if (button_) button_->SetFloatParams(0xb, &zero, 1);
    if (control_) {
        if (controlWasIdle_) SetTerrainControlledEffectState(true);
        else if (mods & 2) SetTerrainControlledEffectState(false);
    }
    ReleaseCapture();
    capturing_ = false;
    return true;
}

bool cSC4ViewInputControlEffect::OnMouseExit() {
    if (inactive_) inactive_->Stop(0);
    return cSC4ViewInputControl::OnMouseExit();
}

void cSC4ViewInputControlEffect::SetTerrainControlledEffectState(bool on) {
    if (!control_) return;       // the original had no null check
    if (on) {
        control_->SetSourceTransform(transform_);
        control_->Start(0);
    } else {
        control_->Stop(0);
    }
}

void cSC4ViewInputControlEffect::SetEffectName(const char* name) {
    name_ = name;
    hasButtonTrigger_ = effectsManager_->HasEffect((name_ + "_button_trigger").c_str());
    hasTrigger_ = effectsManager_->HasEffect((name_ + "_trigger").c_str());
    bool any = hasButtonTrigger_ || hasTrigger_;
    auto load = [&](const char* suffix, IToolVisualEffect*& slot) {
        std::string full = name_ + suffix;
        if (slot) { IToolVisualEffect* old = slot; slot = nullptr; old->Release(); }
        return effectsManager_->CreateEffect(full.c_str(), &slot);
    };
    bool b = load("_button", button_);
    bool s = load("_single", single_);
    bool c = load("_control", control_);
    bool a = load("_active", active_);
    bool i = load("_inactive", inactive_);
    anyEffect_ = any | b | s | c | a | i;
    if (button_) button_->Start(0);
}

void cSC4ViewInputControlEffect::SetEffectGroupID(std::uint32_t groupId) {
    groupId_ = groupId;
    hasButtonTrigger_ = effectsManager_->HasEffect((name_ + "_button_trigger").c_str());
    hasTrigger_ = effectsManager_->HasEffect((name_ + "_trigger").c_str());
    bool any = hasButtonTrigger_ || hasTrigger_;
    auto load = [&](int type, IToolVisualEffect*& slot) {
        if (slot) { IToolVisualEffect* old = slot; slot = nullptr; old->Release(); }
        return effectsManager_->CreateEffect(groupId_, type, &slot);
    };
    bool b = load(kTypeButton, button_);
    bool s = load(kTypeSingle, single_);
    bool c = load(kTypeControl, control_);
    bool a = load(kTypeActive, active_);
    bool i = load(kTypeInactive, inactive_);
    anyEffect_ = any | b | s | c | a | i;
    if (button_) button_->Start(0);
}

}  // namespace sc4

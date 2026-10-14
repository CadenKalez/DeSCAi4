#include "sc4audio/SemManager.h"

bool cSC4AudioSemManager::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != kGZIID_cIGZUnknown && iid != kGZIID_cSC4AudioSemManager) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4AudioSemManager::Release() {
    if (refCount_ == 1) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

bool cSC4AudioSemManager::Init(cIHitMan* hitMan) {
    if (hitMan == nullptr) return false;
    if (hitMan_ != hitMan) {
        hitMan->AddRef();
        if (hitMan_ != nullptr) hitMan_->Release();
        hitMan_ = hitMan;
    }
    default2D_ = hitMan_->GetSem(0x7dcded2d);
    if (!default2D_) return false;
    default3D_ = hitMan_->GetSem(0x7dcded3d);
    if (!default3D_) return false;

    model2D_ = new cSC4AudioEmanationModel2D();
    if (!model2D_->Init()) return false;
    model3D_ = new cSC4AudioEmanationModel3D();
    if (!model3D_->Init()) return false;
    effects_ = new cSC4AudioEmanationModel3DEffects();
    if (!effects_->Init()) return false;

    hitMan_->RegisterSem(effects_.get());
    hitMan_->RemoveSem(0x7dcded2d);
    hitMan_->RegisterSem(model2D_.get());
    hitMan_->RemoveSem(0x7dcded3d);
    hitMan_->RegisterSem(model3D_.get());
    return true;
}

bool cSC4AudioSemManager::Shutdown() {
    if (hitMan_ != nullptr) {
        hitMan_->RemoveSem(0x7dcded2d);
        hitMan_->RegisterSem(default2D_.get());
        hitMan_->RemoveSem(0x7dcded3d);
        hitMan_->RegisterSem(default3D_.get());
        hitMan_->RemoveSem(0x0a63a543);
    }
    if (model2D_) { model2D_->Shutdown(); model2D_ = nullptr; }
    if (model3D_) { model3D_->Shutdown(); model3D_ = nullptr; }
    if (effects_) { effects_->Shutdown(); effects_ = nullptr; }
    default2D_ = nullptr;
    default3D_ = nullptr;
    return true;
}

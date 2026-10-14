#include "audio/GZSnd3D.h"

#include <algorithm>
#include <cmath>

namespace GZSnd3D {
namespace {
cGZSnd3DListener* g_listener = nullptr;
}
cGZSnd3DListener* ActiveListener() { return g_listener; }
void SetActiveListener(cGZSnd3DListener* listener) { g_listener = listener; }

std::vector<cGZSnd3D*>& SoundList() {
    static std::vector<cGZSnd3D*> list;
    return list;
}
std::recursive_mutex& SoundListLock() {
    static std::recursive_mutex lock;
    return lock;
}
}  // namespace GZSnd3D

namespace {
struct Vec {
    float v[3];
};
float Dot(const Vec& a, const Vec& b) { return a.v[0] * b.v[0] + a.v[1] * b.v[1] + a.v[2] * b.v[2]; }
Vec Cross(const Vec& a, const Vec& b) {
    return {{a.v[1] * b.v[2] - a.v[2] * b.v[1], a.v[2] * b.v[0] - a.v[0] * b.v[2], a.v[0] * b.v[1] - a.v[1] * b.v[0]}};
}
}  // namespace

// ============================================================================================
// cGZSnd3D
// ============================================================================================

cGZSnd3D::cGZSnd3D() {
    pcm_.SetIs3D(true);
    std::lock_guard<std::recursive_mutex> guard(GZSnd3D::SoundListLock());
    GZSnd3D::SoundList().push_back(this);
}

// ~cGZSnd3D @ 0x100627620: leaves the global list, then the cGZSnd destructor runs.
cGZSnd3D::~cGZSnd3D() {
    std::lock_guard<std::recursive_mutex> guard(GZSnd3D::SoundListLock());
    auto& list = GZSnd3D::SoundList();
    const auto it = std::find(list.begin(), list.end(), this);
    if (it != list.end()) list.erase(it);
}

bool cGZSnd3D::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSnd3D) {
        *out = static_cast<cIGZSnd3D*>(this);
        AddRef();
        return true;
    }
    return cGZSnd::QueryInterface(iid, out);
}

bool cGZSnd3D::SetPosition(float x, float y, float z) {
    position_[0] = -y;
    position_[1] = z;
    position_[2] = x;
    dirty_ = true;
    Update3DSound();
    return true;
}

bool cGZSnd3D::SetMinimumDistance(float distance) {
    minimumDistance_ = distance;
    dirty_ = true;
    Update3DSound();
    return true;
}

bool cGZSnd3D::SetMaximumDistance(float distance) {
    maximumDistance_ = distance;
    dirty_ = true;
    Update3DSound();
    return true;
}

bool cGZSnd3D::SetHeadRelative(bool headRelative) {
    headRelative_ = headRelative;
    dirty_ = true;
    return true;
}

void cGZSnd3D::Update3DSound() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    cGZSnd3DListener* listener = GZSnd3D::ActiveListener();
    if (!listener) return;
    dirty_ = false;

    const float* lp = listener->position();
    Vec relative;
    if (headRelative_) {
        relative = {{lp[0] + position_[0], lp[1] + position_[1], lp[2] + position_[2]}};
    } else {
        relative = {{position_[0] - lp[0], position_[1] - lp[1], position_[2] - lp[2]}};
    }
    float distance = std::sqrt(relative.v[0] * relative.v[0] + relative.v[1] * relative.v[1] + relative.v[2] * relative.v[2]);
    if (distance != 0.0f || std::isnan(distance)) {
        if (minimumDistance_ <= distance) {
            if (maximumDistance_ <= distance && distance != maximumDistance_) distance = maximumDistance_;
        } else {
            distance = minimumDistance_;
        }
    }

    float volumeA, volumeB;   // SetVolumes(A, B): A scales with the sound being on the listener's right
    if (0.1f < distance) {
        const float base = minimumDistance_ < 1.0f ? 1.0f : minimumDistance_;
        float attenuation = base / distance;
        if (1.0f < attenuation) attenuation = 1.0f;
        const float inverse = 1.0f / distance;
        relative.v[0] *= inverse;
        relative.v[1] *= inverse;
        relative.v[2] *= inverse;
        const Vec up = {{listener->up()[0], listener->up()[1], listener->up()[2]}};
        const Vec front = {{listener->front()[0], listener->front()[1], listener->front()[2]}};
        const float side = Dot(Cross(up, front), relative);
        volumeA = attenuation * (side * 0.375f + 0.375f + 0.25f);
        volumeB = attenuation * (-side * 0.375f + 0.375f + 0.25f);
    } else {
        volumeA = volumeB = static_cast<float>(MasterVolume()) * 0.8125f;
    }
    volumeB = volumeB > 1.0f ? 1.0f : (volumeB < 0.0f ? 0.0f : volumeB);
    volumeA = volumeA > 1.0f ? 1.0f : (volumeA < 0.0f ? 0.0f : volumeA);
    if (GetDataSource() != nullptr) pcm_.SetVolumes(volumeA, volumeB);
}

// ============================================================================================
// cGZSnd3DListener
// ============================================================================================

bool cGZSnd3DListener::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == 0x406077d2) {
        *out = static_cast<cIGZSnd3DListener*>(this);
    } else if (iid == 0x5b30e7b3) {
        *out = static_cast<cIGZSnd3DListenerEx*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cGZSnd3DListener::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

bool cGZSnd3DListener::Init() {
    position_[0] = position_[1] = position_[2] = 0.0f;
    front_[0] = front_[1] = front_[2] = 0.0f;
    up_[0] = up_[1] = up_[2] = 0.0f;
    dirty_ = true;
    return true;
}

// @ 0x1006290b0: re-evaluates every loaded positional sound when the listener changed (or the
// sound itself is dirty).
bool cGZSnd3DListener::CommitDeferredSettings() {
    if (!dirty_) return true;
    {
        std::lock_guard<std::recursive_mutex> guard(GZSnd3D::SoundListLock());
        for (cGZSnd3D* sound : GZSnd3D::SoundList()) {
            if (sound && sound->IsLoaded() && (dirty_ || sound->needsUpdate())) sound->Update3DSound();
        }
    }
    dirty_ = false;
    return true;
}

bool cGZSnd3DListener::SetPosition(float x, float y, float z) {
    position_[0] = -y;
    position_[1] = z;
    position_[2] = x;
    dirty_ = true;
    CommitDeferredSettings();
    return true;
}

bool cGZSnd3DListener::SetOrientation(float fx, float fy, float fz, float ux, float uy, float uz) {
    front_[0] = -fy; front_[1] = fz; front_[2] = fx;
    up_[0] = -uy; up_[1] = uz; up_[2] = ux;
    dirty_ = true;
    return true;
}

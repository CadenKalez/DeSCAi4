#include "sc4audio/Listener.h"

#include <cmath>

#include "audio/interfaces/cIGZSndEventStd.h"
#include "rz/cRZMessage2Standard.h"
#include "sc4/SC4GeometryUtilities.h"

using namespace SC4Audio;

namespace {

// _ReadTuningValues()::kfZoomDistances / kfCameraPulls @ 0x100a20210 / 0x100a20230.
constexpr float kDefaultZoomDistances[kZoomLevels] = {1336.0f, 667.0f, 333.0f, 145.0f, 73.0f};
constexpr float kDefaultCameraPulls[kZoomLevels] = {1.0f, 0.75f, 0.5f, 0.25f, 0.0f};

struct Vec3 {
    float v[3];
};
Vec3 Sub(const float* a, const float* b) { return {{a[0] - b[0], a[1] - b[1], a[2] - b[2]}}; }
Vec3 Cross(const Vec3& a, const Vec3& b) {
    return {{a.v[1] * b.v[2] - a.v[2] * b.v[1], a.v[2] * b.v[0] - a.v[0] * b.v[2], a.v[0] * b.v[1] - a.v[1] * b.v[0]}};
}
void Normalize(Vec3& a) {
    const float length = std::sqrt(a.v[0] * a.v[0] + a.v[1] * a.v[1] + a.v[2] * a.v[2]);
    if (length > 0.0f) {
        a.v[0] /= length; a.v[1] /= length; a.v[2] /= length;
    }
}

// Tuning-property vectors are always five entries long here (see ReadTuningValues).
void FillFive(std::vector<float>& out, const float (&defaults)[kZoomLevels]) {
    out.assign(defaults, defaults + kZoomLevels);
}

}  // namespace

bool cSC4AudioListener::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 0xca4a66b9 || iid == kGZIID_cIGZUnknown || iid == 0x452294aa) {
        if (out) *out = static_cast<cIGZMessageTarget2*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cSC4AudioListener::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

// cSC4AudioListener::Init() @ 0x1003f57d0: listens for the city lifecycle and tuning-reload messages.
bool cSC4AudioListener::Init() {
    if (initialised_) return true;
    messageServer_ = GetServices().messageServer;
    if (messageServer_) {
        cIGZMessageTarget2* target = this;
        messageServer_->AddNotification(target, kMessageCityInit);
        messageServer_->AddNotification(target, kMessageCityShutdown);
        messageServer_->AddNotification(target, 0xc53d10aa);
    }
    initialised_ = true;
    return true;
}

// cSC4AudioListener::Shutdown() @ 0x1003f5a60
bool cSC4AudioListener::Shutdown() {
    if (!initialised_) return true;
    if (messageServer_) {
        cIGZMessageTarget2* target = this;
        messageServer_->RemoveNotification(target, kMessageCityInit);
        messageServer_->RemoveNotification(target, kMessageCityShutdown);
        messageServer_->RemoveNotification(target, 0xc53d10aa);
    }
    messageServer_ = nullptr;
    soundSystem_ = nullptr;
    initialised_ = false;
    return true;
}

// cSC4AudioListener::DoMessage @ 0x1003f5c00
bool cSC4AudioListener::DoMessage(cIGZMessage2* message) {
    if (!message) return false;
    switch (message->GetType()) {
        case kMessageCameraMoved:
        case kMessageCameraZoomed:
            UpdateListener();
            break;
        case 0xc53d10aa: {   // tuning reload
            ReadTuningValues();
            CalculateZoomValues();
            if (soundSystem_) {
                void* raw = nullptr;
                if (soundSystem_->EventCreateWithData(kEventListenerTuningChanged, kGZCLSID_cGZSndEventStd, &raw, 0, 0, 0, 0)) {
                    auto* event = static_cast<cIGZSndEventStd*>(raw);
                    if (event) {
                        soundSystem_->PostEvent(event);
                        event->Release();
                    }
                }
            }
            UpdateListener();
            break;
        }
        case kMessageCheatCode: HandleCheatCodeMessage(message); break;
        case kMessageCityInit:
        case kMessageCityShutdown: HandleCityInitShutdownMessages(message); break;
        default: break;
    }
    return true;
}

// @ 0x1003f5e80: only checks that the message carries the standard payload; nothing else happens.
bool cSC4AudioListener::HandleCheatCodeMessage(cIGZMessage2* message) {
    if (!message) return false;
    void* raw = nullptr;
    if (!message->QueryInterface(kGZIID_cIGZMessage2Standard, &raw)) return false;
    static_cast<cIGZMessage2Standard*>(raw)->Release();
    return true;
}

// @ 0x1003f5f80. City init: take the sound system, follow camera messages, load tuning and place
// the listener. City shutdown: stop following and drop the sound system.
bool cSC4AudioListener::HandleCityInitShutdownMessages(cIGZMessage2* message) {
    if (!message) return false;
    void* raw = nullptr;
    if (!message->QueryInterface(kGZIID_cIGZMessage2Standard, &raw)) return false;
    auto* standard = static_cast<cIGZMessage2Standard*>(raw);
    bool result = true;
    const std::uint32_t type = message->GetType();
    if (type == kMessageCityInit) {
        soundSystem_ = GetServices().soundSystem;
        if (!soundSystem_) {
            result = false;
        } else {
            if (messageServer_) {
                cIGZMessageTarget2* target = this;
                messageServer_->AddNotification(target, kMessageCameraMoved);
                messageServer_->AddNotification(target, kMessageCameraZoomed);
            }
            ReadTuningValues();
            CalculateZoomValues();
            UpdateListener();
        }
    } else if (type == kMessageCityShutdown) {
        if (messageServer_) {
            cIGZMessageTarget2* target = this;
            messageServer_->RemoveNotification(target, kMessageCameraMoved);
            messageServer_->RemoveNotification(target, kMessageCameraZoomed);
        }
        soundSystem_ = nullptr;
    }
    standard->Release();
    return result;
}

// cSC4AudioListener::_ReadTuningValues() @ 0x1003f6b30. Deviations: the zoom-distance and
// multiplier tables are always resized to five entries (the original writes defaults into a
// possibly empty vector); tables the exemplar provides with fewer than five entries are padded
// with zeros.
void cSC4AudioListener::ReadTuningValues() {
    cIExemplarSource* exemplars = GetServices().exemplars;
    const cGZPersistResourceKey key{0x6534284a, 0xca25875d, 0x6a359c5c};
    if (!exemplars || !exemplars->ExemplarExists(key)) return;

    exemplars->GetFloatProperty(key, 0x2a36e3cb, rolloffFactor_);
    if (soundSystem_) {
        if (cIGZSnd3DListener* listener = soundSystem_->AsIGZSnd3DListener()) {
            if (cIGZSnd3DListenerEx* extended = listener->AsIGZSnd3DListenerEx()) extended->SetRolloffFactor(rolloffFactor_);
        }
    }

    if (!exemplars->GetFloatArrayProperty(key, 0xca359f17, zoomDistances_)) FillFive(zoomDistances_, kDefaultZoomDistances);
    if (!exemplars->GetFloatArrayProperty(key, 0x6a359f22, zoomMultipliers_)) {
        zoomMultipliers_.assign(kZoomLevels, 1.0f);
    }
    exemplars->GetFloatProperty(key, 0x2a35a25f, zoomScale_);
    if (!exemplars->GetFloatArrayProperty(key, 0xea4a7c55, cameraPulls_)) FillFive(cameraPulls_, kDefaultCameraPulls);
    if (!exemplars->GetFloatArrayProperty(key, 0x2aa40ccc, movementThresholds_)) {
        movementThresholds_.resize(kZoomLevels);
        float value = 26.0f;
        for (int i = 0; i < kZoomLevels; ++i) {
            movementThresholds_[static_cast<std::size_t>(i)] = value;
            value /= 2.0f;
        }
    }
    for (auto* table : {&zoomDistances_, &zoomMultipliers_, &cameraPulls_, &movementThresholds_}) {
        if (table->size() < kZoomLevels) table->resize(kZoomLevels, 0.0f);
    }
    for (int i = 0; i < kZoomLevels; ++i) {
        float& t = movementThresholds_[static_cast<std::size_t>(i)];
        t = t * t * 16.0f * 16.0f;
    }
}

// cSC4AudioListener::_CalculateZoomValues() @ 0x1003f7330: publishes the distances other audio
// code reads through SC4Audio::ZoomDistances().
void cSC4AudioListener::CalculateZoomValues() {
    if (zoomDistances_.size() < kZoomLevels || zoomMultipliers_.size() < kZoomLevels) return;
    float* out = ZoomDistances();
    for (int i = kZoomLevels - 1; i >= 0; --i) {
        const auto index = static_cast<std::size_t>(i);
        out[i] = zoomDistances_[index] * zoomMultipliers_[index] * zoomScale_;
    }
}

SC4Audio::ListenerView cSC4AudioListener::View() const {
    ListenerView view;
    for (int i = 0; i < 3; ++i) {
        view.eye[i] = eye_[i];
        view.target[i] = target_[i];
    }
    return view;
}

// cSC4AudioListener::UpdateListener() @ 0x1003f62c0
//
// 1. target = the camera's look-at point (zoom levels below 2: the centre of the city); a target
//    below sea level is replaced by the camera ray's intersection with the sea-level plane.
// 2. The listener is placed straight above the target by the zoom level's distance and oriented
//    to look down (up = the negated horizontal axis crossed with the view direction).
// 3. When the camera moved further than the zoom level's threshold since the last notification,
//    message 0x8a4a69c7 is posted with the listener as its unknown and its position as data 1.
bool cSC4AudioListener::UpdateListener() {
    Services& services = GetServices();
    cIGameAudioContext* game = services.game;
    if (!game || !game->HasOccupantManager()) return false;
    cIViewport* view = ViewUtilities();
    if (!view) return true;

    const std::int32_t zoom = view->GetZoomLevel();
    float cameraPosition[3], target[3];
    Vec3 up;
    view->GetCameraEye(cameraPosition);
    view->GetCameraTarget(target);
    view->GetCameraUp(up.v);

    if (zoom < 2) {
        float width = 0.0f, height = 0.0f;
        game->GetCityExtentMeters(width, height);
        target[0] = width / 2.0f;
        target[2] = height / 2.0f;
    }

    Vec3 forward = Sub(target, cameraPosition);
    Normalize(forward);
    const Vec3 right = Cross(forward, up);

    const float seaLevel = game->GetSeaLevel();
    if (target[1] < seaLevel) {
        const float plane[4] = {0.0f, 1.0f, 0.0f, -seaLevel};
        SC4GeometryUtilities::RayIntersectsPlane(cameraPosition, forward.v, plane, target);
    }

    const std::size_t level = zoom >= 0 && zoom < kZoomLevels ? static_cast<std::size_t>(zoom) : 0;
    const float zoomDistance = ZoomDistances()[level];

    eye_[0] = target[0];
    eye_[1] = target[1] + zoomDistance;
    eye_[2] = target[2];
    const Vec3 pull = {{forward.v[0] * zoomDistance, forward.v[1] * zoomDistance, forward.v[2] * zoomDistance}};
    pull_[0] = target[0] - pull.v[0];
    pull_[1] = target[1] - pull.v[1];
    pull_[2] = target[2] - pull.v[2];

    Vec3 front = Sub(target, eye_);
    Normalize(front);
    Vec3 listenerUp = Cross(right, front);
    Normalize(listenerUp);
    listenerUp = {{-listenerUp.v[0], -listenerUp.v[1], -listenerUp.v[2]}};
    target_[0] = target[0];
    target_[1] = target[1];
    target_[2] = target[2];

    if (soundSystem_) {
        if (cIGZSnd3DListener* listener = soundSystem_->AsIGZSnd3DListener()) {
            listener->SetOrientation(front.v[0], front.v[1], front.v[2], listenerUp.v[0], listenerUp.v[1], listenerUp.v[2]);
            listener->SetPosition(eye_[0], eye_[1], eye_[2]);
        }
    }

    const float moved = SC4GeometryUtilities::LengthOfLineSquared(lastCamera_, cameraPosition);
    if (movementThresholds_.size() == kZoomLevels && movementThresholds_[level] <= moved && moved != movementThresholds_[level]) {
        lastCamera_[0] = cameraPosition[0];
        lastCamera_[1] = cameraPosition[1];
        lastCamera_[2] = cameraPosition[2];
        if (messageServer_) {
            cRZAutoRefCount<cRZMessage2Standard> message(new cRZMessage2Standard());
            message->SetType(kMessageListenerMoved);
            message->SetIGZUnknown(this);
            message->SetVoid1(eye_);
            messageServer_->MessagePost(message.get(), false);
        }
    }
    return true;
}

#include "audio/GZSndSysMac.h"

#include "audio/AudioServices.h"
#include "audio/GZSnd3D.h"

bool cGZSndSysMac::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSndRenderer) {
        *out = static_cast<cIGZSndRenderer*>(this);
    } else if (iid == 0x287fb697) {
        *out = static_cast<cIGZSystemService*>(this);
    } else if (iid == 0x406077d2 || iid == 0x5b30e7b3) {
        cGZSnd3DListener* listener = GZSnd3D::ActiveListener();
        return listener ? listener->QueryInterface(iid, out) : false;
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cGZSndSysMac::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

// cGZSndSysMac::Init() @ 0x1006361b0: registers with the sound system and creates the listener.
bool cGZSndSysMac::Init() {
    if (cIGZSndSys* system = Audio::SoundSystem()) system->RegisterRenderer(static_cast<cIGZSndRenderer*>(this));
    auto* listener = new cGZSnd3DListener();
    listener->AddRef();
    GZSnd3D::SetActiveListener(listener);
    listener->Init();
    return true;
}

bool cGZSndSysMac::Shutdown() {
    if (cIGZSndSys* system = Audio::SoundSystem()) system->UnregisterRenderer(static_cast<cIGZSndRenderer*>(this));
    if (cGZSnd3DListener* listener = GZSnd3D::ActiveListener()) {
        listener->Release();
        GZSnd3D::SetActiveListener(nullptr);
    }
    return true;
}

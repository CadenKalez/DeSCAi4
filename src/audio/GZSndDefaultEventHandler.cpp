#include "audio/GZSndDefaultEventHandler.h"

#include "audio/AudioServices.h"
#include "audio/interfaces/cIGZSnd.h"

cGZSndDefaultEventHandler::cGZSndDefaultEventHandler() {
    soundSystem_ = Audio::SoundSystem();
    soundManager_ = Audio::SoundManagerService();
}

bool cGZSndDefaultEventHandler::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cGZSndDefaultEventHandler) {
        *out = static_cast<cIGZSndEventHandler*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZSndDefaultEventHandler::Release() {
    if (refCount_ == 1) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

bool cGZSndDefaultEventHandler::DoEvent(cIGZSndEventStd* event) {
    cIGZMessage2Standard* message = event->AsMessage2Standard();
    const std::uint32_t id = event->GetEventId();

    // The sound a play / stop / kill event refers to (AddRef'd), or null.
    const auto findSound = [&]() -> cIGZSnd* {
        const auto soundId = static_cast<std::uint32_t>(message->GetData1());
        if (soundId == 0 || !soundManager_) return nullptr;
        void* raw = nullptr;
        if (!soundManager_->GetSoundById(soundId, 0, nullptr, kGZIID_cIGZSnd, &raw)) return nullptr;
        return static_cast<cIGZSnd*>(raw);
    };
    cIGZSndPropertyCache* cache = soundSystem_ ? soundSystem_->GetSndPropertyCache() : nullptr;

    switch (id) {
        case 1: {
            cIGZSnd* snd = findSound();
            if (!snd) return false;
            snd->PlayWithVolumeAndLoop(0x400, false, 1);
            snd->Release();
            return true;
        }
        case 2:
            if (cIGZSnd* snd = findSound()) {
                snd->Stop();
                snd->Release();
            }
            return false;
        case 3: return false;
        case 4:
            if (cIGZSnd* snd = findSound()) {
                snd->Kill();
                snd->Release();
            }
            return false;
        case 0xca3553d6:
            if (cache) cache->AddInstance(static_cast<std::uint32_t>(message->GetData1()));
            return false;
        case 0xca3553e7:
            if (cache) cache->RemoveInstance(static_cast<std::uint32_t>(message->GetData1()));
            return false;
        case 0xea355407:
            if (cache) {
                cache->SetInstancePropertyUint32(static_cast<std::uint32_t>(message->GetData1()),
                                                 static_cast<std::uint32_t>(message->GetData2()),
                                                 static_cast<std::uint32_t>(message->GetData3()));
            }
            return false;
        case 0x0a35540c:
            if (cache) {
                cache->RemoveInstanceProperty(static_cast<std::uint32_t>(message->GetData1()),
                                              static_cast<std::uint32_t>(message->GetData2()));
            }
            return false;
        case 0x0a355411:
            if (cache) {
                cache->SetGlobalPropertyUint32(static_cast<std::uint32_t>(message->GetData1()),
                                               static_cast<std::uint32_t>(message->GetData2()));
            }
            return false;
        case 0x0a355417:
            if (cache) cache->RemoveGlobalProperty(static_cast<std::uint32_t>(message->GetData1()));
            return false;
        default: return false;
    }
}

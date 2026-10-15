#include "audio/HitManDirector.h"

namespace sc4audio {

cHitManCOMDirector::cHitManCOMDirector(IHitDirectorHost* host) : host_(host) {
    for (std::uint32_t classId : kHitManClassIds) host_->AddClass(classId);
}

bool cHitManCOMDirector::OnStart(cIGZUnknown* com) {
    host_->AddSystemServiceDirector(com);
    for (std::uint32_t classId : {HitIds::kClsidHitMan, HitIds::kClsidHitDebugInfo}) {
        cIGZUnknown* service = nullptr;
        host_->CreateInstance(classId, 0x287fb697, reinterpret_cast<void**>(&service));
        if (service) {
            host_->RegisterSystemService(service);
            service->Release();
        }
    }
    return true;
}

bool cHitManCOMDirector::PreAppInit() {
    cIGZPersistResourceManager* resources = host_->ResourceManager();
    for (std::uint32_t type : kHitFactoryResourceTypes) resources->RegisterObjectFactory(HitIds::kClsidHitResourceFactory, type, nullptr);
    cIGZSndMgr* sounds = host_->SoundManager();
    cIGZSoundCreator* creator = nullptr;
    host_->CreateInstance(HitIds::kClsidHitSegmentDataSourceChannelCreator, kGZIID_cIGZSoundCreator, reinterpret_cast<void**>(&creator));
    sounds->RegisterSndCreator(creator);
    if (creator) creator->Release();
    return true;
}

bool cHitManCOMDirector::PreAppShutdown() {
    if (cIHitMan* hitMan = host_->HitManager()) hitMan->Shutdown();
    if (cIHitDebugInfo* debug = host_->DebugInfo()) debug->Shutdown();
    return true;
}

}  // namespace sc4audio

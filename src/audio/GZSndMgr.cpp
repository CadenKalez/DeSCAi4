#include "audio/GZSndMgr.h"

#include <vector>

#include "audio/AudioServices.h"
#include "rz/nRZHash.h"

cGZSndMgr::cGZSndMgr() : cRZSystemService(kServiceId, kServicePriority) {}

// ~cGZSndMgr @ 0x1004a1370: Shutdown runs first (virtual), containers and references follow.
cGZSndMgr::~cGZSndMgr() { Shutdown(); }

bool cGZSndMgr::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZSndMgr) {
        if (!out) return false;
        *out = static_cast<cIGZSndMgr*>(this);
        AddRef();
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

// cGZSndMgr::Release() @ 0x1004a18d0: the last release shuts the service down first.
std::uint32_t cGZSndMgr::Release() {
    if (RefCount() == 1) Shutdown();
    return cRZSystemService::Release();
}

// cGZSndMgr::Init() @ 0x1004a1970: takes the sound system, asks for a 250 ms update tick and
// keeps the persistence manager. The flag is set even when the sound system is missing.
bool cGZSndMgr::Init() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (initialised_) return true;
    initialised_ = true;
    soundSystem_ = Audio::SoundSystem();
    if (!soundSystem_) return false;
    soundSystem_->RegisterTarget(static_cast<cIGZSndUpdateTarget*>(this), kUpdateIntervalMs);
    persist_ = Audio::PersistService();
    return true;
}

// cGZSndMgr::Shutdown() @ 0x1004a1c60. Data sources and the sound system / persistence
// references are kept (the original does not touch them).
bool cGZSndMgr::Shutdown() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    // Move the contents out first so releases that re-enter the manager see consistent state.
    auto autoPlay = std::move(autoPlay_);
    autoPlay_.clear();
    auto systemSounds = std::move(systemSounds_);
    systemSounds_.clear();
    auto sounds = std::move(sounds_);
    sounds_.clear();
    auto soundCreators = std::move(soundCreators_);
    soundCreators_.clear();
    auto dataSourceCreators = std::move(dataSourceCreators_);
    dataSourceCreators_.clear();
    unusedIds_.clear();
    initialised_ = false;
    return true;
}

bool cGZSndMgr::GetResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out) {
    if (!persist_) return false;
    return persist_->GetResource(key, iid, out, 0, 0);
}

std::uint32_t cGZSndMgr::SystemSndID(std::int32_t index) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const auto it = systemSounds_.find(index);
    return it == systemSounds_.end() ? 0u : it->second;
}

bool cGZSndMgr::SetSystemSndID(std::int32_t index, std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    systemSounds_[index] = id;
    return true;
}

// cGZSndMgr::RegisterSnd @ 0x1004a6260: an id keeps its first registration.
void cGZSndMgr::RegisterSnd(cIGZSnd* snd, std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    sounds_.emplace(id, cRZAutoRefCount<cIGZSnd>(snd));
}

void cGZSndMgr::UnregisterSndById(std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const auto it = sounds_.find(id);
    if (it != sounds_.end()) sounds_.erase(it);
}

void cGZSndMgr::UnregisterSndByPointer(cIGZSnd* snd) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto it = sounds_.begin(); it != sounds_.end(); ++it) {
        if (it->second.get() == snd) {
            sounds_.erase(it);
            return;
        }
    }
}

// @ 0x1004a47b0: the creator must implement cIGZSoundCreator; a creator registered before is
// replaced and the newest creator is asked first.
bool cGZSndMgr::RegisterSndCreator(cIGZSoundCreator* creator) {
    if (!creator) return false;
    void* raw = nullptr;
    if (!creator->QueryInterface(kGZIID_cIGZSoundCreator, &raw)) return false;
    static_cast<cIGZUnknown*>(raw)->Release();
    RemoveSndCreator(creator);
    std::lock_guard<std::recursive_mutex> guard(lock_);
    soundCreators_.push_front(cRZAutoRefCount<cIGZSoundCreator>(creator));
    return true;
}

bool cGZSndMgr::RemoveSndCreator(cIGZSoundCreator* creator) {
    cRZAutoRefCount<cIGZSoundCreator> keepAlive;   // released outside the lock
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto it = soundCreators_.begin(); it != soundCreators_.end(); ++it) {
        if (it->get() == creator) {
            keepAlive = *it;
            soundCreators_.erase(it);
            return true;
        }
    }
    return false;
}

// cGZSndMgr::GetSound(unsigned int, ...) @ 0x1004a5a50: nothing is built while the sound system
// has no renderer; a registered sound answers directly, otherwise the creators are asked in order.
bool cGZSndMgr::GetSoundById(std::uint32_t id, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) {
    if (!soundSystem_ || soundSystem_->NumRenderers() == 0) return false;
    cRZAutoRefCount<cIGZSnd> registered;
    {
        std::lock_guard<std::recursive_mutex> guard(lock_);
        const auto it = sounds_.find(id);
        if (it != sounds_.end() && it->second) registered = it->second;
    }
    if (registered) return registered->QueryInterface(iid, out);

    std::vector<cRZAutoRefCount<cIGZSoundCreator>> creators;
    {
        std::lock_guard<std::recursive_mutex> guard(lock_);
        creators.assign(soundCreators_.begin(), soundCreators_.end());
    }
    for (auto& creator : creators) {
        if (creator->CreateSound(id, flags, variant, iid, out)) return true;
    }
    return false;
}

// @ 0x1004a5970: a name maps to the id CRC24(name) | 0xff000000.
bool cGZSndMgr::GetSoundByName(cIGZString& name, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) {
    const std::uint32_t id = nRZHash::CRC24(name.Data(), false) | 0xff000000u;
    return GetSoundById(id, flags, variant, iid, out);
}

bool cGZSndMgr::AddSndToAutoPlayList(cIGZSnd* snd) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    autoPlay_.push_back(cRZAutoRefCount<cIGZSnd>(snd));
    return true;
}

// @ 0x1004a9530: plays sound `id` once at `volume`, keeping it alive in the autoplay list until
// it finishes (see Update).
bool cGZSndMgr::SndAutoPlayWithVolume(std::uint32_t id, std::int32_t volume) {
    cRZAutoRefCount<cIGZSnd> snd;
    {
        std::lock_guard<std::recursive_mutex> guard(lock_);
        const auto it = sounds_.find(id);
        if (it != sounds_.end()) snd = it->second;
    }
    if (!snd) {
        void* raw = nullptr;
        if (!GetSoundById(id, 0, nullptr, kGZIID_cIGZSnd, &raw) || !raw) return false;
        snd = static_cast<cIGZSnd*>(raw);
        static_cast<cIGZSnd*>(raw)->Release();   // the call's reference
    }
    AddSndToAutoPlayList(snd.get());
    return snd->PlayWithVolumeAndLoop(volume, false, 1);
}

// @ 0x1004a9ab0: the system sound index maps to a sound id (0 when unmapped).
bool cGZSndMgr::SystemSndAutoPlayWithVolume(std::uint32_t index, std::int32_t volume) {
    std::uint32_t id = 0;
    {
        std::lock_guard<std::recursive_mutex> guard(lock_);
        const auto it = systemSounds_.find(static_cast<std::int32_t>(index));
        if (it != systemSounds_.end()) id = it->second;
    }
    return SndAutoPlayWithVolume(id, volume);
}

// cGZSndMgr::Update(int) @ 0x1004a9f00: only the oldest autoplay sound is examined per tick.
void cGZSndMgr::Update(std::int32_t) {
    cRZAutoRefCount<cIGZSnd> finished;
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (!autoPlay_.empty()) {
        cIGZSnd* front = autoPlay_.front().get();
        if (!front || !front->IsPlaying()) {
            finished = autoPlay_.front();
            autoPlay_.pop_front();
        }
    }
}

// @ 0x1004aa300: the creator must implement cIGZSndDataSourceCreator; an id keeps only its
// newest creator.
bool cGZSndMgr::RegisterSndDataSourceCreator(cIGZSndDataSourceCreator* creator, std::uint32_t id) {
    if (!creator) return false;
    void* raw = nullptr;
    if (!creator->QueryInterface(kGZIID_cIGZSndDataSourceCreator, &raw)) return false;
    static_cast<cIGZUnknown*>(raw)->Release();
    RemoveSndDataSourceCreatorById(id);
    std::lock_guard<std::recursive_mutex> guard(lock_);
    dataSourceCreators_[id] = cRZAutoRefCount<cIGZSndDataSourceCreator>(creator);
    return true;
}

bool cGZSndMgr::RemoveSndDataSourceCreatorByPointer(cIGZSndDataSourceCreator* creator) {
    cRZAutoRefCount<cIGZSndDataSourceCreator> keepAlive;
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto it = dataSourceCreators_.begin(); it != dataSourceCreators_.end(); ++it) {
        if (it->second.get() == creator) {
            keepAlive = it->second;
            dataSourceCreators_.erase(it);
            return true;
        }
    }
    return false;
}

bool cGZSndMgr::RemoveSndDataSourceCreatorById(std::uint32_t id) {
    cRZAutoRefCount<cIGZSndDataSourceCreator> keepAlive;
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const auto it = dataSourceCreators_.find(id);
    if (it == dataSourceCreators_.end()) return false;
    keepAlive = it->second;
    dataSourceCreators_.erase(it);
    return true;
}

// @ 0x1004abc50: the creator registered under `id`, as interface `iid` (AddRef'd).
bool cGZSndMgr::GetSndDataSourceCreator(std::uint32_t id, std::uint32_t iid, void** out) {
    cRZAutoRefCount<cIGZSndDataSourceCreator> creator;
    {
        std::lock_guard<std::recursive_mutex> guard(lock_);
        const auto it = dataSourceCreators_.find(id);
        if (it == dataSourceCreators_.end()) return false;
        creator = it->second;
    }
    return creator ? creator->QueryInterface(iid, out) : false;
}

// @ 0x1004ac060: a registered data source answers directly; otherwise every creator is asked.
bool cGZSndMgr::GetSndDataSource(std::uint32_t id, std::uint32_t flags, cIGZVariant* variant, std::uint32_t iid, void** out) {
    cRZAutoRefCount<cIGZSndDataSource> registered;
    {
        std::lock_guard<std::recursive_mutex> guard(lock_);
        const auto it = dataSources_.find(id);
        if (it != dataSources_.end() && it->second) registered = it->second;
    }
    if (registered) return registered->QueryInterface(iid, out);

    std::vector<cRZAutoRefCount<cIGZSndDataSourceCreator>> creators;
    {
        std::lock_guard<std::recursive_mutex> guard(lock_);
        for (auto& entry : dataSourceCreators_) creators.push_back(entry.second);
    }
    for (auto& creator : creators) {
        if (creator->CreateSndDataSource(id, flags, variant, iid, out)) return true;
    }
    return false;
}

void cGZSndMgr::RegisterSndDataSource(cIGZSndDataSource* source, std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    dataSources_.emplace(id, cRZAutoRefCount<cIGZSndDataSource>(source));
}

void cGZSndMgr::UnregisterSndDataSourceById(std::uint32_t id) {
    cRZAutoRefCount<cIGZSndDataSource> keepAlive;
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const auto it = dataSources_.find(id);
    if (it != dataSources_.end()) {
        keepAlive = it->second;
        dataSources_.erase(it);
    }
}

void cGZSndMgr::UnregisterSndDataSourceByPointer(cIGZSndDataSource* source) {
    cRZAutoRefCount<cIGZSndDataSource> keepAlive;
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto it = dataSources_.begin(); it != dataSources_.end(); ++it) {
        if (it->second.get() == source) {
            keepAlive = it->second;
            dataSources_.erase(it);
            return;
        }
    }
}

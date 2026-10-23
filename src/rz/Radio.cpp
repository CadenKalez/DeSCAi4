// cRZRadio — see Radio.h for the overview, the layout and the quirks kept. Function order follows the binary.
#include "rz/Radio.h"

#include <cstring>
#include <string>

#include "audio/interfaces/cIGZSndDataSourceHolder.h"
#include "audio/interfaces/cIGZSndMgr.h"
#include "audio/interfaces/cIGZSndSys.h"
#include "gz/GZCOMDirector.h"
#include "gz/cIGZFileSystem.h"
#include "rz/DiskRadioStation.h"
#include "rz/LanguageManager.h"
#include "rz/cRZDirectory.h"
#include "rz/cRZFile.h"
#include "rz/cRZSysServPtr.h"

// cRZRadio is 0xf0 bytes in the binary (CreatecRZRadio allocates exactly that).
static_assert(sizeof(cRZRadio) == 0xf0, "cRZRadio layout");
static_assert(sizeof(cIGZRadio::RadioSettings) == 0x20, "RadioSettings layout");
static_assert(sizeof(cIGZRadio::FadeOptions) == 0x08, "FadeOptions layout");

namespace {

// cIGZSndDataSourceHolder's iid (GZSnd.cpp's kIidDataSourceHolder): PlaySong_Internal asks the radio sound for it.
constexpr std::uint32_t kGZIID_cIGZSndDataSourceHolder = 0x09d002d2;
// Stream types PlaySong_Internal passes: 2 to drop the data source, 1 to set the new file.
constexpr std::int32_t kRZRadioDataSourceStreaming = 2;
constexpr std::int32_t kRZRadioSoundFileStreaming = 1;

// The platform/iTunes branch of OnIdle. The default answers as if iTunes were not in use, which is what the
// original does on this build when UsingITunes() is false.
class DefaultRadioHost final : public IRadioHost {
public:
    bool UsingITunes() override { return false; }
    bool ITunesIsPlaying() override { return false; }
    void ITunesStop() override {}
    void ITunesPlayPlaylist() override {}
    bool ReadyToHearMusic() override { return false; }
};

IRadioStationHost* g_stationHost = nullptr;
IRadioHost* g_host = nullptr;
DefaultRadioHost g_defaultHost;

// The four-character key the two call-sign lookups build: std::string::assign(callSign, 4) in the original, which
// also reads past the terminator of a shorter string (kept).
cRZString MakeCallSignKey(const char* callSign) {
    cRZString key;
    key.str().assign(callSign, kRZRadioCallSignLength);
    return key;
}

// A station's call sign, read into a buffer pre-filled with '?' (the original passed a cRZString holding "????").
void ReadStationCallSign(cIGZRadioStation* station, char (&buffer)[kRZRadioCallSignBufferSize]) {
    std::memset(buffer, '?', sizeof buffer);
    station->GetStationCallSign(buffer);
}

bool CallSignMatches(const cRZString& key, const char* callSign) {
    return key.str().compare(0, kRZRadioCallSignLength, callSign, kRZRadioCallSignLength) == 0;
}

}  // namespace

// weak fallback: src/rz/DiskRadioStation.cpp provides the real host when the class is linked in (the same pattern as
// the GZCOM creator fallbacks). Without it the station seam stays unhooked and SetupDiskStations answers false.
__attribute__((weak)) IRadioStationHost* DiskRadioStationHost() { return nullptr; }

IRadioStationHost* RadioStationHost() { return g_stationHost ? g_stationHost : DiskRadioStationHost(); }
void SetRadioStationHost(IRadioStationHost* host) { g_stationHost = host; }
IRadioHost* RadioHost() { return g_host ? g_host : &g_defaultHost; }
void SetRadioHost(IRadioHost* host) { g_host = host; }

// @ 0x1004c6136 (the second constructor symbol @ 0x1004c6244 only calls this one)
cRZRadio::cRZRadio() : cRZSystemService(kGZSID_cRZRadio, kRZRadioServicePriority) {
    // Layout asserts against the binary's constructor; the offsets are the on-disk/pointer layout of the original.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
    static_assert(offsetof(cRZRadio, initialized_) == 0x38, "initialized_ at +0x38");
    static_assert(offsetof(cRZRadio, refCount_) == 0x3c, "refCount_ at +0x3c");
    static_assert(offsetof(cRZRadio, settingsFlags_) == 0x40, "settingsFlags_ at +0x40");
    static_assert(offsetof(cRZRadio, stations_) == 0x48, "stations_ at +0x48");
    static_assert(offsetof(cRZRadio, volume_) == 0x60, "volume_ at +0x60");
    static_assert(offsetof(cRZRadio, muteCount_) == 0x64, "muteCount_ at +0x64");
    static_assert(offsetof(cRZRadio, on_) == 0x68, "on_ at +0x68");
    static_assert(offsetof(cRZRadio, currentStation_) == 0x6c, "currentStation_ at +0x6c");
    static_assert(offsetof(cRZRadio, option0_) == 0x70, "option0_ at +0x70");
    static_assert(offsetof(cRZRadio, option1_) == 0x71, "option1_ at +0x71");
    static_assert(offsetof(cRZRadio, option3_) == 0x72, "option3_ at +0x72");
    static_assert(offsetof(cRZRadio, radioDirectory_) == 0x78, "radioDirectory_ at +0x78");
    static_assert(offsetof(cRZRadio, languageManager_) == 0xa0, "languageManager_ at +0xa0");
    static_assert(offsetof(cRZRadio, soundSystem_) == 0xa8, "soundSystem_ at +0xa8");
    static_assert(offsetof(cRZRadio, radioSound_) == 0xb0, "radioSound_ at +0xb0");
    static_assert(offsetof(cRZRadio, onDemand_) == 0xb8, "onDemand_ at +0xb8");
    static_assert(offsetof(cRZRadio, fading_) == 0xb9, "fading_ at +0xb9");
    static_assert(offsetof(cRZRadio, currentSong_) == 0xc0, "currentSong_ at +0xc0");
    static_assert(offsetof(cRZRadio, isPlaying_) == 0xe8, "isPlaying_ at +0xe8");
    static_assert(offsetof(cRZRadio, isFading_) == 0xe9, "isFading_ at +0xe9");
    static_assert(offsetof(cRZRadio, savedVolume_) == 0xec, "savedVolume_ at +0xec");
#pragma clang diagnostic pop
}

// @ 0x1004c6856: only cIGZRadio (0xe0510260), cIGZSystemService (0x287fb697) and cIGZUnknown (1) are answered, and
// iid 1 is answered with the cIGZRadio view rather than the primary one (quirk kept).
bool cRZRadio::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZRadio || iid == kGZIID_cIGZUnknown) {
        *out = static_cast<cIGZRadio*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cIGZSystemService) {
        *out = static_cast<cIGZSystemService*>(this);
        AddRef();
        return true;
    }
    return false;
}

// @ 0x1004c68b4: cRZRadio's own counter at +0x3c, not cRZUnknown's at +0x10.
std::uint32_t cRZRadio::AddRef() { return ++refCount_; }

// @ 0x1004c68ec: at most one reference is left, FinalRelease destroys the object without decrementing first.
std::uint32_t cRZRadio::Release() {
    if (refCount_ > 1) return --refCount_;
    FinalRelease();
    return 0;
}

// @ 0x1004c6978: Shutdown (vslot 0x48) then the deleting destructor (vslot 0x78).
void cRZRadio::FinalRelease() {
    Shutdown();
    delete this;
}

// @ 0x1004c6336
bool cRZRadio::Init() {
    if (!RZGetFramework()) return false;

    cRZSysServPtr<cIGZSndSys, 0x23cecba0, 0x106077d2> soundSystem;
    if (!soundSystem) return false;
    SetSoundSystem(soundSystem);

    cRZSysServPtr<cIGZLanguageManager, 0x441e5070, 0x54b7d5> languageManager;
    SetLanguageManager(languageManager);

    if (!initialized_) {
        if (!radioSound_ && soundSystem_) {
            cRZSysServPtr<cIGZSndMgr, 0x1af85bfb, 0x3af85bb7> soundManager;
            if (!soundManager) return false;
            std::uint32_t soundClass = 0;
            if (!soundManager->GetDefaultSndDataSourcePlayer2DClassId(soundClass)) return false;
            cIGZCOM* com = GZCOM();
            if (!com) return false;   // the original dereferences GZCOM() without a check
            if (!com->CreateInstance(soundClass, kGZIID_cIGZSnd, reinterpret_cast<void**>(&radioSound_))) return false;
            radioSound_->AddRef();
        }

        cRZSysServPtr<cIGZFileSystem, 0x2073215d, 0xe0203660> fileSystem;
        if (fileSystem) {
            // The radio lives below the data directory, not the application directory (vslot 0x38 = DataDirectory).
            const std::string directory = std::string(fileSystem->DataDirectory()) + kRZRadioDirectoryName;
            const cRZString radioDirectory(directory.c_str());
            SetRadioDirectory(radioDirectory);
            SetupDiskStations();
            initialized_ = true;
            if (on_) {
                on_ = false;
                TurnOn(true);
            }
        }

        if (soundSystem_) soundSystem_->RegisterEventHandler(static_cast<cIGZSndEventHandler*>(this));
    }

    cIGZFrameWork* framework = RZGetFramework();
    framework->AddToTick(this);
    framework->AddToOnIdle(this);
    return true;
}

// @ 0x1004c67a8: the released radio sound pointer is not cleared (quirk kept).
bool cRZRadio::Shutdown() {
    if (initialized_) {
        RemoveAllStations(true);
        if (radioSound_) {
            if (radioSound_->IsPlaying()) radioSound_->Stop();
            radioSound_->Release();
        }
        cIGZFrameWork* framework = RZGetFramework();
        framework->RemoveFromTick(this);
        framework->RemoveFromOnIdle(this);
        if (soundSystem_) soundSystem_->UnregisterEventHandler(static_cast<cIGZSndEventHandler*>(this));
        initialized_ = false;
    }
    return true;
}

// @ 0x1004c69a2
void cRZRadio::ReadSettings(RadioSettings* settings) {
    option0_ = settings->option0 != 0;
    option1_ = settings->option1 != 0;
    option3_ = settings->option3 != 0;
    settingsFlags_ = static_cast<std::uint32_t>(settings->settingsFlags);
    SetVolume(settings->volume);
    SetStation(static_cast<std::uint32_t>(settings->station));
    TurnOn(settings->on != 0);
}

// @ 0x1004c6a20: leaves settings->unused14 untouched.
void cRZRadio::WriteSettings(RadioSettings* settings) {
    settings->volume = volume_;
    settings->on = on_ ? 1 : 0;
    settings->station = static_cast<std::int32_t>(currentStation_);
    settings->option0 = option0_ ? 1 : 0;
    settings->option1 = option1_ ? 1 : 0;
    settings->option3 = option3_ ? 1 : 0;
    settings->settingsFlags = static_cast<std::int32_t>(settingsFlags_);
}

// @ 0x1004c6a88
bool cRZRadio::TurnOn(bool on) {
    if (on) {
        if (!on_) {
            on_ = true;
            if (stations_.empty()) SetupDiskStations();
            if (!stations_.empty() && radioSound_) {
                cIGZRadioStation* station = stations_[currentStation_];
                station->SetStation();
                station->OnIdle();
                radioSound_->SetVolume(volume_);
            }
        }
    } else if (on_) {
        on_ = false;
        if (radioSound_) StopPlayback();
    }
    return true;
}

// @ 0x1004c6b46
bool cRZRadio::IsOn() { return on_; }

// @ 0x1004c6b5a: nested mute counter; the volume checks happen before the subsystem checks.
void cRZRadio::Mute(bool mute) {
    const std::int32_t previous = muteCount_;
    if (mute) {
        ++muteCount_;
        if (previous != 0) return;
        if (volume_ == 0) return;
        if (!radioSound_) return;
        if (!soundSystem_) return;
        soundSystem_->DoEventIds(kRZRadioEventSetVolume, 0, 0, 0, 0);
    } else {
        if (previous == 0) return;
        --muteCount_;
        if (muteCount_ != 0) return;
        if (savedVolume_ == 0) return;
        if (!radioSound_) return;
        if (!soundSystem_) return;
        soundSystem_->DoEventIds(kRZRadioEventSetVolume, static_cast<std::uint32_t>(savedVolume_), 0, 0, 0);
    }
}

// @ 0x1004c6bf4: only options 0, 1 and 3 exist.
void cRZRadio::SetOption(bool value, RadioOption option) {
    switch (option) {
        case kRadioOption0: option0_ = value; break;
        case kRadioOption1: option1_ = value; break;
        case kRadioOption3: option3_ = value; break;
        default: break;
    }
}

// @ 0x1004c6c3c
bool cRZRadio::GetOption(RadioOption option) {
    switch (option) {
        case kRadioOption0: return option0_;
        case kRadioOption1: return option1_;
        case kRadioOption3: return option3_;
        default: return false;
    }
}

// @ 0x1004c6c94
std::int32_t cRZRadio::GetVolume() { return volume_; }

// @ 0x1004c6ca8: the clamp compares unsigned, so a negative volume becomes 0x400.
bool cRZRadio::SetVolume(std::int32_t volume) {
    std::int32_t clamped = kRZRadioMaxVolume;
    if (static_cast<std::uint32_t>(volume) < static_cast<std::uint32_t>(kRZRadioMaxVolume)) clamped = volume;
    volume_ = clamped;
    savedVolume_ = clamped;
    if (radioSound_ && muteCount_ == 0 && soundSystem_)
        soundSystem_->DoEventIds(kRZRadioEventSetVolume, static_cast<std::uint32_t>(clamped), 0, 0, 0);
    return true;
}

// @ 0x1004c6d04
bool cRZRadio::GetCurrentStation(std::uint32_t& index) {
    if (stations_.empty()) return false;
    index = currentStation_;
    return true;
}

// @ 0x1004c6d3c
bool cRZRadio::GetCurrentStation(char* callSign) {
    if (stations_.empty()) return false;
    stations_[currentStation_]->GetStationCallSign(callSign);
    return true;
}

// @ 0x1004c6d84
cIGZRadioStation* cRZRadio::GetStation(std::uint32_t index) {
    return index < stations_.size() ? stations_[index] : nullptr;
}

// @ 0x1004c6dcc: the running station is unset before the new index is validated (empty-list dereference kept).
bool cRZRadio::SetStation(std::uint32_t index) {
    if (currentStation_ != index) {
        stations_[currentStation_]->UnsetStation();
        if (!fading_) StopPlayback();
        if (!stations_.empty()) {
            const std::uint32_t count = static_cast<std::uint32_t>(stations_.size());
            const std::uint32_t target = index < count ? index : count - 1;
            currentStation_ = target;
            if (on_) stations_[target]->SetStation();
        }
    }
    return true;
}

// @ 0x1004c6e50: four-character call-sign lookup (the original compares two cRZStrings).
cIGZRadioStation* cRZRadio::GetStation(const char* callSign) {
    const cRZString key = MakeCallSignKey(callSign);
    char buffer[kRZRadioCallSignBufferSize];
    for (cIGZRadioStation* station : stations_) {
        ReadStationCallSign(station, buffer);
        if (CallSignMatches(key, buffer)) return station;
    }
    return nullptr;
}

// @ 0x1004c7012: posting the fade event comes before the station list is (re)built.
bool cRZRadio::SetStation(const char* callSign, const FadeOptions* fade) {
    if (fade) {
        fading_ = true;
        if (soundSystem_) {
            soundSystem_->DoEventIds(kRZRadioEventFade, static_cast<std::uint32_t>(fade->targetVolume),
                                     static_cast<std::uint32_t>(fade->durationMs), 0, 0);
        }
    }
    if (stations_.empty()) SetupDiskStations();
    const cRZString key = MakeCallSignKey(callSign);
    char buffer[kRZRadioCallSignBufferSize];
    for (std::uint32_t index = 0; index < stations_.size(); ++index) {
        ReadStationCallSign(stations_[index], buffer);
        if (CallSignMatches(key, buffer)) return SetStation(index);
    }
    return false;
}

// @ 0x1004c722c: duplicate pointers are refused; the first station starts playback when the radio is already on.
bool cRZRadio::AddStation(cIGZRadioStation* station) {
    for (cIGZRadioStation* existing : stations_) {
        if (existing == station) return false;
    }
    station->SetRadio(static_cast<cIGZRadio*>(this));
    station->AddRef();
    stations_.push_back(station);
    if (stations_.size() == 1 && IsOn()) {
        SetStation(0);
        OnIdle();
    }
    return true;
}

// @ 0x1004c72fc
bool cRZRadio::RemoveStation(cIGZRadioStation* station, bool release) {
    for (auto it = stations_.begin(); it != stations_.end(); ++it) {
        if (*it != station) continue;
        if (release) station->Release();
        stations_.erase(it);
        return true;
    }
    return false;
}

// @ 0x1004c7382
bool cRZRadio::RemoveStation(const char* callSign, bool release) {
    cIGZRadioStation* station = GetStation(callSign);
    if (!station) return false;
    return RemoveStation(station, release);
}

// @ 0x1004c7402: always answers false, like the binary.
bool cRZRadio::RemoveAllStations(bool release) {
    if (release) {
        for (cIGZRadioStation* station : stations_) {
            if (station) station->Release();
        }
    }
    stations_.clear();
    return false;
}

// @ 0x1004c74ae
bool cRZRadio::IsStationAvailable(const char* callSign) { return GetStation(callSign) != nullptr; }

// @ 0x1004c74de
std::uint32_t cRZRadio::GetStationCount() { return static_cast<std::uint32_t>(stations_.size()); }

// @ 0x1004c7502: the iTunes branch, then the sound-state refresh event and the station's own idle.
bool cRZRadio::OnIdle() {
    IRadioHost* host = RadioHost();
    if (host->UsingITunes()) {
        if (host->ReadyToHearMusic() && !host->ITunesIsPlaying()) host->ITunesPlayPlaylist();
        return false;
    }
    if (host->ITunesIsPlaying()) host->ITunesStop();
    if (!on_ || stations_.empty()) return false;
    if (soundSystem_) soundSystem_->DoEventIds(kRZRadioEventRefreshState, 0, 0, 0, 0);
    if (onDemand_) return false;
    if (IsFading()) return false;
    if (fading_) {
        fading_ = false;
        StopPlayback();
    }
    return stations_[currentStation_]->OnIdle();
}

// @ 0x1004c75e6: the language manager's directory name plus the separator (no null check in the original).
bool cRZRadio::GetLanguageDirectoryName(std::uint32_t language, cRZString& out) {
    if (!languageManager_->GetLanguageDirectoryName(out, language)) return false;
    out.str().push_back('/');
    return true;
}

// @ 0x1004c7626
bool cRZRadio::SetupDiskStations() {
    cRZDirectory directory(cRZString(""), false, false, false);

    const std::string& radioDirectory = radioDirectory_.str();
    cRZString stationsDirectory;
    stationsDirectory.str() = radioDirectory + kRZRadioStationsDirectoryName;
    // Built and never used in the original (the commercial directory below is rebuilt from the radio directory).
    cRZString unusedCommercialDirectory;
    unusedCommercialDirectory.str() = radioDirectory + kRZRadioCommercialDirectoryName;

    directory.ChangeDirectory(stationsDirectory, false);
    std::list<cRZString> entries;
    directory.ReadDirectoryDirectoryEntriesIntoStringList(entries, 0);

    std::int32_t stationCount = 0;
    if (!entries.empty()) {
        RemoveAllStations(true);

        IRadioStationHost* host = RadioStationHost();
        if (!host) return false;   // seam not hooked up: cRZDiskRadioStation is unrecovered

        // One scratch string per role, reused by every entry and every directory block (as in the original).
        cRZString languageDirectoryName;
        cRZString lookupDirectory;

        // The language-directory block of the three station directories: ask for the current language (0), look for
        // sound files inside it; for a non-English language without any (always, see kRZRadioSoundFileFilter),
        // fall back to English (option 3) or drop the language directory altogether.
        const auto applyLanguageDirectory = [this, &languageDirectoryName, &lookupDirectory](cRZString& target) {
            GetLanguageDirectoryName(0, languageDirectoryName);
            lookupDirectory.str() = target.str() + languageDirectoryName.str();
            if (languageManager_->GetCurrentLanguage() != 1 &&
                !DoesAtLeastOneSoundEntryExistForTheGivenDirectory(lookupDirectory)) {
                if (GetOption(kRadioOption3)) GetLanguageDirectoryName(1, languageDirectoryName);
                else languageDirectoryName.str().erase(0, std::string::npos);
            }
            target.str().append(languageDirectoryName.str());
        };

        for (cRZString& entry : entries) {
            // A station directory is one that holds a "*.id" call-sign file.
            lookupDirectory.str() = stationsDirectory.str();
            lookupDirectory.str().append(entry.str().data(), entry.str().size());
            lookupDirectory.str().push_back('/');
            lookupDirectory.str().append(kRZRadioCallIdFilter);
            if (!cRZDirectory::DoesAnyEntryExistThatMatchesPattern(lookupDirectory)) continue;

            cIGZRadioStation* station = host->CreateDiskStation();
            if (!station) return false;
            station->AddRef();
            AddStation(station);
            const std::uint32_t shuffle = settingsFlags_ & 1;
            station->SetShuffleStrategy(shuffle == 0 ? 0 : 1);

            entry.str().push_back('/');   // the original appends the separator to the listed entry itself
            cRZString& stationDirectory = host->StationDirectory(station);
            stationDirectory.str() = stationsDirectory.str() + entry.str();
            host->MusicDirectory(station).str() = stationDirectory.str() + kRZRadioMusicDirectoryName;

            host->DjDirectory(station).str() = stationDirectory.str() + kRZRadioDjDirectoryName;
            applyLanguageDirectory(host->DjDirectory(station));

            host->CommercialDirectory(station).str() = radioDirectory + kRZRadioCommercialDirectoryName;
            applyLanguageDirectory(host->CommercialDirectory(station));

            host->JingleDirectory(station).str() = stationDirectory.str() + kRZRadioJingleDirectoryName;
            applyLanguageDirectory(host->JingleDirectory(station));

            station->ReadCallSignFromDisk();
            ++stationCount;
        }

        if (stations_.size() <= currentStation_) currentStation_ = 0;
    }
    return stationCount != 0;
}

// @ 0x1004c8094: the comma-separated filter is matched as one pattern (RZFileFindFirst does not split it), so this
// always answers false; kept.
bool cRZRadio::DoesAtLeastOneSoundEntryExistForTheGivenDirectory(const cRZString& directory) {
    const std::string pattern = directory.str() + kRZRadioSoundFileFilter;
    return cRZDirectory::DoesAnyEntryExistThatMatchesPattern(cRZString(pattern.c_str()));
}

// @ 0x1004c8136
bool cRZRadio::PlaySong(const cRZString& song) {
    const bool exists = cRZFile::FileExists(song);
    if (exists) SendPlayMessage(song);
    return exists;
}

// @ 0x1004c8166: the copy passes to the sound system, which owns it (the original builds it with new).
void cRZRadio::SendPlayMessage(const cRZString& song) {
    if (!soundSystem_) return;
    auto* message = new cRZString(song);
    soundSystem_->DoEventString(kRZRadioEventPlaySong, message, 0, 0, 0, 0);
}

// @ 0x1004c8238
void cRZRadio::StopPlayback() {
    if (soundSystem_) soundSystem_->DoEventIds(kRZRadioEventStopPlayback, 0, 0, 0, 0);
}

// @ 0x1004c8290
void cRZRadio::PlayNextSong(bool arg) {
    if (soundSystem_) soundSystem_->DoEventIds(kRZRadioEventPlayNext, arg ? 1u : 0u, 0, 0, 0);
}

// @ 0x1004c82ee: always true; the song is only copied while the last refresh saw playback.
bool cRZRadio::GetCurrentSong(cRZString& out) {
    if (isPlaying_) out.str() = currentSong_.str();
    return true;
}

// @ 0x1004c8346
void cRZRadio::SetCurrentSong(const cRZString& song) { currentSong_.str() = song.str(); }

// @ 0x1004c8372: the data-source holder reference is leaked (only released on the unwind path in the original).
void cRZRadio::PlaySong_Internal(const cRZString& song) {
    // The holder query's answer is assumed to be a cRZString (SendPlayMessage put one in the event); the reference
    // it takes is never dropped here, only in the original's unwind path.
    cIGZSndDataSourceHolder* holder = nullptr;
    if (radioSound_->QueryInterface(kGZIID_cIGZSndDataSourceHolder, reinterpret_cast<void**>(&holder))) {
        holder->SetDataSource(nullptr, kRZRadioDataSourceStreaming);
    }
    radioSound_->SetSoundFileFromString(song, kRZRadioSoundFileStreaming);
    radioSound_->PlayWithVolumeAndLoop(volume_, false, 1);
    SetCurrentSong(song);
}

// @ 0x1004c842c
bool cRZRadio::DoEvent(cIGZSndEventStd* event) {
    cIGZMessage2Standard* message = event->AsMessage2Standard();
    const std::int32_t id = static_cast<std::int32_t>(event->GetEventId());
    bool handled = false;

    if (id < static_cast<std::int32_t>(kRZRadioEventSetVolume)) {   // signed, as in the binary
        if (id == static_cast<std::int32_t>(kRZRadioEventPlaySong)) {
            handled = true;
            if (message->GetHasString()) {
                radioSound_->Stop();
                cIGZString* text = nullptr;   // the original does not check GetString's answer here
                message->GetString(kGZIID_cIGZString, reinterpret_cast<void**>(&text));
                PlaySong_Internal(*static_cast<cRZString*>(text));
                if (text) text->Release();
            }
        } else if (id == static_cast<std::int32_t>(kRZRadioEventRefreshState)) {
            handled = true;
            if (radioSound_) {
                isPlaying_ = radioSound_->IsPlaying();
                isFading_ = radioSound_->IsFading();
            }
        } else if (id == static_cast<std::int32_t>(kRZRadioEventStopPlayback)) {
            radioSound_->Stop();   // no null check in the original
            handled = true;
        }
    } else if (id == static_cast<std::int32_t>(kRZRadioEventSetVolume)) {
        volume_ = static_cast<std::int32_t>(message->GetData1());
        handled = true;
        if (radioSound_ && !fading_) radioSound_->SetVolume(volume_);
    } else if (id == static_cast<std::int32_t>(kRZRadioEventFade)) {
        handled = true;
        if (radioSound_ && radioSound_->IsPlaying()) {
            const std::int32_t from = radioSound_->GetVolume();
            const std::int32_t to = static_cast<std::int32_t>(message->GetData1());
            const std::int32_t duration = static_cast<std::int32_t>(message->GetData2());
            radioSound_->FadeVolume(from, to, static_cast<std::uint32_t>(duration), true);
            fading_ = true;
        }
    } else if (id == static_cast<std::int32_t>(kRZRadioEventPlayNext)) {
        if (!radioSound_ || (message->GetData1() == 0 && radioSound_->IsPlaying())) {
            handled = false;
        } else {
            radioSound_->Stop();
            handled = true;
            if (!stations_.empty()) {
                cIGZRadioStation* station = stations_[currentStation_];
                if (IRadioStationHost* host = RadioStationHost()) host->ClearStartedFlag(station);
                station->StartPlayingNextSelectionOfGivenType(0);
            }
        }
    }
    return handled;
}

// @ 0x1004c86ea: the tick only asks the (no-argument) idle to run.
bool cRZRadio::OnTick(std::uint32_t) {
    OnIdle();
    return true;
}

// @ 0x1004c8704
void cRZRadio::SetLanguageManager(cIGZLanguageManager* manager) { languageManager_ = manager; }

// @ 0x1004c8712
void cRZRadio::SetSoundSystem(cIGZSndSys* soundSystem) { soundSystem_ = soundSystem; }

// @ 0x1004c8720
cIGZSnd* cRZRadio::GetRadioSound() { return radioSound_; }

// @ 0x1004c872e
bool cRZRadio::IsPlaying() { return isPlaying_; }

// @ 0x1004c873a
bool cRZRadio::IsFading() { return isFading_; }

// @ 0x1004c8746
bool cRZRadio::GetRadioDirectory(cIGZString& out) {
    out.AssignFromChars(radioDirectory_.str().data(), radioDirectory_.Strlen());
    return true;
}

// @ 0x1004c8786
void cRZRadio::SetRadioDirectory(const cIGZString& directory) { radioDirectory_.AssignFromIGZString(directory); }

// @ 0x1004c8794
void cRZRadio::SetOnDemandState(bool onDemand) { onDemand_ = onDemand; }

// @ 0x1003deecd
cIGZRadio* CreatecRZRadio() { return new cRZRadio(); }

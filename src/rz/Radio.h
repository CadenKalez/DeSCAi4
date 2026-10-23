// cRZRadio — the radio service (cIGZRadio). It owns the station list (cIGZRadioStation objects, built from the
// "ReS/Music/Radio/" data directory), the on/off state, volume and the mute counter, the three options, the persisted
// settings blob, the current station/song and the one cIGZSnd instance it plays them through. It is a framework
// system service (id 0x1d07b39f, priority 0x7a116) and a cIGZSndEventHandler that answers the asynchronous sound
// events it posts to the sound system: play this file, stop, set volume, fade, play the next selection.
//
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1: 53 named member symbols 0x1004c6136..0x1004c880e (plus their
// non-virtual thunks) and the COM factory CreatecRZRadio @ 0x1003deecd.
//
// Layout (0xf0 bytes, asserted in Radio.cpp): +0 cRZSystemService (0x28 = cIGZSystemService view + cRZUnknown view at
// +8), +0x28 cIGZRadio view, +0x30 cIGZSndEventHandler view, then the members below. The original reference count is
// the class's own +0x3c field, not cRZUnknown's (+0x10): QueryInterface/AddRef/Release in all four vtable groups use
// it, so the inherited cRZUnknown::RemoveRef()/RefCount() keep reading the unused base counter (quirk kept).
//
// Quirks kept (all in the binary):
//  * Release at count <= 1 calls FinalRelease (Shutdown, then delete this) without decrementing first.
//  * Shutdown releases the radio sound but leaves the dangling pointer in m_radioSound (+0xb0); a second Init then
//    believes it already has a sound.
//  * cIGZUnknown iid 1 is answered with the cIGZRadio view (+0x28), not the primary view, and no other iid is passed
//    on to cRZSystemService::QueryInterface.
//  * SetStation(uint) calls UnsetStation() on stations_[currentStation] before checking that the list is non-empty
//    (null dereference on an empty list).
//  * SetupDiskStations computes the shared commercial directory into a local and never uses it (it rebuilds the
//    string per station), appends '/' to each listed entry in place, and matches the "*.mp3,*.wav,..." filter with a
//    single fnmatch (RZFileFindFirst does not split on commas), so DoesAtLeastOneSoundEntryExistForTheGivenDirectory
//    always answers false - the language fallback below therefore runs for every non-English language.
//  * PlaySong_Internal leaks the cIGZSndDataSourceHolder reference: the local is only released on the unwind path.
//  * Mute() checks the volume before the subsystems, so muting at volume 0 still increments the mute count.
#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "audio/interfaces/cIGZSnd.h"
#include "audio/interfaces/cIGZSndEventStd.h"
#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"
#include "rz/cRZSystemService.h"

class cIGZLanguageManager;
class cIGZSndSys;
class cRZDirectory;

constexpr std::uint32_t kGZSID_cRZRadio = 0x1d07b39f;
constexpr std::int32_t kRZRadioServicePriority = 0x7a116;
constexpr std::uint32_t kGZIID_cIGZRadio = 0xe0510260;
constexpr std::int32_t kRZRadioMaxVolume = 0x400;                 // volume scale 0..1024, like cIGZSnd
constexpr std::size_t kRZRadioCallSignLength = 4;                 // call signs are four characters
constexpr std::size_t kRZRadioCallSignBufferSize = 32;            // the original passed a cRZString's SSO buffer (22)

// The event ids cRZRadio posts to the sound system (cIGZSndSys::DoEventIds / DoEventString) and handles back in
// DoEvent. They are also the message types of the strings/values carried along.
constexpr std::uint32_t kRZRadioEventPlaySong = 0xeab186df;       // string payload: cRZString with the sound path
constexpr std::uint32_t kRZRadioEventRefreshState = 0xeab1a483;    // update m_isPlaying / m_isFading
constexpr std::uint32_t kRZRadioEventStopPlayback = 0x0ab186e5;
constexpr std::uint32_t kRZRadioEventSetVolume = 0xab1a61e;        // data1 = new volume
constexpr std::uint32_t kRZRadioEventFade = 0x4ab186e9;            // data1 = target volume, data2 = duration ms
constexpr std::uint32_t kRZRadioEventPlayNext = 0x6ab1a480;

// Strings from the binary's __DATA (SZ_* globals): the radio directory below the data directory and the names
// SetupDiskStations looks for inside it.
constexpr const char* kRZRadioDirectoryName = "ReS/Music/Radio/";
constexpr const char* kRZRadioStationsDirectoryName = "Stations/";
constexpr const char* kRZRadioCommercialDirectoryName = "Commercials/";
constexpr const char* kRZRadioMusicDirectoryName = "Music/";
constexpr const char* kRZRadioDjDirectoryName = "DJ/";
constexpr const char* kRZRadioJingleDirectoryName = "Jingle/";
constexpr const char* kRZRadioCallIdFilter = "*.id";              // a station directory must hold one
constexpr const char* kRZRadioSoundFileFilter = "*.mp3,*.wav,*.xa,*.m3u,*.pls";

// cIGZRadioStation — the station interface cRZRadio drives. Its slots are the vtable of cRZDiskRadioStation
// (0x100ab6e80, slots 0..27): the class is recovered in rz/DiskRadioStation.{h,cpp}. The original declares the two
// destructor slots in the middle of this interface (at +0x78/+0x80, between SetModifierList and LoadAllPlaylists),
// which is why the destructor is declared here and LoadAllPlaylists starts at +0x88. The enum types the binary names
// (ePlayListMode, eShuffleStrategy) are passed as int32: the recovered values are 0/1. cRZRadio also writes the
// concrete station's directory strings: see IRadioStationHost.
class cIGZRadioStation : public cIGZUnknown {
public:
    virtual bool Init() = 0;                                                              // [3]  +0x18
    virtual bool Shutdown() = 0;                                                          // [4]  +0x20
    virtual bool OnIdle() = 0;                                                            // [5]  +0x28
    virtual void SetRadio(class cIGZRadio* radio) = 0;                                    // [6]  +0x30
    virtual void SetStation() = 0;                                                        // [7]  +0x38
    virtual void UnsetStation() = 0;                                                      // [8]  +0x40
    virtual void GetStationCallSign(char* out) = 0;                                       // [9]  +0x48 (4 chars)
    virtual void GetStationDescription(cIGZString* out) = 0;                              // [10] +0x50
    virtual bool IsOnTheAir() = 0;                                                        // [11] +0x58
    virtual void SetPlaylistMode(std::int32_t mode) = 0;                                  // [12] +0x60 ePlayListMode
    virtual void SetShuffleStrategy(std::int32_t strategy) = 0;                           // [13] +0x68 eShuffleStrategy
    virtual void SetModifierList(std::list<cRZString>* modifiers) = 0;                    // [14] +0x70
    // [15] [16] the destructors. The original declares them inside the interface, so the concrete class's own
    // destructor overrides these two slots instead of appending new ones (which is why [17] is LoadAllPlaylists).
    virtual ~cIGZRadioStation() = default;
    virtual bool LoadAllPlaylists() = 0;                                                  // [17] +0x88
    virtual bool ShufflePlaylist(std::int32_t strategy) = 0;                              // [18] +0x90
    virtual bool ShufflePlayList(const cRZString& name, std::list<cRZString>& out,
                                 std::int32_t strategy) = 0;                              // [19] +0x98
    virtual void ShuffleCycle() = 0;                                                      // [20] +0xa0
    virtual bool IsCycleTypePresent(std::int32_t cycleType) = 0;                          // [21] +0xa8
    virtual bool MoveToNextPlaylistItem(std::int32_t cycleType) = 0;                      // [22] +0xb0
    virtual bool ReadCallSignFromDisk() = 0;                                              // [23] +0xb8
    virtual bool StartPlayingNextSelectionOfGivenType(std::int32_t cycleType) = 0;        // [24] +0xc0
    virtual bool PlayWelcome() = 0;                                                       // [25] +0xc8
    virtual bool ExpandPlayLists(cRZString& directory, std::list<cRZString>& out) = 0;    // [26] +0xd0
    virtual void PlayNextSelectionNotOnExclusionList() = 0;                               // [27] +0xd8
};

// cIGZRadio — the interface the game asks the framework for (service 0x1d07b39f, iid 0xe0510260, vtable group
// +0x28 of cRZRadio). Slots as in the binary; the group starts at [3] (it has no destructor slots).
class cIGZRadio : public cIGZUnknown {
public:
    // The persisted block (0x20 bytes): ReadSettings reads all but +0x14, WriteSettings leaves +0x14 untouched.
    struct RadioSettings {
        std::int32_t volume;          // +0x00
        std::int32_t on;              // +0x04
        std::int32_t station;         // +0x08
        std::int32_t option0;         // +0x0c
        std::int32_t option1;         // +0x10
        std::int32_t unused14;        // +0x14 (neither function touches it)
        std::int32_t option3;         // +0x18
        std::int32_t settingsFlags;   // +0x1c (cRZRadio +0x40; bit 0 = station shuffle)
    };
    // The fade posted with SetStation(name, fade): data1/data2 of kRZRadioEventFade.
    struct FadeOptions {
        std::int32_t targetVolume;    // +0x00
        std::int32_t durationMs;      // +0x04
    };
    // Only 0, 1 and 3 are stored and answered; every other value reads false / is ignored. The original names are
    // not recovered.
    enum RadioOption : std::int32_t {
        kRadioOption0 = 0,
        kRadioOption1 = 1,
        kRadioOption2 = 2,
        kRadioOption3 = 3,
    };

    virtual bool Init() = 0;                                                              // [3]  +0x18
    virtual bool Shutdown() = 0;                                                          // [4]  +0x20
    virtual bool OnIdle() = 0;                                                            // [5]  +0x28
    virtual cIGZSnd* GetRadioSound() = 0;                                                 // [6]  +0x30
    virtual bool PlaySong(const cRZString& song) = 0;                                     // [7]  +0x38
    virtual void PlayNextSong(bool arg) = 0;                                              // [8]  +0x40
    virtual bool GetCurrentStation(std::uint32_t& index) = 0;                             // [9]  +0x48
    virtual bool GetCurrentStation(char* callSign) = 0;                                   // [10] +0x50
    virtual cIGZRadioStation* GetStation(std::uint32_t index) = 0;                        // [11] +0x58
    virtual bool SetStation(std::uint32_t index) = 0;                                     // [12] +0x60
    virtual cIGZRadioStation* GetStation(const char* callSign) = 0;                       // [13] +0x68
    virtual bool SetStation(const char* callSign, const FadeOptions* fade) = 0;           // [14] +0x70
    virtual std::int32_t GetVolume() = 0;                                                 // [15] +0x78
    virtual bool SetVolume(std::int32_t volume) = 0;                                      // [16] +0x80
    virtual std::uint32_t GetStationCount() = 0;                                          // [17] +0x88
    virtual bool IsStationAvailable(const char* callSign) = 0;                            // [18] +0x90
    virtual bool AddStation(cIGZRadioStation* station) = 0;                               // [19] +0x98
    virtual bool RemoveStation(cIGZRadioStation* station, bool release) = 0;              // [20] +0xa0
    virtual bool RemoveStation(const char* callSign, bool release) = 0;                   // [21] +0xa8
    virtual bool RemoveAllStations(bool release) = 0;                                     // [22] +0xb0
    virtual bool TurnOn(bool on) = 0;                                                     // [23] +0xb8
    virtual bool IsOn() = 0;                                                              // [24] +0xc0
    virtual void Mute(bool mute) = 0;                                                     // [25] +0xc8
    virtual bool GetOption(RadioOption option) = 0;                                       // [26] +0xd0
    virtual void SetOption(bool value, RadioOption option) = 0;                           // [27] +0xd8
    virtual bool GetRadioDirectory(cIGZString& out) = 0;                                  // [28] +0xe0
    virtual void SetRadioDirectory(const cIGZString& directory) = 0;                      // [29] +0xe8
    virtual void SetOnDemandState(bool onDemand) = 0;                                     // [30] +0xf0
    virtual void StopPlayback() = 0;                                                      // [31] +0xf8
    virtual bool GetCurrentSong(cRZString& out) = 0;                                      // [32] +0x100
    virtual void SetCurrentSong(const cRZString& song) = 0;                               // [33] +0x108
    virtual bool IsPlaying() = 0;                                                         // [34] +0x110
    virtual void ReadSettings(RadioSettings* settings) = 0;                               // [35] +0x118
    virtual void WriteSettings(RadioSettings* settings) = 0;                              // [36] +0x120
};

// What cRZRadio needs from the concrete disk station (cRZDiskRadioStation, recovered in rz/DiskRadioStation.{h,cpp}):
// it allocates one per station directory and writes its directory strings and its "cycle started" byte directly. The
// built-in implementation (DiskRadioStationHost) hands out the real class; a test can install its own.
class IRadioStationHost {
public:
    virtual ~IRadioStationHost() = default;
    // new cRZDiskRadioStation (0x218 bytes) and its constructor, before the AddRef cRZRadio does next. @ 0x1004c792e
    virtual cIGZRadioStation* CreateDiskStation() = 0;
    // The station's own cRZString members, written by SetupDiskStations in the original.
    virtual cRZString& StationDirectory(cIGZRadioStation* station) = 0;       // cRZDiskRadioStation +0xb8
    virtual cRZString& MusicDirectory(cIGZRadioStation* station) = 0;         // +0xe0
    virtual cRZString& DjDirectory(cIGZRadioStation* station) = 0;            // +0x128
    virtual cRZString& CommercialDirectory(cIGZRadioStation* station) = 0;    // +0x170
    virtual cRZString& JingleDirectory(cIGZRadioStation* station) = 0;        // +0x1b8
    // DoEvent clears the concrete station's byte at +0x30 (its "cycle started" flag) before asking it for the next
    // selection. @ 0x1004c8674
    virtual void ClearStartedFlag(cIGZRadioStation* station) = 0;
};

// The macOS/iTunes branch of OnIdle (UsingITunes / iTunesPlayer / sReadyToHearMusic, none of them recovered).
class IRadioHost {
public:
    virtual ~IRadioHost() = default;
    virtual bool UsingITunes() = 0;            // UsingITunes() @ 0x1003a89c0
    virtual bool ITunesIsPlaying() = 0;        // iTunesPlayer::IsPlaying() @ 0x1007b6cb6
    virtual void ITunesStop() = 0;             // iTunesPlayer::Stop() @ 0x1007b6c38
    virtual void ITunesPlayPlaylist() = 0;     // iTunesPlayer::PlayPlaylist(nullptr, true, true) @ 0x1007b6c51
    virtual bool ReadyToHearMusic() = 0;       // sReadyToHearMusic @ 0x100b70ec8
};

IRadioStationHost* RadioStationHost();
void SetRadioStationHost(IRadioStationHost* host);
// Always non-null: the built-in host answers "not using iTunes" (the original's calls then take the cRZSnd path).
IRadioHost* RadioHost();
void SetRadioHost(IRadioHost* host);

class cRZRadio : public cRZSystemService, public cIGZRadio, public cIGZSndEventHandler {
public:
    cRZRadio();                                      // @ 0x1004c6136 (the other ctor symbol calls this one)
    ~cRZRadio() override = default;                  // @ 0x1004c624e / 0x1004c62e2 / 0x1004c62fa

    // --- cRZSystemService / cIGZUnknown ---
    bool QueryInterface(std::uint32_t iid, void** out) override;      // @ 0x1004c6856
    std::uint32_t AddRef() override;                                  // @ 0x1004c68b4
    std::uint32_t Release() override;                                 // @ 0x1004c68ec
    bool Init() override;                                             // @ 0x1004c6336
    bool Shutdown() override;                                         // @ 0x1004c67a8
    bool OnTick(std::uint32_t elapsedMs) override;                    // @ 0x1004c86ea

    // --- the members of the primary vtable (order as in the binary, slots 16..55) ---
    using cRZSystemService::OnIdle;                                   // keep the framework's OnIdle(uint) visible
    virtual void SetLanguageManager(cIGZLanguageManager* manager);    // 0x80  @ 0x1004c8704 (not an interface slot)
    virtual void SetSoundSystem(cIGZSndSys* soundSystem);             // 0x88  @ 0x1004c8712
    bool OnIdle() override;                                           // 0x90  @ 0x1004c7502 (cIGZRadio [5], no args)
    bool DoEvent(cIGZSndEventStd* event) override;                    // 0x98  @ 0x1004c842c (cIGZSndEventHandler [3])
    cIGZSnd* GetRadioSound() override;                                // 0xa0  @ 0x1004c8720
    bool PlaySong(const cRZString& song) override;                    // 0xa8  @ 0x1004c8136
    void StopPlayback() override;                                     // 0xb0  @ 0x1004c8238
    void PlayNextSong(bool arg) override;                             // 0xb8  @ 0x1004c8290
    bool GetCurrentStation(std::uint32_t& index) override;            // 0xc0  @ 0x1004c6d04
    bool GetCurrentStation(char* callSign) override;                  // 0xc8  @ 0x1004c6d3c
    cIGZRadioStation* GetStation(std::uint32_t index) override;       // 0xd0  @ 0x1004c6d84
    bool SetStation(std::uint32_t index) override;                    // 0xd8  @ 0x1004c6dcc
    cIGZRadioStation* GetStation(const char* callSign) override;      // 0xe0  @ 0x1004c6e50
    bool SetStation(const char* callSign, const FadeOptions* fade) override;   // 0xe8 @ 0x1004c7012
    std::int32_t GetVolume() override;                                // 0xf0  @ 0x1004c6c94
    bool SetVolume(std::int32_t volume) override;                     // 0xf8  @ 0x1004c6ca8
    std::uint32_t GetStationCount() override;                         // 0x100 @ 0x1004c74de
    bool IsStationAvailable(const char* callSign) override;           // 0x108 @ 0x1004c74ae
    bool AddStation(cIGZRadioStation* station) override;              // 0x110 @ 0x1004c722c
    bool RemoveStation(cIGZRadioStation* station, bool release) override;   // 0x118 @ 0x1004c72fc
    bool RemoveStation(const char* callSign, bool release) override;  // 0x120 @ 0x1004c7382
    bool RemoveAllStations(bool release) override;                    // 0x128 @ 0x1004c7402 (always false)
    bool GetCurrentSong(cRZString& out) override;                     // 0x130 @ 0x1004c82ee
    void SetCurrentSong(const cRZString& song) override;              // 0x138 @ 0x1004c8346
    bool IsPlaying() override;                                        // 0x140 @ 0x1004c872e
    virtual bool IsFading();                                          // 0x148 @ 0x1004c873a (primary vtable only)
    bool TurnOn(bool on) override;                                    // 0x150 @ 0x1004c6a88
    bool IsOn() override;                                             // 0x158 @ 0x1004c6b46
    void Mute(bool mute) override;                                    // 0x160 @ 0x1004c6b5a
    bool GetOption(RadioOption option) override;                      // 0x168 @ 0x1004c6c3c
    void SetOption(bool value, RadioOption option) override;          // 0x170 @ 0x1004c6bf4
    bool GetRadioDirectory(cIGZString& out) override;                 // 0x178 @ 0x1004c8746
    void SetRadioDirectory(const cIGZString& directory) override;     // 0x180 @ 0x1004c8786
    void SetOnDemandState(bool onDemand) override;                    // 0x188 @ 0x1004c8794
    void ReadSettings(RadioSettings* settings) override;              // 0x190 @ 0x1004c69a2
    void WriteSettings(RadioSettings* settings) override;             // 0x198 @ 0x1004c6a20
    virtual bool SetupDiskStations();                                 // 0x1a0 @ 0x1004c7626
    virtual bool DoesAtLeastOneSoundEntryExistForTheGivenDirectory(const cRZString& directory);   // 0x1a8
    virtual bool GetLanguageDirectoryName(std::uint32_t language, cRZString& out);                 // 0x1b0
    virtual void FinalRelease();                                      // 0x1b8 @ 0x1004c6978

    // Not a vtable slot (the original keeps it private); used by DoEvent, public so the test can drive it.
    void PlaySong_Internal(const cRZString& song);                    // @ 0x1004c8372
    void SendPlayMessage(const cRZString& song);                      // @ 0x1004c8166

private:
    bool initialized_ = false;                      // +0x38
    std::uint32_t refCount_ = 0;                    // +0x3c (cRZRadio's own; see the header comment)
    std::uint32_t settingsFlags_ = 0;               // +0x40 RadioSettings+0x1c, bit 0 = station shuffle
    std::vector<cIGZRadioStation*> stations_;       // +0x48 begin, +0x50 end, +0x58 capacity end
    std::int32_t volume_ = kRZRadioMaxVolume;       // +0x60
    std::int32_t muteCount_ = 0;                    // +0x64
    bool on_ = false;                               // +0x68
    std::uint32_t currentStation_ = 0;              // +0x6c
    bool option0_ = true;                           // +0x70
    bool option1_ = false;                          // +0x71
    bool option3_ = true;                           // +0x72
    cRZString radioDirectory_;                      // +0x78 (data directory + "ReS/Music/Radio/")
    cIGZLanguageManager* languageManager_ = nullptr;   // +0xa0 borrowed (the lookup drops its reference)
    cIGZSndSys* soundSystem_ = nullptr;             // +0xa8 borrowed, same
    cIGZSnd* radioSound_ = nullptr;                 // +0xb0 owned; Shutdown releases it but does not clear it
    bool onDemand_ = false;                         // +0xb8
    bool fading_ = false;                           // +0xb9 cRZRadio's own fade flag (kRZRadioEventFade)
    cRZString currentSong_;                         // +0xc0
    bool isPlaying_ = false;                        // +0xe8 (last sound state, refreshed by the event)
    bool isFading_ = false;                         // +0xe9 (ditto)
    std::int32_t savedVolume_ = kRZRadioMaxVolume;  // +0xec volume to restore when the mute count reaches 0
};

// @ 0x1003deecd: new cRZRadio, constructor, the cIGZRadio view (+0x28) as the COM object.
cIGZRadio* CreatecRZRadio();

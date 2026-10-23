// cRZDiskRadioStation — one radio station read from disk: the station directory holds a "*.id" call-sign file, a
// "*.txt" description and four playlists under Music/, DJ/, Commercials/ (shared) and Jingle/. The class walks those
// directories per sound-file filter, expands .m3u/.pls playlists into their entries, shuffles each list (randomly or
// newest-first), and plays selections by asking the station's cIGZRadio (cRZRadio) to play the joined path.
//
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1: 32 member symbols 0x1004c3360..0x1004c86f0 (the two constructor
// and three destructor symbols share one body each) and the factory CreatecRZDiskRadioStation @ 0x1003def08. The vtable
// is 0x100ab6e80, slots 0..27; see cIGZRadioStation in rz/Radio.h for the slot order.
//
// Layout (0x218 bytes, asserted in DiskRadioStation.cpp; the factory allocates exactly that):
//   +0x00 vptr
//   +0x08 uint32 reference count (the class's own; cIGZUnknown's counter is unused — quirk kept)
//   +0x0c bool   on the air, +0x0d bool welcome already played
//   +0x10 std::vector<int32> the cycle order {0,1,2,3}, shuffled by the constructor and by ShuffleCycle
//   +0x28 int32  index into the cycle order, +0x2c int32 number of times the music cycle was repeated
//   +0x30 bool   "a selection was just started" flag (cRZRadio clears it through IRadioStationHost)
//   +0x38 cRZTimer (0x18 bytes, milliseconds)
//   +0x50 cIGZRadio* the radio (borrowed), +0x58 its cIGZSnd (borrowed, never read after SetRadio)
//   +0x60 cRZString call sign ("????" until ReadCallSignFromDisk), +0x88 description
//   +0xb0 cRZString station directory, +0xd8 music, +0x120 DJ, +0x168 commercials, +0x1b0 jingles
//   +0x100/+0x148/+0x190/+0x1d8 std::list<cRZString> music/DJ/commercial/jingle playlists, each followed at
//        +0x118/+0x160/+0x1a8/+0x1f0 by the current node (8 bytes; the original keeps a raw node pointer there)
//   +0x1f8 std::list<cRZString> modifier (exclusion) list, +0x210 int32 playlist mode, +0x214 int32 shuffle strategy
// The member cRZStrings' std::string starts 8 bytes into them (+0xb8, +0xe0, +0x128, +0x170, +0x1b8), which is what
// the IRadioStationHost accessors documented with those offsets return.
//
// Quirks kept (all in the binary):
//  * AddRef returns whatever the previous counter increment left in eax (the symbol is void) and Release at a count of
//    1 calls the deleting destructor without decrementing first.
//  * QueryInterface answers only 0x4056574b (cIGZRadioStation) and 1 (cIGZUnknown); iid 1 gets the same this pointer.
//  * SetStation chains ReadCallSignFromDisk, LoadAllPlaylists, ShufflePlaylist(strategy), and only calls PlayWelcome
//    when the welcome file has not played yet (the flag at +0xd).
//  * The station's cRZString temporaries are rebuilt per call: LoadAllPlaylists rebuilds a copy of the filter token
//    for each of the four directories, ExpandPlayLists rebuilds the "*.m3u,*.pls" filter per file, and ReadCallSignFromDisk
//    reuses one filter string for "*.id" then "*.txt".
//  * PlayNextSelectionNotOnExclusionList treats the modifier list differently per playlist mode: mode 1 skips entries
//    that ARE on the list, every other mode skips entries that are NOT on it.
//  * ExpandPlayLists erases the playlist file from the list it is expanding while the remaining filter tokens still
//    refer to it (a use-after-free in the original). The port keeps a copy of the entry for those comparisons: the
//    observable behaviour is the same because a name can only match one extension.
//  * IsCycleTypePresent asks the radio for options 0, 1 and 2 for the DJ/commercial/jingle cycles, but cRZRadio only
//    answers 0, 1 and 3, so the jingle cycle (3 -> option 2) is never present.
//  * ShufflePlaylist swaps the first and last element when the new first element equals the previous last one, so a
//    reshuffle never puts the track that just played first again.
//  * OnIdle's first branch repeats the music cycle (75% chance) without advancing the cycle order and bumps +0x2c,
//    which nothing ever reads.
//  * The timer is constructed with milliseconds, SetUnits(4) clears it again and Start() runs it; the "already
//    started" path resets and restarts it.
// Deviations forced by safety (all on paths where the original dereferences a null or dangling node / reads past a
// string): StartPlayingNextSelectionOfGivenType answers false and PlayNextSelectionNotOnExclusionList returns when the
// music playlist is empty; GetStationCallSign pads a call sign shorter than four characters with '?' instead of
// copying raw bytes past its terminator; ExpandPlayLists keeps a copy of the entry it erases for the remaining filter
// tokens (see above).
#pragma once
#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

#include "rz/Radio.h"
#include "rz/cRZString.h"
#include "rz/cRZTimer.h"

// RZFileTimeSort — the comparator cRZDiskRadioStation::ShufflePlayList sorts a playlist with (its only user in the
// binary, emitted next to this class: 0x1004c8bf8..0x1004c9c93). It holds the playlist directory as a cRZString
// (+0x00, std::string at +0x08) and compares GetTime(directory + a) against GetTime(directory + b): newest first.
class RZFileTimeSort {
public:
    explicit RZFileTimeSort(const cRZString& directory) : directory_(directory) {}
    bool operator()(const cRZString& a, const cRZString& b) const;   // @ 0x1004c9230

private:
    cRZString directory_;
};

// The iid cRZDiskRadioStation answers besides cIGZUnknown (cRZRadio never asks for it).
constexpr std::uint32_t kGZIID_cIGZRadioStation = 0x4056574b;

// The cycle types of the order vector at +0x10 (cRZDiskRadioStation::RadioStationCycleType in the binary).
enum RadioStationCycleType : std::int32_t {
    kRZRadioCycleMusic = 0,        // <station>/Music/
    kRZRadioCycleDj = 1,           // <station>/DJ/
    kRZRadioCycleCommercial = 2,   // shared Commercials/
    kRZRadioCycleJingle = 3,       // <station>/Jingle/
};
constexpr std::int32_t kRZRadioCycleTypeCount = 4;

// Strings the class reads from __DATA (SZ_* globals in the binary).
constexpr const char* kRZRadioStationDescriptionFilter = "*.txt";              // SZ_STATION_DESCRIPTION_FILTER
constexpr const char* kRZRadioPlaylistFilter = "*.m3u,*.pls";                  // SZ_SOUND_PLAYLIST_FILTER
constexpr const char* kRZRadioWelcomeFileName = "Welcome.wav";                 // SZ_RADIO_WELCOME_FILE_NAME

// OnIdle / StartPlayingNextSelectionOfGivenType tuning, as in the binary.
constexpr std::uint32_t kRZRadioIdleDelayMs = 4000;        // ms a selection has to play before the next one
constexpr unsigned kRZRadioMusicRepeatPercent = 75;        // first branch: repeat music
constexpr unsigned kRZRadioDjPercent = 50;                 // case 1: continue past a DJ cycle
constexpr unsigned kRZRadioCommercialPercent = 10;         // case 2
constexpr unsigned kRZRadioJinglePercent = 10;             // case 3

class cRZDiskRadioStation final : public cIGZRadioStation {
public:
    cRZDiskRadioStation();                            // @ 0x1004c3360 (alias 0x1004c388c)
    ~cRZDiskRadioStation() override;                  // @ 0x1004c3896 / 0x1004c39b4 / 0x1004c39be

    // --- cIGZUnknown ---
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1004c39ec
    std::uint32_t AddRef() override;                              // @ 0x1004c3a0e
    std::uint32_t Release() override;                             // @ 0x1004c3a1c

    // --- cIGZRadioStation (slots 3..14, 17..27 of the binary vtable) ---
    bool Init() override;                                         // @ 0x1004c3a3c (always true)
    bool Shutdown() override;                                     // @ 0x1004c3a44 (always true)
    bool OnIdle() override;                                       // @ 0x1004c3b50
    void SetRadio(cIGZRadio* radio) override;                     // @ 0x1004c86c6
    void SetStation() override;                                   // @ 0x1004c3a4c
    void UnsetStation() override;                                 // @ 0x1004c3a9c (empty)
    void GetStationCallSign(char* out) override;                  // @ 0x1004c5af2 (four bytes, no terminator)
    void GetStationDescription(cIGZString* out) override;         // @ 0x1004c5b4e
    bool IsOnTheAir() override;                                   // @ 0x1004c5b9c
    void SetPlaylistMode(std::int32_t mode) override;             // @ 0x1004c5ba6
    void SetShuffleStrategy(std::int32_t strategy) override;      // @ 0x1004c5bb2
    void SetModifierList(std::list<cRZString>* modifiers) override;   // @ 0x1004c6070
    bool LoadAllPlaylists() override;                             // @ 0x1004c4590 (the binary returns nothing)
    bool ShufflePlaylist(std::int32_t strategy) override;         // @ 0x1004c5286
    bool ShufflePlayList(const cRZString& directory, std::list<cRZString>& list,
                         std::int32_t strategy) override;         // @ 0x1004c578a
    void ShuffleCycle() override;                                 // @ 0x1004c5ab2
    bool IsCycleTypePresent(std::int32_t cycleType) override;     // @ 0x1004c5bbe
    bool MoveToNextPlaylistItem(std::int32_t cycleType) override; // @ 0x1004c5c64
    bool ReadCallSignFromDisk() override;                         // @ 0x1004c5d54
    bool StartPlayingNextSelectionOfGivenType(std::int32_t cycleType) override;   // @ 0x1004c428c
    bool PlayWelcome() override;                                  // @ 0x1004c3aa2
    bool ExpandPlayLists(cRZString& directory, std::list<cRZString>& list) override;   // @ 0x1004c4c36
    void PlayNextSelectionNotOnExclusionList() override;          // @ 0x1004c3eca (the binary returns 1)

    // Not vtable slots in the binary.
    bool DoesFileExist(const cIGZString& fileName);               // @ 0x1004c3da4

    // --- read-only inspection (the original's callers wrote the members directly) ---
    std::uint32_t RefCount() const { return refCount_; }
    bool WelcomePlayed() const { return welcomePlayed_; }
    bool CycleStarted() const { return started_; }
    std::int32_t CycleIndex() const { return cycleIndex_; }
    const std::vector<std::int32_t>& CycleOrder() const { return cycles_; }
    const cRZString& CallSign() const { return callSign_; }
    const cRZString& Description() const { return description_; }
    const cRZString& StationDirectory() const { return stationDirectory_; }
    const cRZString& MusicDirectory() const { return musicDirectory_; }
    const cRZString& DjDirectory() const { return djDirectory_; }
    const cRZString& CommercialDirectory() const { return commercialDirectory_; }
    const cRZString& JingleDirectory() const { return jingleDirectory_; }
    const std::list<cRZString>& MusicPlaylist() const { return musicList_; }
    const std::list<cRZString>& DjPlaylist() const { return djList_; }
    const std::list<cRZString>& CommercialPlaylist() const { return commercialList_; }
    const std::list<cRZString>& JinglePlaylist() const { return jingleList_; }
    const std::list<cRZString>& ModifierList() const { return modifierList_; }
    std::int32_t PlaylistMode() const { return playlistMode_; }
    std::int32_t ShuffleStrategy() const { return shuffleStrategy_; }

private:
    friend class cRZDiskRadioStationHost;

    std::uint32_t refCount_ = 0;                          // +0x08
    bool onAir_ = true;                                   // +0x0c
    bool welcomePlayed_ = false;                          // +0x0d
    std::vector<std::int32_t> cycles_;                    // +0x10
    std::int32_t cycleIndex_ = 0;                         // +0x28
    std::int32_t musicRepeatCount_ = 0;                   // +0x2c (written, never read)
    bool started_ = false;                                // +0x30
    cRZTimer timer_{cRZTimer::kMilliseconds};             // +0x38
    cIGZRadio* radio_ = nullptr;                          // +0x50 borrowed
    cIGZSnd* sound_ = nullptr;                            // +0x58 borrowed, cached by SetRadio
    cRZString callSign_{"????"};                          // +0x60
    cRZString description_;                               // +0x88
    cRZString stationDirectory_;                          // +0xb0 (std::string at +0xb8)
    cRZString musicDirectory_;                            // +0xd8 (std::string at +0xe0)
    std::list<cRZString> musicList_;                      // +0x100
    std::list<cRZString>::iterator musicCursor_{};        // +0x118
    cRZString djDirectory_;                               // +0x120 (std::string at +0x128)
    std::list<cRZString> djList_;                         // +0x148
    std::list<cRZString>::iterator djCursor_{};           // +0x160
    cRZString commercialDirectory_;                       // +0x168 (std::string at +0x170)
    std::list<cRZString> commercialList_;                 // +0x190
    std::list<cRZString>::iterator commercialCursor_{};   // +0x1a8
    cRZString jingleDirectory_;                           // +0x1b0 (std::string at +0x1b8)
    std::list<cRZString> jingleList_;                     // +0x1d8
    std::list<cRZString>::iterator jingleCursor_{};       // +0x1f0
    std::list<cRZString> modifierList_;                   // +0x1f8
    std::int32_t playlistMode_ = 0;                       // +0x210
    std::int32_t shuffleStrategy_ = 0;                    // +0x214
};

// @ 0x1003def08: new (0x218) cRZDiskRadioStation, no AddRef (cRZRadio's SetupDiskStations adds the reference).
cRZDiskRadioStation* CreatecRZDiskRadioStation();

// The built-in IRadioStationHost (cRZRadio's seam over this class): hands out real cRZDiskRadioStation objects and
// exposes the directory strings / the "cycle started" byte the original wrote directly. Never null; the radio uses it
// when no other host was installed. @ 0x1004c792e (the original's `new cRZDiskRadioStation`) and @ 0x1004c8674.
IRadioStationHost* DiskRadioStationHost();

class cRZDiskRadioStationHost final : public IRadioStationHost {
public:
    // @ 0x1004c792e: `new cRZDiskRadioStation()`, before the AddRef cRZRadio does next.
    cIGZRadioStation* CreateDiskStation() override;
    // The station's own cRZStrings, written by SetupDiskStations in the original (+0xb0, +0xd8, +0x120, +0x168, +0x1b0;
    // the std::string inside each one sits at +0xb8, +0xe0, +0x128, +0x170, +0x1b8).
    cRZString& StationDirectory(cIGZRadioStation* station) override;
    cRZString& MusicDirectory(cIGZRadioStation* station) override;
    cRZString& DjDirectory(cIGZRadioStation* station) override;
    cRZString& CommercialDirectory(cIGZRadioStation* station) override;
    cRZString& JingleDirectory(cIGZRadioStation* station) override;
    // @ 0x1004c8674: DoEvent clears the station's byte at +0x30 (its "cycle started" flag).
    void ClearStartedFlag(cIGZRadioStation* station) override;
};

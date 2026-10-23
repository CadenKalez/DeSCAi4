// cRZDiskRadioStation — test.
//
// The station is driven over real directories and files in a temporary tree (the class is nothing but directory
// walking): the "*.id" call sign and "*.txt" description, LoadAllPlaylists over the five sound-file filters,
// ExpandPlayLists over .m3u playlists, both shuffle strategies (random and file-time), the cursor walk of
// StartPlayingNextSelectionOfGivenType / MoveToNextPlaylistItem / PlayNextSelectionNotOnExclusionList, PlayWelcome,
// ShuffleCycle, the OnIdle state machine (driven with the stub clock), the modifier list, DoesFileExist, the
// reference count / QueryInterface, the layout offsets and the built-in IRadioStationHost.
//
// Build (from the repo root):
//   clang++ -std=c++20 -Wall -Wextra -O1 -fsanitize=address,undefined -Isrc -Iscripts/tests/stubs -Iscripts/tests
//       scripts/tests/rz_diskradiostation_test.cpp src/rz/DiskRadioStation.cpp src/rz/cRZString.cpp
//       src/rz/cRZDirectory.cpp src/gz/FileFind.cpp src/rz/cRZFile.cpp src/rz/cRZTimer.cpp src/gz/PlayList.cpp
//       src/rz/RZStringUtils.cpp src/rz/StringConversion.cpp src/rz/TextEncoding.cpp src/rz/CharLength.cpp
//       src/rz/RZConvert.cpp src/platform/MacTools.cpp -o /tmp/drs_test && /tmp/drs_test
#include <SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <list>
#include <string>
#include <utility>
#include <vector>

#include <sys/time.h>
#include <unistd.h>

#include "rz/DiskRadioStation.h"
#include "rz/Radio.h"
#include "rz/cRZString.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
#define CHECK_EQ(a, b) do { auto&& _a = (a); auto&& _b = (b); if (!(_a == _b)) { \
    std::printf("FAIL line %d: %s == %s\n", __LINE__, #a, #b); ++failures; } } while (0)

namespace {

// ------------------------------------------------------------------ fakes

// cIGZRadio as the station uses it: the volume / playing state, the three options, PlaySong and GetRadioSound.
struct FakeRadio final : cIGZRadio {
    std::uint32_t refs = 0;
    std::int32_t volume = kRZRadioMaxVolume;
    bool playing = false;
    // cRZRadio answers options 0, 1 and 3 only (kRadioOption2 is always false) - the station asks for 2 for the
    // jingle cycle, so this table mirrors the real radio.
    bool options[4] = {true, true, false, true};
    cIGZSnd* sound = nullptr;
    std::vector<std::string> played;

    bool QueryInterface(std::uint32_t, void** out) override { *out = this; AddRef(); return true; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return refs ? --refs : 0; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool OnIdle() override { return true; }
    cIGZSnd* GetRadioSound() override { return sound; }
    bool PlaySong(const cRZString& song) override { played.push_back(song.str()); return true; }
    void PlayNextSong(bool) override {}
    bool GetCurrentStation(std::uint32_t&) override { return false; }
    bool GetCurrentStation(char*) override { return false; }
    cIGZRadioStation* GetStation(std::uint32_t) override { return nullptr; }
    bool SetStation(std::uint32_t) override { return true; }
    cIGZRadioStation* GetStation(const char*) override { return nullptr; }
    bool SetStation(const char*, const FadeOptions*) override { return true; }
    std::int32_t GetVolume() override { return volume; }
    bool SetVolume(std::int32_t value) override { volume = value; return true; }
    std::uint32_t GetStationCount() override { return 0; }
    bool IsStationAvailable(const char*) override { return false; }
    bool AddStation(cIGZRadioStation*) override { return true; }
    bool RemoveStation(cIGZRadioStation*, bool) override { return true; }
    bool RemoveStation(const char*, bool) override { return true; }
    bool RemoveAllStations(bool) override { return true; }
    bool TurnOn(bool) override { return true; }
    bool IsOn() override { return true; }
    void Mute(bool) override {}
    bool GetOption(RadioOption option) override {
        const std::int32_t index = static_cast<std::int32_t>(option);
        return index >= 0 && index < 4 ? options[index] : false;
    }
    void SetOption(bool value, RadioOption option) override { options[static_cast<std::int32_t>(option)] = value; }
    bool GetRadioDirectory(cIGZString&) override { return true; }
    void SetRadioDirectory(const cIGZString&) override {}
    void SetOnDemandState(bool) override {}
    void StopPlayback() override {}
    bool GetCurrentSong(cRZString&) override { return true; }
    void SetCurrentSong(const cRZString&) override {}
    bool IsPlaying() override { return playing; }
    void ReadSettings(RadioSettings*) override {}
    void WriteSettings(RadioSettings*) override {}
};

// ------------------------------------------------------------------ helpers

// The libc++ std::random_shuffle both the constructor and ShuffleCycle/ShufflePlayList use.
std::vector<std::int32_t> ReferenceShuffle(std::vector<std::int32_t> values) {
    for (std::size_t i = 1; i < values.size(); ++i) {
        const std::size_t j = static_cast<std::size_t>(std::rand()) % (i + 1);
        if (i != j) std::swap(values[i], values[j]);
    }
    return values;
}

// Itanium pointer-to-member encoding of a virtual function: the vtable byte offset (with a low "virtual" bit on the
// x86_64 variant). Masking bit 0 reads the same on both.
template <typename Function>
std::size_t VirtualSlotOffset(Function function) {
    struct Representation {
        std::ptrdiff_t pointer;
        std::ptrdiff_t adjustment;
    } representation{};
    static_assert(sizeof(representation) == sizeof(function), "pointer to member function size");
    std::memcpy(&representation, &function, sizeof representation);
    return static_cast<std::size_t>(representation.pointer) & ~static_cast<std::size_t>(1);
}

void WriteFile(const std::string& path, const std::string& contents = "x") {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

void SetModifiedTime(const std::string& path, std::time_t seconds) {
    struct timeval times[2];
    times[0].tv_sec = seconds;
    times[0].tv_usec = 0;
    times[1] = times[0];
    ::utimes(path.c_str(), times);
}

std::vector<std::string> Sorted(const std::list<cRZString>& values) {
    std::vector<std::string> out;
    for (const cRZString& value : values) out.push_back(value.str());
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> Names(const std::list<cRZString>& values) {
    std::vector<std::string> out;
    for (const cRZString& value : values) out.push_back(value.str());
    return out;
}

bool Contains(const std::list<cRZString>& values, const std::string& value) {
    for (const cRZString& item : values) {
        if (item.str() == value) return true;
    }
    return false;
}

// The entry StartPlayingNextSelectionOfGivenType plays next: the cursor (set to the first node by ShufflePlaylist)
// advanced by one, wrapping to the first node.
std::string SecondEntry(const std::list<cRZString>& values) {
    if (values.empty()) return std::string();
    if (values.size() == 1) return values.front().str();
    return std::next(values.begin())->str();
}

// The temporary tree the station walks.
struct Fixture {
    std::string root;

    explicit Fixture(const std::string& name) {
        // The pid keeps a second (parallel) run of this test out of the same tree.
        root = (std::filesystem::temp_directory_path() / (name + "_" + std::to_string(::getpid()))).string();
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        if (!root.empty() && root.back() != '/') root.push_back('/');
    }
    ~Fixture() { std::filesystem::remove_all(root); }

    std::string Stations() const { return root + "Stations/"; }
    std::string Commercials() const { return root + "Commercials/"; }
    std::string Station(const std::string& name) const { return Stations() + name + "/"; }

    // The full station: call sign, description, four playlists (one of them an .m3u) and a shared commercial directory.
    void WriteAlpha() {
        WriteFile(Station("Alpha") + "Alpha.id");
        WriteFile(Station("Alpha") + "Alpha Station.txt", "description");
        WriteFile(Station("Alpha") + "Music/drs_a.mp3");
        WriteFile(Station("Alpha") + "Music/drs_b.wav");
        WriteFile(Station("Alpha") + "Music/drs_zz.mp3");
        WriteFile(Station("Alpha") + "Music/drs_notes.xa");
        WriteFile(Station("Alpha") + "Music/drs_list.m3u", "drs_c.mp3\nsub/drs_c2.mp3\n");
        WriteFile(Station("Alpha") + "Music/drs_broken.pls", "");   // an empty .pls: listed, expanded, removed
        WriteFile(Station("Alpha") + "DJ/drs_dj1.mp3");
        WriteFile(Station("Alpha") + "Jingle/drs_j1.mp3");
        WriteFile(Commercials() + "drs_comm1.mp3");
    }

    void WriteSolo() {
        WriteFile(Station("Solo") + "Solo.id");
        WriteFile(Station("Solo") + "Music/drs_solo.mp3");
        std::filesystem::create_directories(Station("Solo") + "DJ/");
    }

    void WriteTimed() {
        WriteFile(Station("Time") + "Time.id");
        WriteFile(Station("Time") + "Music/drs_t1.mp3");
        WriteFile(Station("Time") + "Music/drs_t2.mp3");
        WriteFile(Station("Time") + "Music/drs_t3.mp3");
        SetModifiedTime(Station("Time") + "Music/drs_t1.mp3", 3'000'000);
        SetModifiedTime(Station("Time") + "Music/drs_t2.mp3", 2'000'000);
        SetModifiedTime(Station("Time") + "Music/drs_t3.mp3", 1'000'000);
    }
};

// Points a station's five directories at a fixture tree (what cRZRadio::SetupDiskStations writes through the host).
void WireDirectories(cRZDiskRadioStation* station, const std::string& stationDirectory, const std::string& commercial) {
    IRadioStationHost* host = DiskRadioStationHost();
    host->StationDirectory(station).str() = stationDirectory;
    host->MusicDirectory(station).str() = stationDirectory + "Music/";
    host->DjDirectory(station).str() = stationDirectory + "DJ/";
    host->CommercialDirectory(station).str() = commercial;
    host->JingleDirectory(station).str() = stationDirectory + "Jingle/";
}

}  // namespace

int main() {
    // -------------------------------------------------------------------------------------------------------------
    // 1. Layout: the interface slots are the binary's vtable offsets (0x100ab6e80) and the object is 0x218 bytes.
    // -------------------------------------------------------------------------------------------------------------
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::Init), 0x18u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::OnIdle), 0x28u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::SetRadio), 0x30u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::SetStation), 0x38u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::GetStationCallSign), 0x48u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::GetStationDescription), 0x50u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::IsOnTheAir), 0x58u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::SetModifierList), 0x70u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::LoadAllPlaylists), 0x88u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::ShufflePlaylist), 0x90u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::ShufflePlayList), 0x98u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::ShuffleCycle), 0xa0u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::IsCycleTypePresent), 0xa8u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::MoveToNextPlaylistItem), 0xb0u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::ReadCallSignFromDisk), 0xb8u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::StartPlayingNextSelectionOfGivenType), 0xc0u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::PlayWelcome), 0xc8u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::ExpandPlayLists), 0xd0u);
    CHECK_EQ(VirtualSlotOffset(&cIGZRadioStation::PlayNextSelectionNotOnExclusionList), 0xd8u);
    CHECK_EQ(sizeof(cRZDiskRadioStation), 0x218u);

    FakeRadio radio;

    // -------------------------------------------------------------------------------------------------------------
    // 2. Construction, QueryInterface and the reference count.
    // -------------------------------------------------------------------------------------------------------------
    {
        std::srand(0);
        auto* station = new cRZDiskRadioStation();
        CHECK_EQ(station->RefCount(), 0u);
        CHECK(station->IsOnTheAir());
        CHECK(!station->WelcomePlayed());
        CHECK(!station->CycleStarted());
        CHECK_EQ(station->CycleIndex(), 0);
        CHECK_EQ(station->PlaylistMode(), 0);
        CHECK_EQ(station->ShuffleStrategy(), 0);
        CHECK(station->MusicPlaylist().empty());
        CHECK(station->DjPlaylist().empty());
        CHECK(station->CommercialPlaylist().empty());
        CHECK(station->JinglePlaylist().empty());
        CHECK(station->ModifierList().empty());

        // The call sign starts as "????" (four '?' bytes, no terminator) and is the description until one is read.
        char callSign[8];
        std::memset(callSign, '@', sizeof callSign);
        station->GetStationCallSign(callSign);
        CHECK_EQ(std::string(callSign, 4), "????");
        CHECK_EQ(callSign[4], '@');   // exactly four bytes are written
        cRZString description;
        station->GetStationDescription(&description);
        CHECK_EQ(description.str(), "????");

        // The cycle order is {0,1,2,3} shuffled by the constructor.
        std::vector<std::int32_t> order = station->CycleOrder();
        CHECK_EQ(order.size(), std::size_t(kRZRadioCycleTypeCount));
        std::vector<std::int32_t> sortedOrder = order;
        std::sort(sortedOrder.begin(), sortedOrder.end());
        CHECK(sortedOrder == std::vector<std::int32_t>({0, 1, 2, 3}));

        // QueryInterface: only 0x4056574b and 1, both with the same pointer, each adding a reference.
        void* out = nullptr;
        CHECK(station->QueryInterface(kGZIID_cIGZRadioStation, &out));
        CHECK(out == static_cast<void*>(station));
        CHECK_EQ(station->RefCount(), 1u);
        out = nullptr;
        CHECK(station->QueryInterface(kGZIID_cIGZUnknown, &out));
        CHECK(out == static_cast<void*>(station));
        CHECK_EQ(station->RefCount(), 2u);
        out = reinterpret_cast<void*>(0x1);
        CHECK(!station->QueryInterface(0x12345678u, &out));
        CHECK(out == reinterpret_cast<void*>(0x1));
        CHECK_EQ(station->Release(), 1u);
        CHECK_EQ(station->Release(), 0u);   // count 1: the deleting destructor runs
    }
    {
        // Init / Shutdown answer true and change nothing.
        auto* station = new cRZDiskRadioStation();
        CHECK(station->Init());
        CHECK(station->Shutdown());
        CHECK(station->IsOnTheAir());
        delete station;
    }

    // -------------------------------------------------------------------------------------------------------------
    // 3. The built-in host: it creates real stations, exposes their strings and clears the "started" flag.
    // -------------------------------------------------------------------------------------------------------------
    {
        IRadioStationHost* host = DiskRadioStationHost();
        CHECK(host != nullptr);
        cIGZRadioStation* created = host->CreateDiskStation();
        CHECK(created != nullptr);
        auto* station = static_cast<cRZDiskRadioStation*>(created);
        host->StationDirectory(station).str() = "Stations/Alpha/";
        CHECK_EQ(station->StationDirectory().str(), "Stations/Alpha/");
        host->MusicDirectory(station).str() = "Stations/Alpha/Music/";
        CHECK_EQ(station->MusicDirectory().str(), "Stations/Alpha/Music/");
        host->DjDirectory(station).str() = "Stations/Alpha/DJ/";
        CHECK_EQ(station->DjDirectory().str(), "Stations/Alpha/DJ/");
        host->CommercialDirectory(station).str() = "Commercials/";
        CHECK_EQ(station->CommercialDirectory().str(), "Commercials/");
        host->JingleDirectory(station).str() = "Stations/Alpha/Jingle/";
        CHECK_EQ(station->JingleDirectory().str(), "Stations/Alpha/Jingle/");

        // The host is what cRZRadio uses to clear the byte at +0x30 before asking for the next selection.
        g_ticks = 1000;
        station->SetRadio(&radio);
        CHECK(station->OnIdle());          // "not on the air" branch would leave it alone; this sets it
        CHECK(station->CycleStarted());
        host->ClearStartedFlag(station);
        CHECK(!station->CycleStarted());
        delete station;
    }

    // -------------------------------------------------------------------------------------------------------------
    // 4. Real directories: call sign, description, playlists, expansion and DoesFileExist.
    // -------------------------------------------------------------------------------------------------------------
    {
        Fixture fixture("rz_drs_alpha");
        fixture.WriteAlpha();
        fixture.WriteSolo();
        auto* station = CreatecRZDiskRadioStation();
        station->SetRadio(&radio);
        WireDirectories(station, fixture.Station("Alpha"), fixture.Commercials());

        // ReadCallSignFromDisk: "Alpha.id" -> "Alpha", "Alpha Station.txt" -> "Alpha Station".
        CHECK(station->ReadCallSignFromDisk());
        CHECK_EQ(station->CallSign().str(), "Alpha");
        CHECK_EQ(station->Description().str(), "Alpha Station");
        char callSign[8];
        std::memset(callSign, '@', sizeof callSign);
        station->GetStationCallSign(callSign);
        CHECK_EQ(std::string(callSign, 4), "Alph");
        CHECK_EQ(callSign[4], '@');
        cRZString description;
        station->GetStationDescription(&description);
        CHECK_EQ(description.str(), "Alpha Station");

        // Removing the description file does not clear it (the original only writes when it finds a file).
        std::filesystem::remove(fixture.Station("Alpha") + "Alpha Station.txt");
        CHECK(station->ReadCallSignFromDisk());
        CHECK_EQ(station->Description().str(), "Alpha Station");

        // LoadAllPlaylists: every sound-file filter appends, then the .m3u/.pls entries are expanded in place.
        CHECK(station->LoadAllPlaylists());
        const std::vector<std::string> music = Sorted(station->MusicPlaylist());
        // drs_a.mp3 and drs_zz.mp3 (*.mp3), drs_b.wav (*.wav), drs_notes.xa (*.xa) and the two drs_list.m3u entries.
        // The two playlist files themselves (the .m3u and the empty .pls) are removed by ExpandPlayLists.
        const std::vector<std::string> expectedMusic = {"drs_a.mp3", "drs_b.wav", "drs_c.mp3", "drs_notes.xa",
                                                        "drs_zz.mp3", "sub/drs_c2.mp3"};
        CHECK(music == expectedMusic);
        CHECK_EQ(Sorted(station->DjPlaylist()), (std::vector<std::string>{"drs_dj1.mp3"}));
        CHECK_EQ(Sorted(station->CommercialPlaylist()), (std::vector<std::string>{"drs_comm1.mp3"}));
        CHECK_EQ(Sorted(station->JinglePlaylist()), (std::vector<std::string>{"drs_j1.mp3"}));
        // Expanded entries are appended at the end, the playlist files are gone.
        CHECK_EQ(station->MusicPlaylist().size(), std::size_t(6));
        CHECK(!Contains(station->MusicPlaylist(), "drs_list.m3u"));
        CHECK(!Contains(station->MusicPlaylist(), "drs_broken.pls"));

        // DoesFileExist: as given first, then below the music directory.
        CHECK(station->DoesFileExist(cRZString("drs_a.mp3")));
        CHECK(station->DoesFileExist(cRZString((fixture.Station("Alpha") + "Music/drs_a.mp3").c_str())));
        CHECK(!station->DoesFileExist(cRZString("nope.mp3")));
        CHECK(!station->DoesFileExist(cRZString("drs_dj1.mp3")));   // only the music directory is searched

        // Loading twice replaces the lists (the original clears all four first).
        CHECK(station->LoadAllPlaylists());
        CHECK_EQ(Sorted(station->MusicPlaylist()), expectedMusic);

        // ExpandPlayLists on a hand-made list: a non-playlist name stays, a playlist is replaced by its entries,
        // and a listed name that cannot be opened is still removed (the erase does not depend on Open).
        {
            std::list<cRZString> list = {cRZString("drs_a.mp3"), cRZString("drs_list.m3u"),
                                         cRZString("missing.m3u")};
            CHECK(station->ExpandPlayLists(DiskRadioStationHost()->MusicDirectory(station), list));
            CHECK_EQ(Names(list), (std::vector<std::string>{"drs_a.mp3", "drs_c.mp3", "sub/drs_c2.mp3"}));
        }
        {
            // The extension test ignores case, so an upper-case name is expanded as well.
            WriteFile(fixture.Station("Alpha") + "Music/UPPER.M3U", "drs_u.mp3\n");
            std::list<cRZString> list = {cRZString("UPPER.M3U")};
            CHECK(station->ExpandPlayLists(DiskRadioStationHost()->MusicDirectory(station), list));
            CHECK_EQ(Names(list), (std::vector<std::string>{"drs_u.mp3"}));
        }

        // Another station directory: the call sign is re-read.
        WireDirectories(station, fixture.Station("Solo"), fixture.Commercials());
        CHECK(station->ReadCallSignFromDisk());
        CHECK_EQ(station->CallSign().str(), "Solo");
        delete station;

        // A directory that does not exist: no file is found, but the answer still says "there is a call sign" -
        // ReadCallSignFromDisk returns callSign_.Strlen() != 0, and the constructor pre-filled "????" (quirk kept).
        auto* missing = CreatecRZDiskRadioStation();
        WireDirectories(missing, fixture.Station("Missing"), fixture.Commercials());
        CHECK(missing->ReadCallSignFromDisk());
        CHECK_EQ(missing->CallSign().str(), "????");
        std::memset(callSign, '@', sizeof callSign);
        missing->GetStationCallSign(callSign);
        CHECK_EQ(std::string(callSign, 4), "????");
        delete missing;
    }

    // -------------------------------------------------------------------------------------------------------------
    // 5. ShufflePlaylist: random order, the "do not repeat the last track" swap, and the file-time strategy.
    // -------------------------------------------------------------------------------------------------------------
    {
        Fixture fixture("rz_drs_shuffle");
        fixture.WriteAlpha();
        auto* station = CreatecRZDiskRadioStation();
        station->SetRadio(&radio);
        WireDirectories(station, fixture.Station("Alpha"), fixture.Commercials());
        CHECK(station->ReadCallSignFromDisk());
        CHECK(station->LoadAllPlaylists());

        // Moving to the next item of every cycle type and back to the first one via the wrap.
        for (std::int32_t cycle = 0; cycle < kRZRadioCycleTypeCount; ++cycle) {
            CHECK(station->MoveToNextPlaylistItem(cycle));
        }
        CHECK(station->MoveToNextPlaylistItem(7));   // unknown type: no cursor moves, still true

        // Strategy 0: a permutation of the same entries, and the reshuffled first entry never equals the one that
        // was last before the shuffle (the swap quirk). 40 seeds exercise the collision.
        std::list<cRZString> previous = station->MusicPlaylist();
        for (int seed = 0; seed < 40; ++seed) {
            const std::string previousLast = previous.back().str();
            const std::vector<std::string> before = Sorted(station->MusicPlaylist());
            std::srand(seed);
            CHECK(station->ShufflePlaylist(0));
            CHECK_EQ(Sorted(station->MusicPlaylist()), before);
            CHECK(station->MusicPlaylist().front().str() != previousLast);
            CHECK_EQ(station->MusicPlaylist().size(), before.size());
            previous = station->MusicPlaylist();
        }

        // The cursor points at the first node, so the walk plays index 1, 2, ... and wraps back to index 0.
        const std::vector<std::string> musicOrder = Names(station->MusicPlaylist());
        for (std::size_t step = 1; step <= musicOrder.size() + 1; ++step) {
            radio.played.clear();
            CHECK(station->StartPlayingNextSelectionOfGivenType(kRZRadioCycleMusic));
            CHECK_EQ(radio.played.size(), std::size_t(1));
            CHECK_EQ(radio.played.back(), station->MusicDirectory().str() + musicOrder[step % musicOrder.size()]);
        }

        // DJ / commercial / jingle play their own directory + entry, without an existence check.
        radio.played.clear();
        CHECK(station->StartPlayingNextSelectionOfGivenType(kRZRadioCycleDj));
        CHECK_EQ(radio.played.back(), station->DjDirectory().str() + "drs_dj1.mp3");
        CHECK(station->StartPlayingNextSelectionOfGivenType(kRZRadioCycleCommercial));
        CHECK_EQ(radio.played.back(), station->CommercialDirectory().str() + "drs_comm1.mp3");
        CHECK(station->StartPlayingNextSelectionOfGivenType(kRZRadioCycleJingle));
        CHECK_EQ(radio.played.back(), station->JingleDirectory().str() + "drs_j1.mp3");
        // Out of range: nothing is played and the answer is false (unsigned compare in the binary).
        const std::size_t playedBefore = radio.played.size();
        CHECK(!station->StartPlayingNextSelectionOfGivenType(4));
        CHECK(!station->StartPlayingNextSelectionOfGivenType(-1));
        CHECK_EQ(radio.played.size(), playedBefore);
        delete station;
    }
    {
        // Strategy 1: newest file first, still subject to the "first != previous last" swap.
        Fixture fixture("rz_drs_time");
        fixture.WriteTimed();
        auto* station = CreatecRZDiskRadioStation();
        station->SetRadio(&radio);
        WireDirectories(station, fixture.Station("Time"), fixture.Commercials());
        CHECK(station->LoadAllPlaylists());
        CHECK_EQ(station->MusicPlaylist().size(), std::size_t(3));
        const std::string previousLast = station->MusicPlaylist().back().str();
        CHECK(station->ShufflePlaylist(1));
        std::vector<std::string> expected = {"drs_t1.mp3", "drs_t2.mp3", "drs_t3.mp3"};
        if (previousLast == expected.front()) std::swap(expected.front(), expected.back());
        CHECK_EQ(Names(station->MusicPlaylist()), expected);

        // The comparator itself (RZFileTimeSort::operator() @ 0x1004c9230): newer first, strict, and built from the
        // directory it was constructed with.
        RZFileTimeSort sorter(station->MusicDirectory());
        CHECK(sorter(cRZString("drs_t1.mp3"), cRZString("drs_t2.mp3")));
        CHECK(sorter(cRZString("drs_t1.mp3"), cRZString("drs_t3.mp3")));
        CHECK(sorter(cRZString("drs_t2.mp3"), cRZString("drs_t3.mp3")));
        CHECK(!sorter(cRZString("drs_t2.mp3"), cRZString("drs_t1.mp3")));
        CHECK(!sorter(cRZString("drs_t1.mp3"), cRZString("drs_t1.mp3")));
        delete station;
    }

    // -------------------------------------------------------------------------------------------------------------
    // 6. The modifier list and the two playlist modes of PlayNextSelectionNotOnExclusionList.
    // -------------------------------------------------------------------------------------------------------------
    {
        Fixture fixture("rz_drs_modifiers");
        fixture.WriteSolo();
        auto* station = CreatecRZDiskRadioStation();
        station->SetRadio(&radio);
        WireDirectories(station, fixture.Station("Solo"), fixture.Commercials());

        std::list<cRZString> modifiers = {cRZString("drs_solo.mp3"), cRZString("other.mp3")};
        station->SetModifierList(&modifiers);
        CHECK_EQ(Sorted(station->ModifierList()), (std::vector<std::string>{"drs_solo.mp3", "other.mp3"}));
        modifiers.clear();                                      // the station keeps its own copy
        CHECK_EQ(station->ModifierList().size(), std::size_t(2));
        station->SetModifierList(nullptr);                      // null clears it
        CHECK(station->ModifierList().empty());

        CHECK(station->LoadAllPlaylists());
        CHECK_EQ(station->MusicPlaylist().size(), std::size_t(1));
        CHECK_EQ(station->MusicPlaylist().front().str(), "drs_solo.mp3");
        CHECK(station->ShufflePlaylist(0));

        // Playlist mode 1 skips the entries that ARE on the modifier list: with the only entry on the list the walk
        // goes all the way round and plays nothing.
        std::list<cRZString> soloModifier = {cRZString("drs_solo.mp3")};
        station->SetModifierList(&soloModifier);
        station->SetPlaylistMode(1);
        radio.played.clear();
        station->PlayNextSelectionNotOnExclusionList();
        CHECK(radio.played.empty());

        // Mode 1 with an unrelated modifier list plays the entry.
        std::list<cRZString> unrelated = {cRZString("other.mp3")};
        station->SetModifierList(&unrelated);
        station->PlayNextSelectionNotOnExclusionList();
        CHECK_EQ(radio.played.size(), std::size_t(1));
        CHECK_EQ(radio.played.back(), station->MusicDirectory().str() + "drs_solo.mp3");

        // Any other mode only accepts entries that ARE on the list.
        station->SetPlaylistMode(0);
        radio.played.clear();
        station->PlayNextSelectionNotOnExclusionList();   // drs_solo.mp3 is not on {other.mp3}: nothing
        CHECK(radio.played.empty());
        station->SetModifierList(&soloModifier);
        station->PlayNextSelectionNotOnExclusionList();
        CHECK_EQ(radio.played.size(), std::size_t(1));
        CHECK_EQ(radio.played.back(), station->MusicDirectory().str() + "drs_solo.mp3");

        // With a playlist mode set, StartPlayingNextSelectionOfGivenType(0) takes the exclusion walk instead of the
        // plain cursor walk.
        radio.played.clear();
        station->SetPlaylistMode(1);
        station->SetModifierList(&unrelated);
        CHECK(station->StartPlayingNextSelectionOfGivenType(kRZRadioCycleMusic));
        CHECK_EQ(radio.played.size(), std::size_t(1));

        // Playlist mode 0 (or mode 1 with an empty modifier list) takes the plain cursor walk instead.
        station->SetPlaylistMode(0);
        station->SetModifierList(nullptr);
        radio.played.clear();
        CHECK(station->StartPlayingNextSelectionOfGivenType(kRZRadioCycleMusic));
        CHECK_EQ(radio.played.size(), std::size_t(1));
        delete station;
    }

    // -------------------------------------------------------------------------------------------------------------
    // 7. IsCycleTypePresent: lists plus the radio options; the jingle cycle asks for option 2, which cRZRadio never
    // answers, so it is never present.
    // -------------------------------------------------------------------------------------------------------------
    {
        Fixture fixture("rz_drs_cycles");
        fixture.WriteAlpha();
        auto* station = CreatecRZDiskRadioStation();
        station->SetRadio(&radio);
        WireDirectories(station, fixture.Station("Alpha"), fixture.Commercials());
        CHECK(station->LoadAllPlaylists());

        radio.options[0] = true;
        radio.options[1] = true;
        CHECK(station->IsCycleTypePresent(kRZRadioCycleMusic));
        CHECK(station->IsCycleTypePresent(kRZRadioCycleDj));
        CHECK(station->IsCycleTypePresent(kRZRadioCycleCommercial));
        CHECK(!station->IsCycleTypePresent(kRZRadioCycleJingle));   // option 2 is never answered
        CHECK(!station->IsCycleTypePresent(4));

        radio.options[0] = false;
        CHECK(!station->IsCycleTypePresent(kRZRadioCycleDj));
        radio.options[0] = true;
        radio.options[1] = false;
        CHECK(!station->IsCycleTypePresent(kRZRadioCycleCommercial));
        radio.options[1] = true;

        // A station without playlists answers false for the three playlist cycles.
        auto* empty = CreatecRZDiskRadioStation();
        empty->SetRadio(&radio);
        WireDirectories(empty, fixture.Station("Alpha") + "Nothing/", fixture.Commercials());
        CHECK(!empty->IsCycleTypePresent(kRZRadioCycleMusic));
        CHECK(!empty->IsCycleTypePresent(kRZRadioCycleDj));
        CHECK(!empty->IsCycleTypePresent(kRZRadioCycleCommercial));
        CHECK(!empty->IsCycleTypePresent(kRZRadioCycleJingle));
        delete empty;
        delete station;
    }

    // -------------------------------------------------------------------------------------------------------------
    // 8. SetStation, PlayWelcome and the "already welcomed" flag.
    // -------------------------------------------------------------------------------------------------------------
    {
        Fixture fixture("rz_drs_welcome");
        fixture.WriteAlpha();
        auto* station = CreatecRZDiskRadioStation();
        station->SetRadio(&radio);
        WireDirectories(station, fixture.Station("Alpha"), fixture.Commercials());

        // No Welcome.wav yet: SetStation reads the call sign, loads and shuffles, and does not play anything.
        radio.played.clear();
        station->SetStation();
        CHECK_EQ(station->CallSign().str(), "Alpha");
        CHECK_EQ(station->MusicPlaylist().size(), std::size_t(6));
        CHECK(radio.played.empty());
        CHECK(!station->WelcomePlayed());

        // With Welcome.wav: played from the station directory, the "started" flag is cleared and the welcome flag set.
        WriteFile(fixture.Station("Alpha") + "Welcome.wav");
        g_ticks = 1000;
        CHECK(station->OnIdle());           // sets the "started" flag (first call)
        CHECK(station->CycleStarted());
        radio.played.clear();
        CHECK(station->PlayWelcome());
        CHECK_EQ(radio.played.size(), std::size_t(1));
        CHECK_EQ(radio.played.back(), fixture.Station("Alpha") + "Welcome.wav");
        CHECK(!station->CycleStarted());
        CHECK(station->WelcomePlayed());

        // SetStation keeps the flag: the welcome is not played a second time.
        radio.played.clear();
        station->SetStation();
        CHECK(radio.played.empty());
        CHECK(station->WelcomePlayed());
        // UnsetStation is empty.
        station->UnsetStation();
        CHECK(station->WelcomePlayed());
        delete station;
    }

    // -------------------------------------------------------------------------------------------------------------
    // 9. OnIdle: the state machine (volume, playing, the 4000 ms gate) and the cycle walk, with the stub clock.
    // -------------------------------------------------------------------------------------------------------------
    {
        Fixture fixture("rz_drs_idle");
        fixture.WriteAlpha();
        fixture.WriteSolo();

        // Pick a construction seed whose cycle order does not start with the music cycle and whose second cycle is
        // not the jingle cycle, so the delayed call takes the deterministic "advance one cycle" path.
        int constructionSeed = 0;
        std::vector<std::int32_t> order;
        for (;; ++constructionSeed) {
            std::srand(constructionSeed);
            order = ReferenceShuffle({0, 1, 2, 3});
            if (order[0] != kRZRadioCycleMusic && order[1] != kRZRadioCycleJingle) break;
        }
        std::srand(constructionSeed);
        auto* station = new cRZDiskRadioStation();
        CHECK_EQ(station->CycleOrder(), order);
        station->SetRadio(&radio);
        WireDirectories(station, fixture.Station("Alpha"), fixture.Commercials());
        CHECK(station->LoadAllPlaylists());
        CHECK(station->ShufflePlaylist(0));

        // Off the air: OnIdle answers true and does nothing.
        radio.volume = 0;
        g_ticks = 1000;
        CHECK(station->OnIdle());
        CHECK(!station->CycleStarted());
        radio.volume = kRZRadioMaxVolume;

        // The radio is playing: nothing to do.
        radio.playing = true;
        CHECK(station->OnIdle());
        CHECK(!station->CycleStarted());
        radio.playing = false;

        // First call: the selection is marked as started and the timer is reset / restarted.
        g_ticks = 1000;
        CHECK(station->OnIdle());
        CHECK(station->CycleStarted());
        CHECK_EQ(radio.played.size(), std::size_t(0));

        // Nothing happens before the 4000 ms have elapsed.
        g_ticks = 4999;
        CHECK(station->OnIdle());
        CHECK(station->CycleStarted());
        CHECK(radio.played.empty());

        // After the delay the cycle order advances by one and (unless the jingle cycle is drawn) a selection plays.
        const std::int32_t nextCycle = order[1];
        std::srand(0);
        if (nextCycle == kRZRadioCycleDj) {
            int gateSeed = 0;
            for (;; ++gateSeed) {
                std::srand(gateSeed);
                if (std::rand() % 100 < static_cast<int>(kRZRadioDjPercent)) break;
            }
            std::srand(gateSeed);
        } else if (nextCycle == kRZRadioCycleCommercial) {
            int gateSeed = 0;
            for (;; ++gateSeed) {
                std::srand(gateSeed);
                if (std::rand() % 100 < static_cast<int>(kRZRadioCommercialPercent)) break;
            }
            std::srand(gateSeed);
        }
        g_ticks = 5001;
        radio.played.clear();
        CHECK(station->OnIdle());
        CHECK_EQ(station->CycleIndex(), 1);
        CHECK(!station->CycleStarted());
        CHECK_EQ(radio.played.size(), std::size_t(1));
        std::string expected;
        switch (nextCycle) {
            case kRZRadioCycleMusic:
                expected = station->MusicDirectory().str() + SecondEntry(station->MusicPlaylist());
                break;
            case kRZRadioCycleDj:
                expected = station->DjDirectory().str() + SecondEntry(station->DjPlaylist());
                break;
            case kRZRadioCycleCommercial:
                expected = station->CommercialDirectory().str() + SecondEntry(station->CommercialPlaylist());
                break;
            default:
                break;
        }
        if (!expected.empty()) CHECK_EQ(radio.played.back(), expected);

        // The next call restarts the timer without advancing.
        g_ticks = 6000;
        CHECK(station->OnIdle());
        CHECK(station->CycleStarted());
        CHECK_EQ(station->CycleIndex(), 1);
        delete station;
    }
    {
        // The music-repeat branch: with the music cycle first in the order, 75% of the draws repeat the music cycle
        // without advancing, and it does advance when the draw is above 75.
        int musicFirstSeed = 0;
        for (;; ++musicFirstSeed) {
            std::srand(musicFirstSeed);
            if (ReferenceShuffle({0, 1, 2, 3})[0] == kRZRadioCycleMusic) break;
        }
        Fixture fixture("rz_drs_repeat");
        fixture.WriteSolo();
        std::srand(musicFirstSeed);
        auto* station = new cRZDiskRadioStation();
        station->SetRadio(&radio);
        WireDirectories(station, fixture.Station("Solo"), fixture.Commercials());
        CHECK(station->LoadAllPlaylists());
        CHECK(station->ShufflePlaylist(0));

        // Force a repeating draw (< 75) and check that the cycle index stays at 0.
        int repeatSeed = 0;
        for (;; ++repeatSeed) {
            std::srand(repeatSeed);
            if (std::rand() % 100 < static_cast<int>(kRZRadioMusicRepeatPercent)) break;
        }
        g_ticks = 1000;
        CHECK(station->OnIdle());            // starts the timer
        g_ticks = 5001;
        std::srand(repeatSeed);
        radio.played.clear();
        CHECK(station->OnIdle());
        CHECK_EQ(station->CycleIndex(), 0);
        CHECK(!station->CycleStarted());
        CHECK_EQ(radio.played.size(), std::size_t(1));

        // A draw of 75 or more advances the cycle order instead.
        int advanceSeed = 0;
        for (;; ++advanceSeed) {
            std::srand(advanceSeed);
            if (std::rand() % 100 >= static_cast<int>(kRZRadioMusicRepeatPercent)) break;
        }
        g_ticks = 6000;
        CHECK(station->OnIdle());
        g_ticks = 11000;
        std::srand(advanceSeed);
        CHECK(station->OnIdle());
        CHECK_EQ(station->CycleIndex(), 1);
        delete station;
    }

    // -------------------------------------------------------------------------------------------------------------
    // 10. ShuffleCycle: a permutation of {0,1,2,3}, and the previous last cycle never becomes the first (unless it
    //     was the music cycle, 0).
    // -------------------------------------------------------------------------------------------------------------
    {
        std::srand(0);
        auto* station = new cRZDiskRadioStation();
        std::vector<std::int32_t> previous = station->CycleOrder();
        for (int round = 0; round < 30; ++round) {
            const std::int32_t previousLast = previous.back();
            std::srand(round);
            std::vector<std::int32_t> expected = ReferenceShuffle(previous);
            if (previousLast != 0 && expected.front() == previousLast) {
                expected.front() = expected.back();
                expected.back() = previousLast;
            }
            std::srand(round);
            station->ShuffleCycle();
            CHECK_EQ(station->CycleOrder(), expected);
            std::vector<std::int32_t> sorted = station->CycleOrder();
            std::sort(sorted.begin(), sorted.end());
            CHECK(sorted == std::vector<std::int32_t>({0, 1, 2, 3}));
            if (previousLast != 0) CHECK(station->CycleOrder().front() != previousLast);
            previous = station->CycleOrder();
        }
        delete station;
    }

    // -------------------------------------------------------------------------------------------------------------
    // 11. SetPlaylistMode / SetShuffleStrategy are plain stores.
    // -------------------------------------------------------------------------------------------------------------
    {
        auto* station = new cRZDiskRadioStation();
        station->SetPlaylistMode(1);
        CHECK_EQ(station->PlaylistMode(), 1);
        station->SetShuffleStrategy(1);
        CHECK_EQ(station->ShuffleStrategy(), 1);
        // SetRadio caches the sound object of the radio (unused afterwards, but part of the layout).
        FakeRadio withSound;
        station->SetRadio(&withSound);
        CHECK_EQ(station->PlaylistMode(), 1);
        delete station;
    }

    if (failures == 0) std::puts("rz_diskradiostation ok");
    else std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}

// cSC4AudioEventHandler / cSC4AudioEventHandlerDisasterFire — unit test.
//
// Covers: the four COM views and their ids, AddRef/Release, the framework lookups of the constructor,
// Init/Shutdown (sound-system registration, the soundscape manager, the radio directory built from the file
// system's data directory, the old and new message-server subscriptions, the preference blob defaults and the
// radio restore), the region-music path (message 0x2a3ad653 Data1 1 -> cIGZRadio::SetStation("krgn", fade) and
// Data1 0 -> "kmay", the Data2 fade override, the already-current early-out), the radio option / playback
// messages, the re-posted 0x26d31ec4/0x26d31ec5 requests, the construction / automata / dispatch soundscape and
// sound-system paths, the disaster fire layer, and every quirk listed in the header.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <list>
#include <set>
#include <string>
#include <vector>

#include "gz/GZCOMDirector.h"
#include "gz/RZServices.h"
#include "gz/cIGZFileSystem.h"
#include "gz/cIGZFrameWork.h"
#include "rz/Radio.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZSysServPtr.h"
#include "sc4/ServiceLocator.h"
#include "sc4audio/AudioEventHandler.h"
#include "sc4audio/AudioScapeManager.h"
#include "sc4audio_fakes.h"

#include <SDL.h>   // scripts/tests/stubs/SDL.h: the test drives g_ticks by hand

namespace {

int g_failures = 0;

#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);          \
            ++g_failures;                                                                      \
        }                                                                                      \
    } while (false)

// ---------------------------------------------------------------------------------------------
// The radio (cIGZRadio) and its stations (cIGZRadioStation). Everything the handler can do is recorded.
// ---------------------------------------------------------------------------------------------
struct FakeStation final : cIGZRadioStation {
    explicit FakeStation(const char* name = "") : callSign(name ? name : "") {}
    std::string callSign;
    std::list<cRZString>* modifierList = nullptr;
    std::int32_t playlistMode = -1;
    int modifierCalls = 0, playlistCalls = 0;
    int refs = 1;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }

    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool OnIdle() override { return true; }
    void SetRadio(cIGZRadio*) override {}
    void SetStation() override {}
    void UnsetStation() override {}
    void GetStationCallSign(char* out) override { std::snprintf(out, 5, "%s", callSign.c_str()); }
    void GetStationDescription(cIGZString* out) override { if (out) out->AssignFromCString(callSign.c_str()); }
    bool IsOnTheAir() override { return true; }
    void SetPlaylistMode(std::int32_t mode) override { playlistMode = mode; ++playlistCalls; }
    void SetShuffleStrategy(std::int32_t) override {}
    void SetModifierList(std::list<cRZString>* modifiers) override { modifierList = modifiers; ++modifierCalls; }
    ~FakeStation() override = default;
    bool LoadAllPlaylists() override { return true; }
    bool ShufflePlaylist(std::int32_t) override { return true; }
    bool ShufflePlayList(const cRZString&, std::list<cRZString>&, std::int32_t) override { return true; }
    void ShuffleCycle() override {}
    bool IsCycleTypePresent(std::int32_t) override { return false; }
    bool MoveToNextPlaylistItem(std::int32_t) override { return false; }
    bool ReadCallSignFromDisk() override { return true; }
    bool StartPlayingNextSelectionOfGivenType(std::int32_t) override { return false; }
    bool PlayWelcome() override { return false; }
    bool ExpandPlayLists(cRZString&, std::list<cRZString>&) override { return false; }
    void PlayNextSelectionNotOnExclusionList() override {}
};

struct FakeRadio final : cIGZRadio {
    struct StationCall {
        std::string callSign;
        const FadeOptions* fade = nullptr;
        std::int32_t targetVolume = 0;
        std::int32_t durationMs = 0;
        bool fadeWasNull = true;
    };

    std::vector<StationCall> setStations;
    std::vector<std::uint32_t> currentStationQueries;   // GetCurrentStation(uint&) calls
    std::vector<std::uint32_t> stationIndexQueries;
    std::vector<std::string> stationCallSignQueries;
    std::vector<std::uint32_t> setStationIndices;
    std::vector<std::uint32_t> playNextSongs;
    std::vector<std::string> playedSongs;
    std::string radioDirectory;
    std::int32_t volume = kRZRadioMaxVolume;
    bool on = true;
    bool onDemand = true;
    std::uint32_t currentStation = 0;
    int turnOnCalls = 0, stopPlaybackCalls = 0, turnOnFalseCalls = 0, setVolumeCalls = 0;
    int refs = 1;

    // When non-null, GetStation(index) and GetStation(callSign) both answer it (for the early-out test).
    cIGZRadioStation* currentStationObject = nullptr;
    FakeStation krgn{"krgn"};
    FakeStation kmay{"kmay"};

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }

    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool OnIdle() override { return true; }
    cIGZSnd* GetRadioSound() override { return nullptr; }
    bool PlaySong(const cRZString& song) override { playedSongs.push_back(song.str()); return true; }
    void PlayNextSong(bool) override { ++playNextSongs.emplace_back(), void(); }
    bool GetCurrentStation(std::uint32_t& index) override {
        currentStationQueries.push_back(currentStation);
        index = currentStation;
        return true;
    }
    bool GetCurrentStation(char* callSign) override {
        if (callSign) std::snprintf(callSign, 5, "%s", currentStationObject ? "krgn" : "");
        return currentStationObject != nullptr;
    }
    cIGZRadioStation* GetStation(std::uint32_t index) override {
        stationIndexQueries.push_back(index);
        return currentStationObject;   // the "currently tuned" station, if the test set one
    }
    bool SetStation(std::uint32_t index) override { setStationIndices.push_back(index); return true; }
    cIGZRadioStation* GetStation(const char* callSign) override {
        stationCallSignQueries.push_back(callSign ? callSign : "");
        if (currentStationObject) return currentStationObject;
        const std::string name = callSign ? callSign : "";
        if (name == "krgn") return &krgn;
        if (name == "kmay") return &kmay;
        return nullptr;
    }
    bool SetStation(const char* callSign, const FadeOptions* fade) override {
        StationCall call;
        call.callSign = callSign ? callSign : "";
        call.fade = fade;
        call.fadeWasNull = fade == nullptr;
        if (fade) {
            call.targetVolume = fade->targetVolume;
            call.durationMs = fade->durationMs;
        }
        setStations.push_back(call);
        return true;
    }
    std::int32_t GetVolume() override { return volume; }
    bool SetVolume(std::int32_t value) override { volume = value; ++setVolumeCalls; return true; }
    std::uint32_t GetStationCount() override { return 2; }
    bool IsStationAvailable(const char*) override { return true; }
    bool AddStation(cIGZRadioStation*) override { return true; }
    bool RemoveStation(cIGZRadioStation*, bool) override { return true; }
    bool RemoveStation(const char*, bool) override { return true; }
    bool RemoveAllStations(bool) override { return false; }
    bool TurnOn(bool value) override {
        on = value;
        ++turnOnCalls;
        if (!value) ++turnOnFalseCalls;
        return true;
    }
    bool IsOn() override { return on; }
    void Mute(bool) override {}
    bool GetOption(RadioOption) override { return false; }
    void SetOption(bool, RadioOption) override {}
    bool GetRadioDirectory(cIGZString& out) override { out.AssignFromCString(radioDirectory.c_str()); return true; }
    void SetRadioDirectory(const cIGZString& directory) override { radioDirectory = directory.ToChar(); }
    void SetOnDemandState(bool value) override { onDemand = value; }
    void StopPlayback() override { ++stopPlaybackCalls; }
    bool GetCurrentSong(cRZString&) override { return false; }
    void SetCurrentSong(const cRZString&) override {}
    bool IsPlaying() override { return false; }
    void ReadSettings(RadioSettings*) override {}
    void WriteSettings(RadioSettings*) override {}
};

// ---------------------------------------------------------------------------------------------
// The soundscape manager (cISC4AudioScapeManager): records every instance / layer call.
// ---------------------------------------------------------------------------------------------
struct ScapeInstance {
    std::uint32_t layer = 0, instance = 0;
    float x = 0, y = 0, z = 0;
    float radius = 0;
};

struct FakeScape final : cISC4AudioScapeManager {
    struct LayerCall {
        std::string name;
        std::uint32_t layer = 0, flags = 0;
        float radius = 0;
    };
    struct SoundCall {
        std::uint32_t layer = 0, sound = 0;
        bool flag = false;
        int a4 = 0, a5 = 0, a6 = 0;
    };
    std::vector<LayerCall> layers;
    std::vector<SoundCall> sounds;
    std::vector<ScapeInstance> inserted, updated, removed;
    std::vector<std::pair<std::uint32_t, bool>> cleared;
    bool createLayerResult = true;
    int refs = 1;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }

    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool CreateLayer(const cIGZString& name, std::uint32_t layerId, std::uint32_t flags, float radius) override {
        layers.push_back(LayerCall{name.ToChar(), layerId, flags, radius});
        return createLayerResult;
    }
    bool DestroyLayer(std::uint32_t) override { return true; }
    bool ClearLayer(std::uint32_t) override { return true; }
    bool ClearAllLayers(std::uint32_t flagsMask, bool keepInstances) override {
        cleared.push_back({flagsMask, keepInstances});
        return true;
    }
    bool GetDistributedGroupPositionsAvgIntersected(std::uint32_t, std::uint32_t, SC4RectFloat&, cS3DVector3*,
                                                    int& count, int& total, bool) override {
        count = total = 0;
        return true;
    }
    bool GetDistributedGroupPositionsAvg(std::uint32_t, SC4RectFloat&, cS3DVector3*, int& count, int& total,
                                         bool) override {
        count = total = 0;
        return true;
    }
    bool GetGroupPositions(std::uint32_t, cS3DVector3*, int& count, int& total) override {
        count = total = 0;
        return true;
    }
    bool InsertInstance(std::uint32_t layerId, std::uint32_t instanceId, const cS3DVector3& position,
                        float radius) override {
        inserted.push_back(ScapeInstance{layerId, instanceId, position.x, position.y, position.z, radius});
        return true;
    }
    bool InsertInstance(std::uint32_t layerId, std::uint32_t soundId, bool soundFlag, std::uint32_t instanceId,
                        const cS3DVector3& position, float radius) override {
        inserted.push_back(ScapeInstance{layerId, instanceId, position.x, position.y, position.z, radius});
        (void)soundId;
        (void)soundFlag;
        return true;
    }
    bool RemoveInstance(std::uint32_t layerId, std::uint32_t instanceId) override {
        removed.push_back(ScapeInstance{layerId, instanceId});
        return true;
    }
    bool UpdateInstance(std::uint32_t layerId, std::uint32_t instanceId, const cS3DVector3& position,
                        float radius) override {
        updated.push_back(ScapeInstance{layerId, instanceId, position.x, position.y, position.z, radius});
        return true;
    }
    bool RegisterLayerSound(std::uint32_t layerId, std::uint32_t soundId, bool soundFlag, int a4, int a5,
                            int a6) override {
        sounds.push_back(SoundCall{layerId, soundId, soundFlag, a4, a5, a6});
        return true;
    }
    bool UnregisterLayerSound(std::uint32_t, std::uint32_t) override { return true; }
};

// ---------------------------------------------------------------------------------------------
// The sound system: records the posted event ids with their payloads and the created sound events.
// ---------------------------------------------------------------------------------------------
struct RecordingSndSys final : fakes::FakeSndSys {
    struct Ids {
        std::uint32_t id = 0, data1 = 0, data2 = 0, data3 = 0, data4 = 0;
    };
    struct Created {
        std::uint32_t eventId = 0, classId = 0, data1 = 0, data2 = 0, data3 = 0, data4 = 0;
    };
    std::vector<Ids> postedIds;
    std::vector<Ids> doEventIds;
    std::vector<Created> created;
    std::vector<std::uint32_t> postedEvents;

    bool DoEventIds(std::uint32_t id, std::uint32_t data1, std::uint32_t data2, std::uint32_t data3,
                    std::uint32_t data4) override {
        doEventIds.push_back(Ids{id, data1, data2, data3, data4});
        return true;
    }

    bool PostEventIds(std::uint32_t id, std::uint32_t data1, std::uint32_t data2, std::uint32_t data3,
                      std::uint32_t data4) override {
        postedIds.push_back(Ids{id, data1, data2, data3, data4});
        return true;
    }
    bool EventCreateWithData(std::uint32_t eventId, std::uint32_t classId, void** out, std::uint32_t data1,
                             std::uint32_t data2, std::uint32_t data3, std::uint32_t data4) override {
        created.push_back(Created{eventId, classId, data1, data2, data3, data4});
        return fakes::FakeSndSys::EventCreateWithData(eventId, classId, out, data1, data2, data3, data4);
    }
    bool PostEvent(cIGZSndEventStd* event) override {
        postedEvents.push_back(event->GetEventId());
        return true;
    }
    void Reset() {
        postedIds.clear();
        doEventIds.clear();
        created.clear();
        postedEvents.clear();
        handlers.clear();
        targets.clear();
    }
};

// ---------------------------------------------------------------------------------------------
// Message servers: the new one records notifications and posted messages' data1.
// ---------------------------------------------------------------------------------------------
struct RecordingMessageServer2 final : cIGZMessageServer2 {
    struct Notification {
        cIGZMessageTarget2* target = nullptr;
        std::uint32_t type = 0;
        bool added = false;
    };
    std::vector<Notification> notifications;
    struct Posted {
        std::uint32_t type = 0;
        std::uint64_t data1 = 0;
        bool hasData1 = false;
    };
    std::vector<Posted> posted;
    int refs = 1;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool MessageSend(cIGZMessage2*) override { return false; }
    bool MessagePost(cIGZMessage2* message, bool) override {
        Posted entry;
        entry.type = message->GetType();
        void* raw = nullptr;
        if (message->QueryInterface(kGZIID_cIGZMessage2Standard, &raw)) {
            auto* standard = static_cast<cIGZMessage2Standard*>(raw);
            entry.hasData1 = standard->GetHasData1();
            entry.data1 = standard->GetData1();
            standard->Release();
        }
        posted.push_back(entry);
        return true;
    }
    bool AddNotification(cIGZMessageTarget2* target, std::uint32_t type) override {
        notifications.push_back(Notification{target, type, true});
        return true;
    }
    bool RemoveNotification(cIGZMessageTarget2* target, std::uint32_t type) override {
        notifications.push_back(Notification{target, type, false});
        return true;
    }
    bool GeneralMessagePostToTarget(cIGZMessage2*, cIGZMessageTarget2*) override { return false; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return false; }
    bool OnTick(std::uint32_t) override { return false; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return 1; }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }

    int Count(std::uint32_t type, bool added) const {
        int n = 0;
        for (const Notification& notification : notifications)
            if (notification.type == type && notification.added == added) ++n;
        return n;
    }
};

struct RecordingMessageServer final : cIGZMessageServer {
    struct Notification {
        cIGZMessageTarget* target = nullptr;
        std::uint32_t type = 0;
        bool added = false;
    };
    std::vector<Notification> notifications;
    int refs = 1;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool MessageSend(const cGZMessage&) override { return false; }
    bool MessagePost(const cGZMessage&, bool) override { return false; }
    bool AddNotification(cIGZMessageTarget* target, std::uint32_t type) override {
        notifications.push_back(Notification{target, type, true});
        return true;
    }
    bool RemoveNotification(cIGZMessageTarget* target, std::uint32_t type) override {
        notifications.push_back(Notification{target, type, false});
        return true;
    }
    bool GeneralMessagePostToTarget(const cGZMessage&, cIGZMessageTarget*) override { return false; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget*) override { return false; }
    bool OnTick(std::uint32_t) override { return false; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
};


// The framework RZ::SS() / RZ::GS() ask for the sound system (the handler's cRZSysServPtr lookups use the resolver
// instead, exactly as the original's RZGetFramework() calls would).
struct FakeFramework final : cIGZFrameWork {
    RecordingSndSys* sndSys = nullptr;
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool GetSystemService(std::uint32_t serviceId, std::uint32_t iid, void** out) override {
        if (serviceId == kGZSID_cGZSndSys && iid == kGZIID_cIGZSndSys && sndSys) {
            sndSys->AddRef();
            *out = static_cast<cIGZSndSys*>(sndSys);
            return true;
        }
        *out = nullptr;
        return false;
    }
    bool AddSystemService(cIGZSystemService*) override { return true; }
    bool RemoveSystemService(cIGZSystemService*) override { return true; }
    bool EnumSystemServices(cIGZUnknownEnumerator&, cIGZUnknown*, std::uint32_t) override { return false; }
    bool AddHook(cIGZFrameWorkHooks*) override { return false; }
    bool RemoveHook(cIGZFrameWorkHooks*) override { return false; }
    bool AddToTick(cIGZSystemService*) override { return false; }
    bool RemoveFromTick(cIGZSystemService*) override { return false; }
    bool AddToOnIdle(cIGZSystemService*) override { return false; }
    bool RemoveFromOnIdle(cIGZSystemService*) override { return false; }
    int GetOnIdleInterval() const override { return 0; }
    void SetOnIdleInterval(int) override {}
    void OnTick(std::uint32_t) override {}
    void OnIdle() override {}
    bool IsTickEnabled() const override { return false; }
    void ToggleTick(bool) override {}
    void Quit(int) override {}
    void AbortiveQuit(int) override {}
    cIGZCmdLine* CommandLine() override { return nullptr; }
    bool IsInstall() const override { return false; }
    cIGZCOM* GetCOMObject() override { return nullptr; }
    int GetState() const override { return 0; }
    cIGZDebugStream* GetDebugStream() override { return nullptr; }
    cIGZDebugStream* DefaultDebugStream() override { return nullptr; }
    cIGZDebugStream* DebugStream() override { return nullptr; }
    void SetDebugStream(cIGZDebugStream*) override {}
    void SetDebugLevel(int) override {}
    int GetDebugLevel() const override { return 0; }
    cIGZOStream* StdOut() override { return nullptr; }
    cIGZOStream* StdErr() override { return nullptr; }
    cIGZIStream* StdIn() override { return nullptr; }
    bool GetStream(int, std::uint32_t, void**) override { return false; }
    bool SetStream(int, cIGZUnknown*) override { return false; }
    void SetApplication(cIGZApp*) override {}
    cIGZApp* Application() const override { return nullptr; }
    void ReportException(const char*) override {}
    cIGZUnknown* ExceptionNotificationObj() const override { return nullptr; }
};

struct FakeFileSystem final : cIGZFileSystem {
    std::string dataDirectory = "/game/GameData/";
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    const char* AppDrive() override { return "/"; }
    const char* AppDirectory() override { return "/game/"; }
    const char* AppName() override { return "SimCity 4"; }
    const char* AppExtension() override { return ".app"; }
    const char* DataDirectory() override { return dataDirectory.c_str(); }
    const char* PlugInDirectory() override { return "/game/Plugins/"; }
    const char* CDAppDrive() override { return "/"; }
    const char* CDAppDirectory() override { return "/game/"; }
    const char* CDDataDirectory() override { return "/game/GameData/"; }
    const char* CDPlugInDirectory() override { return "/game/Plugins/"; }
    bool FindCDDrive() override { return false; }
    void UseCDIniFile(bool) override {}
    void AllowRecursiveCDSearch(bool) override {}
    void SetExpectedCDName(const cIGZString&) override {}
    void SetExpectedCDAppPath(const cIGZString&) override {}
    void SetDataDirectory(const cIGZString&) override {}
    void SetPluginDirectory(const cIGZString&) override {}
    void GetPluginDirectoryName(cIGZString& out) override { out.AssignFromCString("Plugins"); }
    void SetPluginDirectoryName(const cIGZString&) override {}
};

// ---------------------------------------------------------------------------------------------
// The game objects the sound paths key on.
// ---------------------------------------------------------------------------------------------
struct FakeOccupant : sc4::cISC4Occupant {
    std::uint32_t type = 0;
    float position[3] = {1.0f, 2.0f, 3.0f};
    bool fire = false;
    int refs = 1;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 1 || iid == sc4audio::kGZIID_cISC4OccupantAudio) {
            *out = static_cast<sc4::cISC4Occupant*>(this);
            ++refs;
            return true;
        }
        if (iid == sc4audio::kGZIID_cISC4FireOccupantAudio && fire) {
            *out = static_cast<sc4::cISC4Occupant*>(this);
            ++refs;
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool IsInitialized() const override { return true; }
    sc4::cISCPropertyHolder* AsPropertyHolder() override { return nullptr; }
    std::uint32_t GetType() const override { return type; }
    void GetPosition(float* xyz) override { xyz[0] = position[0]; xyz[1] = position[1]; xyz[2] = position[2]; }
    void SetPosition(const float* xyz) override {
        position[0] = xyz[0];
        position[1] = xyz[1];
        position[2] = xyz[2];
    }
    void GetBoundingBox(float* minXyz, float* maxXyz) override {
        for (int i = 0; i < 3; ++i) minXyz[i] = maxXyz[i] = position[i];
    }
    void GetBoundingCityCells(sc4::SC4CellRect& rect) override { rect = {}; }
    void SetRemovalFlags(int) override {}
    void UnsetRemovalFlags(int) override {}
    bool CanRemove(int) override { return true; }
    void PostOccupantMessage(std::uint32_t, std::uint32_t) override {}
    std::uint32_t GetHighlight() const override { return 0; }
    void SetHighlight(int, bool) override {}
    void SetVisibility(bool, bool) override {}
    sc4::cISC43DPlaceableObject* GetPlaceableObject() const override { return nullptr; }
    sc4::cISC43DPlaceableObject* GetOrCreatePlaceableObject() override { return nullptr; }
    void SetPlaceableObject(sc4::cISC43DPlaceableObject*) override {}
    bool IsOccupantGroup(std::uint32_t) const override { return false; }
    void AddOccupantGroup(std::uint32_t) override {}
    bool GetOccupantGroups(std::set<std::uint32_t>&) const override { return false; }
    bool GetOccupantManagerBBox(std::uint8_t*) const override { return false; }
    void SetOccupantManagerBBox(const std::uint8_t*) override {}
    bool GetLotTag(std::uint32_t& tag) const override { tag = 0; return false; }
    void SetLotTag(std::uint32_t) override {}
    void SetFlag(std::uint32_t) override {}
    void SetAllFlags(std::uint32_t) override {}
    void ClearFlag(std::uint32_t) override {}
    bool IsFlagSet(std::uint32_t) override { return false; }
    std::uint32_t GetFlags() override { return 0; }
};

struct FakeAutomaton final : sc4audio::cISC4AutomatonAudio {
    float position[3] = {10.0f, 20.0f, 30.0f};
    int refs = 1;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 1 || iid == sc4audio::kGZIID_cISC4AutomatonAudio) {
            *out = static_cast<sc4audio::cISC4AutomatonAudio*>(this);
            ++refs;
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    sc4::cISC4Occupant* AsOccupant() const override { return nullptr; }
    sc4::cISC4Occupant* AsOccupant() override { return nullptr; }
    void GetPosition(float* xyz) override {
        xyz[0] = position[0];
        xyz[1] = position[1];
        xyz[2] = position[2];
    }
};

// The payload object the network-occupant seam is asked about: it answers iid 0xa821ef94 and hands the host a
// position. The host's NetworkOccupantPosition models cISC4NetworkOccupant vslot 0x180 -> occupant [8].
struct FakeNetworkOccupant final : FakeOccupant {
    bool isNetworkOccupant = true;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == sc4audio::kGZIID_cISC4NetworkOccupantAudio && isNetworkOccupant) {
            *out = static_cast<sc4::cISC4Occupant*>(this);
            ++refs;
            return true;
        }
        return FakeOccupant::QueryInterface(iid, out);
    }
};

struct FakePylon final : cIGZUnknown {
    int refs = 1;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 0xc9b6d7d6) {
            *out = this;
            ++refs;
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
};


// A heap-allocated cRZMessage2Standard: the handler takes and drops references through QueryInterface, and a COM
// object must never be released to zero on the stack.
struct TestMessage {
    TestMessage() {
        object = new cRZMessage2Standard();
        object->AddRef();
    }
    ~TestMessage() { object->Release(); }
    // A fresh message of count 1 (the payload flags do not leak between sub-tests).
    void Reset() {
        object->Release();
        object = new cRZMessage2Standard();
        object->AddRef();
    }
    TestMessage(const TestMessage&) = delete;
    TestMessage& operator=(const TestMessage&) = delete;
    cRZMessage2Standard* operator->() const { return object; }
    operator cRZMessage2Standard*() const { return object; }
    cRZMessage2Standard* object = nullptr;
};

// ---------------------------------------------------------------------------------------------
// The test host.
// ---------------------------------------------------------------------------------------------
struct TestHost final : sc4audio::IAudioEventHandlerHost {
    std::uint8_t preferences[0x1734] = {};
    std::uint32_t fadeMs = 4000;
    int staticInitCalls = 0, defaultMusicCalls = 0;
    std::vector<std::uint32_t> exclusionModes;
    std::vector<std::uint32_t> musicSliderCalls, effectsSliderCalls;
    bool cityEstablished = false;
    cIGZMessageServer2* mouseServer = nullptr;
    FakeNetworkOccupant* networkOccupant = nullptr;
    std::vector<FakeOccupant*> fireOccupants;
    int iterateCalls = 0;
    std::uint32_t musicVolumeFromSlider = 90;

    std::uint8_t* PreferencesBlob() override { return preferences; }
    void PreferencesStaticInit() override { ++staticInitCalls; }
    std::uint32_t FadeOutTimeMs() override { return fadeMs; }
    std::uint32_t VolumeFromSliderValue(std::uint32_t sliderValue, AudioType type) override {
        if (type == kAudioTypeMusic) {
            musicSliderCalls.push_back(sliderValue);
            return musicVolumeFromSlider;
        }
        effectsSliderCalls.push_back(sliderValue);
        return sliderValue;
    }
    void SetDefaultMusicListPreferences() override { ++defaultMusicCalls; }
    void UpdateExclusionList(AudioMode mode) override {
        exclusionModes.push_back(mode == kAudioModeRegion ? 1u : 0u);
    }
    cIGZMessageServer2* MouseMessageServer() override { return mouseServer; }
    bool CityEstablished() override { return cityEstablished; }
    bool NetworkOccupantPosition(cIGZUnknown* occupant, float* xyz) override {
        if (!networkOccupant || occupant != static_cast<cIGZUnknown*>(networkOccupant)) return false;
        float position[3] = {1.0f, 2.0f, 3.0f};
        std::memcpy(xyz, position, sizeof position);
        return true;
    }
    bool IterateOccupants(OccupantCallback callback, void* context, sc4::cISC4OccupantFilter* filter) override {
        ++iterateCalls;
        for (FakeOccupant* occupant : fireOccupants) {
            if (filter && !filter->IsOccupantIncluded(occupant)) continue;
            if (!callback(occupant, context)) return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------------------------------------
// The resolver that stands in for RZGetFramework()->GetSystemService.
// ---------------------------------------------------------------------------------------------
struct Services {
    FakeRadio* radio = nullptr;
    RecordingSndSys* sndSys = nullptr;
    FakeScape* scape = nullptr;
    FakeFileSystem* fileSystem = nullptr;
    RecordingMessageServer2* messageServer2 = nullptr;
    int radioLookups = 0, sndSysLookups = 0, scapeLookups = 0, fileSystemLookups = 0, messageServerLookups = 0;
};

Services g_services;

bool ResolveService(std::uint32_t serviceId, std::uint32_t iid, void** out) {
    *out = nullptr;
    if (serviceId == kGZSID_cRZRadio && iid == kGZIID_cIGZRadio) {
        ++g_services.radioLookups;
        if (!g_services.radio) return false;
        g_services.radio->AddRef();
        *out = static_cast<cIGZRadio*>(g_services.radio);
        return true;
    }
    if (serviceId == kGZSID_cGZSndSys && iid == kGZIID_cIGZSndSys) {
        ++g_services.sndSysLookups;
        if (!g_services.sndSys) return false;
        g_services.sndSys->AddRef();
        *out = static_cast<cIGZSndSys*>(g_services.sndSys);
        return true;
    }
    if (serviceId == kGZSID_cISC4AudioScapeManager && iid == kGZIID_cISC4AudioScapeManager) {
        ++g_services.scapeLookups;
        if (!g_services.scape) return false;
        g_services.scape->AddRef();
        *out = static_cast<cISC4AudioScapeManager*>(g_services.scape);
        return true;
    }
    if (serviceId == kGZSID_cGZFileSystem && iid == kGZIID_cIGZFileSystem) {
        ++g_services.fileSystemLookups;
        if (!g_services.fileSystem) return false;
        g_services.fileSystem->AddRef();
        *out = static_cast<cIGZFileSystem*>(g_services.fileSystem);
        return true;
    }
    if (serviceId == kGZSID_MessageServer2 && iid == kGZIID_cIGZMessageServer2) {
        ++g_services.messageServerLookups;
        if (!g_services.messageServer2) return false;
        g_services.messageServer2->AddRef();
        *out = static_cast<cIGZMessageServer2*>(g_services.messageServer2);
        return true;
    }
    return false;
}

// A cRZMessage2Standard with a type and payloads.
void SetStandardMessage(cRZMessage2Standard* message, std::uint32_t type, std::uint64_t data1 = 0,
                        bool hasData1 = false) {
    message->SetType(type);
    if (hasData1) message->SetData1(data1);
}

// ---------------------------------------------------------------------------------------------
// The tests.
// ---------------------------------------------------------------------------------------------
void TestConstructorAndInterfaceIds() {
    FakeRadio radio;
    RecordingSndSys sndSys;
    FakeScape scape;
    FakeFileSystem fileSystem;
    RecordingMessageServer2 server;
    Services saved = g_services;
    g_services = Services{&radio, &sndSys, &scape, &fileSystem, &server};
    SetRZServiceResolver(&ResolveService);

    {
        cSC4AudioEventHandler handler;
        // The constructor looks the radio and the sound system up (once each) and starts at count 0.
        CHECK(g_services.radioLookups == 1);
        CHECK(g_services.sndSysLookups == 1);
        CHECK(handler.refCount() == 0);
        CHECK(handler.radio() == &radio);
        CHECK(handler.soundSystem() == &sndSys);
        CHECK(!handler.IsInitialized());
        CHECK(handler.modalWindowCount() == 0);

        // Interface ids: 1 / 0xbb523bb2 primary, 0x452294aa cIGZMessageTarget2, 0x89e51472 cIGZInitShutdown.
        void* out = nullptr;
        CHECK(handler.QueryInterface(1, &out) && out == static_cast<cIGZSndEventHandler*>(&handler));
        CHECK(handler.QueryInterface(0xbb523bb2, &out));
        CHECK(handler.QueryInterface(sc4::kGZIID_cIGZMessageTarget2, &out) &&
              out == static_cast<cIGZMessageTarget2*>(&handler));
        CHECK(handler.QueryInterface(kGZIID_cIGZInitShutdown, &out) &&
              out == static_cast<cIGZInitShutdown*>(&handler));
        CHECK(!handler.QueryInterface(0x12345678, &out));   // the +0x08 cIGZMessageTarget view is never answered
        CHECK(handler.refCount() == 4);                     // every accepted QueryInterface AddRef'd once
        // Release destroys at a count of 1; bring the count down first.
        while (handler.refCount() > 1) handler.Release();
    }

    // Create() heap-allocates (the test deletes it through Release).
    cSC4AudioEventHandler* created = cSC4AudioEventHandler::Create();
    CHECK(created != nullptr);
    CHECK(created->radio() == &radio);
    CHECK(created->Release() == 0);

    g_services = saved;
    SetRZServiceResolver(nullptr);
}

void TestInitAndShutdown() {
    FakeRadio radio;
    RecordingSndSys sndSys;
    FakeScape scape;
    FakeFileSystem fileSystem;
    RecordingMessageServer2 server;
    RecordingMessageServer oldServer;
    TestHost host;
    Services saved = g_services;
    g_services = Services{&radio, &sndSys, &scape, &fileSystem, &server};
    SetRZServiceResolver(&ResolveService);
    sc4audio::SetAudioEventHandlerHost(&host);
    SL::spMS = &oldServer;
    SL::spMS2 = &server;
    SL::spPreferences = host.preferences;

    // The default preference blob has the "apply music defaults" flag set.
    host.preferences[sc4audio::kPrefsAudioResetFlag] = 1;
    host.preferences[sc4audio::kPrefsMusicEnabled] = 1;

    {
        cSC4AudioEventHandler handler;
        CHECK(handler.Init());
        CHECK(handler.IsInitialized());
        CHECK(handler.Init());               // second call is a no-op
        // The stack object is never Release()d to zero: Release at a count of 1 destroys (see the Create test).
        CHECK(radio.radioDirectory == "/game/GameData/Radio/");
        CHECK(sndSys.handlers.size() == 1);
        CHECK(sndSys.handlers[0] == static_cast<cIGZSndEventHandler*>(&handler));
        CHECK(handler.scapeManager() == &scape);
        CHECK(scape.refs == 2);              // one reference held by the handler
        // Old message server: the two modal-window types.
        CHECK(oldServer.notifications.size() == 2);
        CHECK(host.staticInitCalls == 1);
        CHECK(host.musicSliderCalls.size() == 1 && host.musicSliderCalls[0] == 0x2d0);
        CHECK(host.effectsSliderCalls.size() == 1 && host.effectsSliderCalls[0] == 0x400);
        CHECK(host.defaultMusicCalls == 1);
        CHECK(host.exclusionModes.size() == 2 && host.exclusionModes[0] == 0 && host.exclusionModes[1] == 1);
        CHECK(host.preferences[sc4audio::kPrefsAudioResetFlag] == 0);
        CHECK((host.preferences[sc4audio::kPrefsAudioFlags] & 1) != 0);
        CHECK(host.preferences[sc4audio::kPrefsMusicVolume + 0] == 90);
        CHECK(host.preferences[sc4audio::kPrefsEffectsVolume + 0] == 0 &&
              host.preferences[sc4audio::kPrefsEffectsVolume + 1] == 4);   // 0x400 little-endian
        // 13 notifications registered with the new server.
        int added = 0;
        for (const auto& notification : server.notifications)
            if (notification.added) ++added;
        CHECK(added == 13);
        CHECK(server.Count(0x2a3ad653, true) == 1);
        CHECK(server.Count(0xa611b34, true) == 1);
        CHECK(server.Count(0x4348b111, true) == 1);
        // The radio was turned off and silenced for the directory change, then restored from the blob.
        CHECK(radio.turnOnFalseCalls >= 1);
        CHECK(radio.turnOnCalls == 2 && radio.on);
        CHECK(radio.setVolumeCalls == 2 && radio.volume == 90);
        CHECK(radio.onDemand);

        // Shutdown removes the same 13 notifications and unregisters the event handler.
        CHECK(handler.Shutdown());
        CHECK(!handler.IsInitialized());
        CHECK(sndSys.handlers.empty());
        CHECK(handler.scapeManager() == nullptr);
        CHECK(scape.refs == 1);
        CHECK(radio.turnOnFalseCalls == 2);
        int removed = 0;
        for (const auto& notification : server.notifications)
            if (!notification.added) ++removed;
        CHECK(removed == 13);
        CHECK(oldServer.notifications.size() == 4);
        // Shutdown after Shutdown is a no-op that reports success.
        CHECK(handler.Shutdown());
    }

    sc4audio::SetAudioEventHandlerHost(nullptr);
    SL::spMS = nullptr;
    SL::spMS2 = nullptr;
    SL::spPreferences = nullptr;
    g_services = saved;
    SetRZServiceResolver(nullptr);
}

void TestRegionMusic() {
    FakeRadio radio;
    RecordingSndSys sndSys;
    FakeScape scape;
    FakeFileSystem fileSystem;
    RecordingMessageServer2 server;
    TestHost host;
    Services saved = g_services;
    g_services = Services{&radio, &sndSys, &scape, &fileSystem, &server};
    SetRZServiceResolver(&ResolveService);
    sc4audio::SetAudioEventHandlerHost(&host);
    SL::spMS2 = &server;
    SL::spPreferences = host.preferences;
    host.preferences[sc4audio::kPrefsAudioResetFlag] = 1;

    cSC4AudioEventHandler handler;
    handler.Init();
    radio.setStations.clear();
    radio.onDemand = true;

    // Data1 = 1 -> the region station "krgn".
    TestMessage message;
    message.Reset();
    SetStandardMessage(message, 0x2a3ad653, 1, true);
    CHECK(handler.DoMessage(message));
    CHECK(radio.setStations.size() == 1);
    CHECK(radio.setStations[0].callSign == "krgn");
    CHECK(!radio.setStations[0].fadeWasNull);
    CHECK(radio.setStations[0].targetVolume == 0);
    CHECK(radio.setStations[0].durationMs == 4000);
    CHECK(!radio.onDemand);   // SetOnDemandState(false) before the switch
    CHECK(radio.currentStationQueries.size() == 1);

    // Data1 = 0 -> the mayor station "kmay".
    radio.setStations.clear();
    message.Reset();
    SetStandardMessage(message, 0x2a3ad653, 0, true);
    CHECK(handler.DoMessage(message));
    CHECK(radio.setStations.size() == 1);
    CHECK(radio.setStations[0].callSign == "kmay");
    CHECK(radio.setStations[0].durationMs == 4000);

    // Data2 overrides the fade duration.
    radio.setStations.clear();
    message.Reset();
    SetStandardMessage(message, 0x2a3ad653, 1, true);
    message->SetData2(250);
    CHECK(handler.DoMessage(message));
    CHECK(radio.setStations.size() == 1);
    CHECK(radio.setStations[0].callSign == "krgn");
    CHECK(radio.setStations[0].durationMs == 250);
    CHECK(radio.setStations[0].targetVolume == 0);

    // The host can override kFadeOutTimeMs.
    host.fadeMs = 1234;
    radio.setStations.clear();
    message.Reset();
    SetStandardMessage(message, 0x2a3ad653, 1, true);
    CHECK(handler.DoMessage(message));
    CHECK(radio.setStations.size() == 1 && radio.setStations[0].durationMs == 1234);
    host.fadeMs = 4000;

    // Already current: GetStation(index) and GetStation("krgn") answer the same station -> no SetStation.
    radio.setStations.clear();
    radio.currentStationObject = &radio.krgn;
    CHECK(handler.DoMessage(message));
    CHECK(radio.setStations.empty());
    radio.currentStationObject = nullptr;

    // A mode other than 0 / 1 consumes the message without a station switch but still reports success.
    radio.setStations.clear();
    message.Reset();
    SetStandardMessage(message, 0x2a3ad653, 7, true);
    CHECK(handler.DoMessage(message));
    CHECK(radio.setStations.empty());

    // A message of another type is ignored.
    message.Reset();
    SetStandardMessage(message, 0x12345678, 1, true);
    CHECK(handler.DoMessage(message));

    // Station playlist options (0xa611b34): Data1 selects the station, Data2 the exclusion list, Data3 the mode.
    std::list<cRZString> modifiers;
    TestMessage options;
    options->SetType(0xa611b34);
    options->SetData1(1);
    options->SetVoid2(&modifiers);
    CHECK(handler.DoMessage(options));
    CHECK(radio.krgn.modifierCalls == 1 && radio.krgn.modifierList == &modifiers);
    CHECK(radio.krgn.playlistMode == 2);   // no Data3 -> 2
    options->SetData3(0);
    CHECK(handler.DoMessage(options));
    CHECK(radio.krgn.playlistMode == 2);   // Data3 == 0 -> 2
    options->SetData3(5);
    CHECK(handler.DoMessage(options));
    CHECK(radio.krgn.playlistMode == 1);   // Data3 != 0 -> 1
    options->SetData1(0);
    CHECK(handler.DoMessage(options));
    CHECK(radio.kmay.modifierCalls == 1);

    // One-shot radio commands.
    message.Reset();
    SetStandardMessage(message, 0x2a611b51);
    CHECK(handler.DoMessage(message));
    CHECK(radio.stopPlaybackCalls == 1);

    message.Reset();
    SetStandardMessage(message, 0xaa650861, 1, true);
    CHECK(handler.DoMessage(message));
    CHECK(radio.onDemand);
    message.Reset();
    SetStandardMessage(message, 0xaa650861, 0, true);
    CHECK(handler.DoMessage(message));
    CHECK(!radio.onDemand);

    // Radio on/off + volume (0xaa650860): Data2 sets the volume, Data1 turns the radio on and advances the song.
    radio.on = false;
    radio.volume = 0;
    radio.playNextSongs.clear();
    message.Reset();
    SetStandardMessage(message, 0xaa650860);
    message->SetData1(1);
    message->SetData2(300);
    CHECK(handler.DoMessage(message));
    CHECK(radio.volume == 300);
    CHECK(radio.on);
    CHECK(radio.playNextSongs.size() == 1);
    // Asking for the state it is already in does nothing.
    radio.setVolumeCalls = 0;
    radio.playNextSongs.clear();
    CHECK(handler.DoMessage(message));
    CHECK(radio.playNextSongs.empty());
    CHECK(radio.setVolumeCalls == 1);   // the volume is still written (the on/off early-out comes after it)
    radio.on = true;
    message.Reset();
    SetStandardMessage(message, 0xaa650860, 0, true);
    CHECK(handler.DoMessage(message));
    CHECK(!radio.on);

    // Play song (0xaa611b6e): Data1 is a cRZString* the handler releases.
    radio.playedSongs.clear();
    auto* song = new cRZString("GameData/Radio/Stations/Region/Music/theme.mp3");
    message->SetType(0xaa611b6e);
    message->SetVoid1(song);
    CHECK(handler.DoMessage(message));
    CHECK(radio.playedSongs.size() == 1);
    CHECK(radio.playedSongs[0] == "GameData/Radio/Stations/Region/Music/theme.mp3");

    // The focus message turns the radio back on only when music is enabled in the preferences.
    host.preferences[sc4audio::kPrefsMusicEnabled] = 1;
    radio.on = false;
    message.Reset();
    SetStandardMessage(message, 0x4348b111, 1, true);
    CHECK(handler.DoMessage(message));
    CHECK(radio.on);
    host.preferences[sc4audio::kPrefsMusicEnabled] = 0;
    radio.on = true;
    message.Reset();
    SetStandardMessage(message, 0x4348b111, 0, true);
    CHECK(handler.DoMessage(message));
    CHECK(radio.on);   // music disabled: the message is ignored

    // The re-posted requests (0x26d31ec5 -> region, 0x26d31ec4 -> mayor) go through SL::spMS2.
    server.posted.clear();
    message.Reset();
    SetStandardMessage(message, 0x26d31ec5);
    CHECK(handler.DoMessage(message));
    CHECK(server.posted.size() == 1);
    CHECK(server.posted[0].type == 0x2a3ad653);
    CHECK(server.posted[0].hasData1 && server.posted[0].data1 == 1);
    message.Reset();
    SetStandardMessage(message, 0x26d31ec4);
    CHECK(handler.DoMessage(message));
    CHECK(server.posted.size() == 2);
    CHECK(server.posted[1].hasData1 && server.posted[1].data1 == 0);

    // The sound-system cache RZ::SS() and RZ::GS() answer from (installed through the process-wide framework).
    FakeFramework framework;
    framework.sndSys = &sndSys;
    SetRZFramework(&framework);
    RZ::ResetCaches();

    // Listener property message (0x4a7fb7e9) forwards Data1 to the sound system as event 0xbaab7a4.
    sndSys.postedIds.clear();
    message.Reset();
    SetStandardMessage(message, 0x4a7fb7e9, 0x1234, true);
    CHECK(handler.DoMessage(message));
    CHECK(sndSys.doEventIds.size() == 1);
    if (sndSys.doEventIds.size() == 1) {
        CHECK(sndSys.doEventIds[0].id == 0xbaab7a4 && sndSys.doEventIds[0].data1 == 0x1234);
    }

    // DoEvent: the tuning-changed id enumerates the update targets and the torch events post their loops.
    fakes::FakeEvent event;
    event.id = 0xaa36d9b6;
    CHECK(!handler.DoEvent(&event));
    sndSys.postedIds.clear();
    event.id = 0x8a10611a;   // torch stop: three stop events on keys 0x601..0x603
    event.message.SetData1(1);
    event.message.SetData2(2);
    event.message.SetData3(3);
    CHECK(!handler.DoEvent(&event));
    CHECK(sndSys.postedIds.size() == 3);
    if (sndSys.postedIds.size() == 3) {
        CHECK(sndSys.postedIds[0].id == 0x6a451908 && sndSys.postedIds[0].data1 == 0x601);
        CHECK(sndSys.postedIds[1].id == 0x6a451908 && sndSys.postedIds[1].data1 == 0x602);
        CHECK(sndSys.postedIds[2].id == 0x6a451908 && sndSys.postedIds[2].data1 == 0x603);
    }
    // An unknown event id is consumed without an action.
    sndSys.postedIds.clear();
    event.id = 0x12345678;
    CHECK(!handler.DoEvent(&event));
    CHECK(sndSys.postedIds.empty());

    handler.Shutdown();
    SetRZFramework(nullptr);
    RZ::ResetCaches();
    sc4audio::SetAudioEventHandlerHost(nullptr);
    SL::spMS2 = nullptr;
    SL::spPreferences = nullptr;
    g_services = saved;
    SetRZServiceResolver(nullptr);
}

void TestCityMessages() {
    FakeRadio radio;
    RecordingSndSys sndSys;
    FakeScape scape;
    FakeFileSystem fileSystem;
    RecordingMessageServer2 server;
    TestHost host;
    Services saved = g_services;
    g_services = Services{&radio, &sndSys, &scape, &fileSystem, &server};
    SetRZServiceResolver(&ResolveService);
    sc4audio::SetAudioEventHandlerHost(&host);
    SL::spMS2 = &server;
    SL::spPreferences = host.preferences;
    host.preferences[sc4audio::kPrefsAudioResetFlag] = 1;

    cSC4AudioEventHandler handler;
    handler.Init();
    server.notifications.clear();
    server.posted.clear();

    // City init: the 11 automata messages are added, the viewport is copied, and an established city asks for the
    // mayor music through the message server.
    host.cityEstablished = true;
    TestMessage message;
    message.Reset();
    SetStandardMessage(message, 0x26d31ec1);
    CHECK(handler.DoMessage(message));
    int added = 0;
    for (const auto& notification : server.notifications)
        if (notification.added) ++added;
    CHECK(added == 11);
    CHECK(server.Count(0xe94671ed, true) == 1);   // registered although DoMessage never dispatches it
    CHECK(server.posted.size() == 1);
    CHECK(server.posted[0].type == 0x2a3ad653 && server.posted[0].data1 == 0);

    // City shutdown: the layers are cleared and the messages removed.
    server.posted.clear();
    message.Reset();
    SetStandardMessage(message, 0x26d31ec2);
    CHECK(handler.DoMessage(message));
    CHECK(scape.cleared.size() == 1);
    CHECK(scape.cleared[0].first == 0 && !scape.cleared[0].second);
    int removed = 0;
    for (const auto& notification : server.notifications)
        if (!notification.added) ++removed;
    CHECK(removed == 11);

    handler.Shutdown();
    sc4audio::SetAudioEventHandlerHost(nullptr);
    SL::spMS2 = nullptr;
    SL::spPreferences = nullptr;
    g_services = saved;
    SetRZServiceResolver(nullptr);
}

void TestConstructionAndAutomataSounds() {
    FakeRadio radio;
    RecordingSndSys sndSys;
    FakeScape scape;
    FakeFileSystem fileSystem;
    RecordingMessageServer2 server;
    TestHost host;
    Services saved = g_services;
    g_services = Services{&radio, &sndSys, &scape, &fileSystem, &server};
    SetRZServiceResolver(&ResolveService);
    sc4audio::SetAudioEventHandlerHost(&host);
    SL::spMS2 = &server;
    SL::spPreferences = host.preferences;
    host.preferences[sc4audio::kPrefsAudioResetFlag] = 1;

    cSC4AudioEventHandler handler;
    handler.Init();

    FakeNetworkOccupant networkOccupant;
    FakeOccupant occupant;
    occupant.position[0] = 4.0f;
    occupant.position[1] = 5.0f;
    occupant.position[2] = 6.0f;
    host.networkOccupant = &networkOccupant;

    // 0x89f10866 without Data2 -> RemoveInstance(layer, Data1). Data3 bit 1 picks the layer.
    TestMessage message;
    message->SetType(0x89f10866);
    message->SetData1(7);
    message->SetData3(0);
    CHECK(handler.DoMessage(message));
    CHECK(scape.removed.size() == 1);
    CHECK(scape.removed[0].layer == 0xaa62c82a && scape.removed[0].instance == 7);
    scape.removed.clear();
    message->SetData3(2);
    CHECK(handler.DoMessage(message));
    CHECK(scape.removed.size() == 1 && scape.removed[0].layer == 0xea62c800);

    // 0x89f10866 with Data2 and a network occupant -> InsertInstance at the occupant's position.
    scape.inserted.clear();
    message->SetVoid2(&networkOccupant);
    message->SetIGZUnknown(&networkOccupant);
    message->SetData3(0);
    CHECK(handler.DoMessage(message));
    CHECK(scape.inserted.size() == 1);
    CHECK(scape.inserted[0].layer == 0xaa62c82a && scape.inserted[0].instance == 7);
    CHECK(scape.inserted[0].x == 1.0f && scape.inserted[0].y == 2.0f && scape.inserted[0].z == 3.0f);
    CHECK(scape.inserted[0].radius == 0.0f);
    // Data2 present but null -> the original removes instead of inserting.
    scape.inserted.clear();
    scape.removed.clear();
    message->SetVoid2(nullptr);
    CHECK(handler.DoMessage(message));
    CHECK(scape.inserted.empty() && scape.removed.size() == 1);

    // 0xa9f10e45 -> UpdateInstance, layer from Data3 bit 1.
    scape.updated.clear();
    TestMessage update;
    update->SetType(0xa9f10e45);
    update->SetData1(9);
    update->SetData3(2);
    update->SetIGZUnknown(&networkOccupant);
    CHECK(handler.DoMessage(update));
    CHECK(scape.updated.size() == 1);
    CHECK(scape.updated[0].layer == 0xea62c800 && scape.updated[0].instance == 9);

    // 0x49e7b1e2 -> InsertInstance keyed by the occupant pointer, in the crew layer.
    scape.inserted.clear();
    TestMessage occupantMessage;
    occupantMessage->SetType(0x49e7b1e2);
    occupantMessage->SetIGZUnknown(&occupant);
    CHECK(handler.DoMessage(occupantMessage));
    CHECK(scape.inserted.size() == 1);
    CHECK(scape.inserted[0].layer == 0xc9dc0a61);
    CHECK(scape.inserted[0].x == 4.0f && scape.inserted[0].y == 5.0f && scape.inserted[0].z == 6.0f);

    // 0xc9e7b1e8: Data1 == 0 removes, otherwise inserts (occupancy layer).
    scape.inserted.clear();
    scape.removed.clear();
    occupantMessage->SetType(0xc9e7b1e8);
    occupantMessage->SetData1(0);
    CHECK(handler.DoMessage(occupantMessage));
    CHECK(scape.removed.size() == 1 && scape.removed[0].layer == 0x49e7b4b0);
    occupantMessage->SetData1(1);
    CHECK(handler.DoMessage(occupantMessage));
    CHECK(scape.inserted.size() == 1 && scape.inserted[0].layer == 0x49e7b4b0);

    // 0xa9e3b6e3: a positive Data1 inserts into the "enter" layer at the occupant's position.
    scape.inserted.clear();
    TestMessage instanceMessage;
    instanceMessage->SetType(0xa9e3b6e3);
    instanceMessage->SetData1(1);
    instanceMessage->SetData2(1);
    instanceMessage->SetIGZUnknown(&occupant);
    CHECK(handler.DoMessage(instanceMessage));
    CHECK(scape.inserted.size() == 1);
    CHECK(scape.inserted[0].layer == 0x49de8a94);
    CHECK(scape.inserted[0].radius == 50.0f);
    // Data1 < 0 inserts into the "leave" layer.
    scape.inserted.clear();
    instanceMessage->SetData1(static_cast<std::uint64_t>(static_cast<std::uint32_t>(-1)));
    CHECK(handler.DoMessage(instanceMessage));
    CHECK(scape.inserted.size() == 1 && scape.inserted[0].layer == 0xa9de8b59);
    // Data1 == 0 removes from both.
    scape.inserted.clear();
    scape.removed.clear();
    instanceMessage->SetData1(0);
    CHECK(handler.DoMessage(instanceMessage));
    CHECK(scape.removed.size() == 2);
    CHECK(scape.removed[0].layer == 0x49de8a94 && scape.removed[1].layer == 0xa9de8b59);

    // 0xaa396960 stops the sound of Data2.
    sndSys.postedIds.clear();
    TestMessage stop;
    stop->SetType(0xaa396960);
    stop->SetData2(0x55);
    CHECK(handler.DoMessage(stop));
    CHECK(sndSys.postedIds.size() == 1);
    CHECK(sndSys.postedIds[0].id == 0x6a451908 && sndSys.postedIds[0].data1 == 0x55);

    // 0xea3ada33 creates a soundscape instance with a fresh id; Data1 / Data2 pick the layer.
    scape.inserted.clear();
    FakeAutomaton automaton;
    TestMessage create;
    create->SetType(0xea3ada33);
    create->SetIGZUnknown(&occupant);   // 0xea3ada33 QIs the occupant interface for the position
    create->SetData1(0);
    create->SetData2(1);
    CHECK(handler.DoMessage(create));
    CHECK(scape.inserted.size() == 1);
    CHECK(scape.inserted[0].layer == 0x4a5e7ffc);
    CHECK(scape.inserted[0].instance != 0);
    CHECK(scape.inserted[0].x == 4.0f && scape.inserted[0].y == 5.0f && scape.inserted[0].z == 6.0f);
    scape.inserted.clear();
    create->SetData1(1);
    create->SetData2(2);
    CHECK(handler.DoMessage(create));
    CHECK(scape.inserted.size() == 1 && scape.inserted[0].layer == 0x0a5e7f51);
    scape.inserted.clear();
    create->SetData1(1);
    create->SetData2(3);
    CHECK(handler.DoMessage(create));
    CHECK(scape.inserted.size() == 1 && scape.inserted[0].layer == 0x6a5e8088);

    // 0x8a385dd8: a positional one-shot sound on the automaton, with the retrigger timer.
    sndSys.postedIds.clear();
    TestMessage effect;
    effect->SetType(0x8a385dd8);
    effect->SetData1(0x1234);
    effect->SetData2(0x5678);
    effect->SetIGZUnknown(&automaton);
    CHECK(handler.DoMessage(effect));
    CHECK(sndSys.postedIds.size() == 2);
    CHECK(sndSys.postedIds[0].id == 0xea451903 && sndSys.postedIds[0].data1 == 0x1234 &&
          sndSys.postedIds[0].data2 == 0x5678 && sndSys.postedIds[0].data3 == 1);
    CHECK(sndSys.postedIds[1].id == 0xca45190f && sndSys.postedIds[1].data1 == 0x5678);
    CHECK(sndSys.postedIds[1].data2 == 10 && sndSys.postedIds[1].data3 == 20 && sndSys.postedIds[1].data4 == 30);
    // The throttled sound id: the second call within the random delay posts nothing, after it posts again.
    g_ticks = 1000;
    sndSys.postedIds.clear();
    effect->SetData1(0xa9f14c47);
    CHECK(handler.DoMessage(effect));
    const std::size_t firstCount = sndSys.postedIds.size();
    CHECK(firstCount == 2);
    CHECK(handler.retriggerDelayMs() >= 1000);
    g_ticks += 1;   // still inside the 1000..3999 ms (or longer) window
    CHECK(handler.DoMessage(effect));
    CHECK(sndSys.postedIds.size() == firstCount);   // throttled
    g_ticks += handler.retriggerDelayMs();
    CHECK(handler.DoMessage(effect));
    CHECK(sndSys.postedIds.size() == firstCount + 2);   // window over: posted again

    handler.Shutdown();
    sc4audio::SetAudioEventHandlerHost(nullptr);
    SL::spMS2 = nullptr;
    SL::spPreferences = nullptr;
    g_services = saved;
    SetRZServiceResolver(nullptr);
}

void TestDispatchSounds() {
    FakeRadio radio;
    RecordingSndSys sndSys;
    FakeScape scape;
    FakeFileSystem fileSystem;
    RecordingMessageServer2 server;
    TestHost host;
    Services saved = g_services;
    g_services = Services{&radio, &sndSys, &scape, &fileSystem, &server};
    SetRZServiceResolver(&ResolveService);
    sc4audio::SetAudioEventHandlerHost(&host);
    SL::spMS2 = &server;
    SL::spPreferences = host.preferences;
    host.preferences[sc4audio::kPrefsAudioResetFlag] = 1;

    cSC4AudioEventHandler handler;
    handler.Init();

    FakePylon pylon;
    TestMessage message;
    message->SetType(0x894671f6);
    message->SetData1(1);   // fire dispatch type, one of 0/1/5/6 that make a sound
    message->SetIGZUnknown(&pylon);
    CHECK(handler.DoMessage(message));
    CHECK(sndSys.created.size() == 1);
    CHECK(sndSys.created[0].eventId == 0xea451903);
    CHECK(sndSys.created[0].classId == 0x5b523ba4);
    CHECK(sndSys.created[0].data1 == 0x09c66951);
    CHECK(sndSys.created[0].data2 == static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&pylon)));
    CHECK(sndSys.postedEvents.size() == 1 && sndSys.postedEvents[0] == 0xea451903);

    // Dispatch type 2 makes no sound.
    sndSys.created.clear();
    message->SetData1(2);
    CHECK(handler.DoMessage(message));
    CHECK(sndSys.created.empty());

    // Police dispatch uses the other sound id.
    sndSys.created.clear();
    message->SetType(0xe94671eb);
    message->SetData1(0);
    CHECK(handler.DoMessage(message));
    CHECK(sndSys.created.size() == 1 && sndSys.created[0].data1 == 0x29d99e9e);

    // The pylon messages only report whether the payload answers cISC4PylonOccupant.
    message->SetType(0xc9f1011a);
    message->SetData1(0);
    message->SetIGZUnknown(&pylon);
    CHECK(handler.DoMessage(message));
    FakeOccupant notAPylon;
    message->SetIGZUnknown(&notAPylon);
    CHECK(handler.DoMessage(message));                            // DoMessage itself always consumes the message
    CHECK(!handler.HandleDispatchManagerMessages(message));       // the sub-handler reports the failed QI
    message->SetIGZUnknown(nullptr);

    handler.Shutdown();
    sc4audio::SetAudioEventHandlerHost(nullptr);
    SL::spMS2 = nullptr;
    SL::spPreferences = nullptr;
    g_services = saved;
    SetRZServiceResolver(nullptr);
}

void TestDisasterFire() {
    FakeRadio radio;
    RecordingSndSys sndSys;
    FakeScape scape;
    FakeFileSystem fileSystem;
    RecordingMessageServer2 server;
    TestHost host;
    Services saved = g_services;
    g_services = Services{&radio, &sndSys, &scape, &fileSystem, &server};
    SetRZServiceResolver(&ResolveService);
    sc4audio::SetAudioEventHandlerHost(&host);
    SL::spMS2 = &server;
    SL::spPreferences = host.preferences;

    // Create() returns the cIGZInitShutdown view (the object's +8 group).
    cIGZInitShutdown* view = cSC4AudioEventHandlerDisasterFire::Create();
    CHECK(view != nullptr);
    auto* handler = dynamic_cast<cSC4AudioEventHandlerDisasterFire*>(view);
    CHECK(handler != nullptr);
    if (!handler) return;
    view->AddRef();   // the caller's reference; the object is created at count 0 like every GZ factory object

    CHECK(handler->Init());
    CHECK(handler->IsInitialized());
    CHECK(scape.layers.size() == 1);
    CHECK(scape.layers[0].name == "Fires");
    CHECK(scape.layers[0].layer == 0x49e4ce85);
    CHECK(scape.layers[0].flags == 0);
    CHECK(scape.layers[0].radius == 50.0f);
    CHECK(scape.sounds.size() == 3);
    CHECK(scape.sounds[0].sound == 0x29dc19ef && scape.sounds[0].a4 == 0 && scape.sounds[0].a5 == 1 &&
          scape.sounds[0].a6 == 9);
    CHECK(scape.sounds[1].sound == 0x09dc19a2 && scape.sounds[1].a5 == 10 && scape.sounds[1].a6 == 0x31);
    CHECK(scape.sounds[2].sound == 0x09dc1982 && scape.sounds[2].a5 == 0x32 && scape.sounds[2].a6 == 0x7fffffff);
    CHECK(handler->scapeManager() == &scape);

    // QueryInterface: 1 / 0x89e51472 answer the +8 view (quirk), 0xbb523bb2 the sound-event view, 0x452294aa
    // the message target.
    FakeOccupant occupant;
    occupant.fire = true;
    CHECK(handler->IsOccupantIncluded(&occupant));
    FakeOccupant notOnFire;
    CHECK(!handler->IsOccupantIncluded(&notOnFire));

    // Occupant inserted (0x99ef1142) -> InsertInstance in the disaster layer.
    TestMessage message;
    message->SetType(0x99ef1142);
    message->SetIGZUnknown(&occupant);
    CHECK(handler->DoMessage(message));
    CHECK(scape.inserted.size() == 1);
    CHECK(scape.inserted[0].layer == 0x49e4ce85);
    CHECK(scape.inserted[0].instance == static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&occupant)));
    // A non-fire occupant is filtered out.
    message->SetIGZUnknown(&notOnFire);
    CHECK(handler->DoMessage(message));
    CHECK(scape.inserted.size() == 1);
    // Occupant removed (0x99ef1143) -> RemoveInstance.
    message->SetType(0x99ef1143);
    message->SetIGZUnknown(&occupant);
    CHECK(handler->DoMessage(message));
    CHECK(scape.removed.size() == 1 && scape.removed[0].layer == 0x49e4ce85);

    // City init (0x26d31ec1) walks every fire occupant through the host's occupant manager.
    host.fireOccupants = {&occupant, &notOnFire};
    scape.inserted.clear();
    message->SetIGZUnknown(nullptr);   // drop the reference to the occupant before it leaves scope
    message.Reset();
    message->SetType(0x26d31ec1);
    CHECK(handler->DoMessage(message));
    CHECK(host.iterateCalls == 1);
    CHECK(scape.inserted.size() == 1);

    // Shutdown removes the three subscriptions and drops the soundscape manager.
    CHECK(handler->Shutdown());
    CHECK(!handler->IsInitialized());
    CHECK(handler->scapeManager() == nullptr);
    CHECK(handler->Shutdown());   // idempotent
    CHECK(handler->Release() == 0);

    sc4audio::SetAudioEventHandlerHost(nullptr);
    SL::spMS2 = nullptr;
    SL::spPreferences = nullptr;
    g_services = saved;
    SetRZServiceResolver(nullptr);
}

void TestNoServices() {
    // Without a framework resolver the handler still constructs; Init reports failure and nothing crashes.
    SetRZServiceResolver(nullptr);
    sc4audio::SetAudioEventHandlerHost(nullptr);
    {
        cSC4AudioEventHandler handler;
        CHECK(handler.radio() == nullptr);
        CHECK(handler.soundSystem() == nullptr);
        CHECK(!handler.Init());
        CHECK(!handler.DoMessage(nullptr));
        CHECK(handler.HandleRadioMessages(nullptr) == false);
        handler._SetRadioStation("krgn", 4000, true);   // no radio: no-op
        CHECK(handler.Shutdown());
    }
}

}  // namespace

int main() {
    TestConstructorAndInterfaceIds();
    TestInitAndShutdown();
    TestRegionMusic();
    TestCityMessages();
    TestConstructionAndAutomataSounds();
    TestDispatchSounds();
    TestDisasterFire();
    TestNoServices();

    if (g_failures == 0) {
        std::printf("sc4_audioeventhandler_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "sc4_audioeventhandler_test: %d failure(s)\n", g_failures);
    return 1;
}

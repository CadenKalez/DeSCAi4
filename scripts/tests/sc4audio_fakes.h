// Test doubles for the SC4 audio layer: sound system, sound manager, sounds, events, messages.
#pragma once
#include <cassert>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "audio/interfaces/cIGZSndCompletionNotification.h"
#include "audio/interfaces/cIGZSndMgr.h"
#include "audio/interfaces/cIGZSndSys.h"
#include "audio/interfaces/cIGZSndRegisterSet.h"
#include "audio/interfaces/cIHitTrackPlayer.h"
#include "sc4audio/SC4AudioHost.h"
#include "sc4audio/SoundManager.h"

namespace fakes {

template <class T>
struct Counted : T {
    int refs = 1;
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }   // fakes are owned by the test, never deleted
};

// ---- messages / events ---------------------------------------------------------------------
struct FakeMessage : cIGZMessage2Standard {
    std::uint32_t type = 0;
    bool has[5] = {};
    std::uint64_t data[5] = {};
    cIGZUnknown* unknown = nullptr;
    int refs = 1;

    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cIGZMessage2Standard || iid == 1) { *out = static_cast<cIGZMessage2Standard*>(this); ++refs; return true; }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Create(std::uint32_t, void**) const override { return false; }
    std::uint32_t GetType() const override { return type; }
    void SetType(std::uint32_t t) override { type = t; }
    bool IsEqual(const cIGZMessage2&) const override { return false; }
    bool IsLess(const cIGZMessage2&) const override { return false; }
    bool GetHasData1() const override { return has[1]; }
    std::uint64_t GetData1() const override { return data[1]; }
    void* GetVoid1() const override { return nullptr; }
    void SetData1(std::uint64_t v) override { has[1] = true; data[1] = v; }
    void SetVoid1(void*) override {}
    bool GetHasData2() const override { return has[2]; }
    std::uint64_t GetData2() const override { return data[2]; }
    void* GetVoid2() const override { return nullptr; }
    void SetData2(std::uint64_t v) override { has[2] = true; data[2] = v; }
    void SetVoid2(void*) override {}
    bool GetHasData3() const override { return has[3]; }
    std::uint64_t GetData3() const override { return data[3]; }
    void* GetVoid3() const override { return nullptr; }
    void SetData3(std::uint64_t v) override { has[3] = true; data[3] = v; }
    void SetVoid3(void*) override {}
    bool GetHasData4() const override { return has[4]; }
    std::uint64_t GetData4() const override { return data[4]; }
    void* GetVoid4() const override { return nullptr; }
    void SetData4(std::uint64_t v) override { has[4] = true; data[4] = v; }
    void SetVoid4(void*) override {}
    bool GetHasString() const override { return false; }
    bool GetString(std::uint32_t, void**) override { return false; }
    void SetString(cIGZString*) override {}
    bool GetHasIGZUnknown() const override { return unknown != nullptr; }
    cIGZUnknown* GetIGZUnknown() override { return unknown; }
    void SetIGZUnknown(cIGZUnknown* u) override { unknown = u; }
    bool GetHasExtra() const override { return false; }
    bool GetExtra(std::uint32_t, void**) override { return false; }
    void SetExtra(cIGZSerializable*) override {}
};

struct FakeEvent final : cIGZSndEventStd {
    std::uint32_t id = 0;
    FakeMessage message;
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool GetHasEventId() const override { return true; }
    std::uint32_t GetEventId() const override { return id; }
    void SetEventId(std::uint32_t v) override { id = v; }
    cIGZMessage2Standard* AsMessage2Standard() override { return &message; }
};

// ---- track player ----------------------------------------------------------------------------
struct FakePlayer;

// ---- sound ---------------------------------------------------------------------------------
struct FakeSnd final : cIGZSnd, cIGZSndRegisterSet, cIGZSndCompletionNotification {
    int refs = 1;
    std::map<std::int32_t, std::int32_t> regs;
    std::vector<std::pair<std::int32_t, std::int32_t>> registerWrites;
    bool playing = false;
    int plays = 0, kills = 0, stops = 0;
    std::int32_t playArgs[4] = {};
    std::int32_t volume = 0;
    cIGZSndCompletionFunctionObject* callback = nullptr;
    int callbackSets = 0;
    cIHitTrackPlayer* player = nullptr;
    bool playResult = true;

    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cIGZSnd || iid == 1) *out = static_cast<cIGZSnd*>(this);
        else if (iid == kGZIID_cIGZSndRegisterSet) *out = static_cast<cIGZSndRegisterSet*>(this);
        else if (iid == 0xe9d00a9a) *out = static_cast<cIGZSndCompletionNotification*>(this);
        else if (iid == kGZIID_cIHitTrackPlayer && player) { *out = player; player->AddRef(); return true; }
        else return false;
        ++refs;
        return true;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }

    // cIGZSnd
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    std::int32_t autoVolume = -1;
    bool PlayWithVolumeAndLoop(std::int32_t volume, bool, SndDup) override { autoVolume = volume; ++plays; playing = true; return true; }
    bool PlayWithScriptArguments(std::int32_t a, std::int32_t b, std::int32_t c, std::int32_t d) override {
        if (!playResult) return false;
        playArgs[0] = a; playArgs[1] = b; playArgs[2] = c; playArgs[3] = d;
        ++plays; playing = true;
        return true;
    }
    bool IsPlaying() const override { return playing; }
    bool Stop() override { ++stops; playing = false; return true; }
    bool Kill() override { ++kills; playing = false; return true; }
    bool StopAllInstances() override { return false; }
    std::uint32_t GetUseCount() const override { return 0; }
    std::uint32_t GetID() const override { return 0; }
    void SetID(std::uint32_t) override {}
    bool Load() override { return true; }
    bool Unload() override { return true; }
    bool IsLoaded() const override { return true; }
    bool Pause(bool) override { return true; }
    bool Unpause() override { return true; }
    bool IsPaused() const override { return false; }
    std::int32_t GetVolume() const override { return volume; }
    bool SetVolume(std::int32_t v) override { volume = v; return true; }
    bool FadeVolume(std::int32_t, std::int32_t, std::uint32_t, bool) override { return false; }
    bool IsFading() const override { return false; }
    std::int32_t GetPan() const override { return 0; }
    bool SetPan(std::int32_t) override { return true; }
    std::int32_t GetFrequency() const override { return 0; }
    bool SetFrequency(std::int32_t) override { return true; }
    std::int32_t GetOriginalFrequency() const override { return 0; }
    bool SetEffectsType(std::int32_t, std::uint32_t) override { return true; }
    bool SetEffectsLevel(std::int32_t, std::uint32_t) override { return true; }
    std::int32_t GetSoundPriority() const override { return 0; }
    bool SetSoundPriority(std::int32_t) override { return true; }
    bool SetName(const cIGZString&) override { return true; }
    bool GetName(cIGZString&) const override { return true; }
    bool SetSoundFileFromString(const cIGZString&, SndStreamingType) override { return true; }
    bool SetSoundFileFromChars(const char*, std::uint32_t, SndStreamingType) override { return true; }

    // cIGZSndRegisterSet
    bool SetRegisterValue(std::int32_t id, std::int32_t value, bool suppress) override {
        regs[id] = value;
        registerWrites.push_back({id, suppress ? -value - 1000000 : value});   // encodes the flag for position writes
        return true;
    }
    std::int32_t GetRegisterValue(std::int32_t id) const override {
        const auto it = regs.find(id);
        return it == regs.end() ? 0 : it->second;
    }
    void ClearRegisterValue(std::int32_t id, bool) override { regs.erase(id); }
    bool HasRegisterValue(std::int32_t id) const override { return regs.count(id) != 0; }
    bool SetRegisterFloat(std::uint32_t, float, bool) override { return true; }
    float GetRegisterFloat(std::uint32_t) const override { return 0; }

    // cIGZSndCompletionNotification
    bool SetCompletionNotification(cIGZSndCompletionFunctionObject* cb) override { callback = cb; ++callbackSets; return true; }
    bool StopCompletionNotification() override { return true; }
    std::uint32_t EstimateRemainingPlayTime() override { return 0; }

    void Finish() {   // the sound reached its end by itself
        playing = false;
        if (callback) callback->OnSoundCompleted(this);
    }
};

struct FakePlayer : cIHitTrackPlayer {
    int resets = 0;
    int refs = 1;
    FakeSnd* snd = nullptr;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cIGZSndRegisterSet) { *out = static_cast<cIGZSndRegisterSet*>(snd); ++snd->refs; return true; }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    cIGZSnd* AsIGZSnd() override { return snd; }
    cIGZSnd3D* AsIGZSnd3D() override { return nullptr; }
    cIGZSndMultipleSources* AsIGZSndMultipleSources() override { return nullptr; }
    cIGZSndRegisterSet* AsIGZSndRegisterSet() override { return snd; }
    bool SetTrackDef(cIGZPropertySet*) override { return true; }
    bool GetTrackDef(std::uint32_t, void**) override { return false; }
    bool SetHitList(cIHitList*) override { return true; }
    bool GetHitList(std::uint32_t, void**) override { return false; }
    float CompletionTime() override { return 0; }
    bool Reset() override { ++resets; return true; }
    void Update(std::int32_t) override {}
    bool UpdateArgs(std::int32_t, std::int32_t, std::int32_t, std::int32_t) override { return false; }
    bool PlayPause() override { return true; }
    void SetErrorCondition() override {}
    bool GetErrorCondition() override { return false; }
    void Step() override {}
    bool CurrentPlayPosition(const std::uint8_t**) override { return false; }
};

// ---- sound manager service -----------------------------------------------------------------
struct FakeSndMgr : cIGZSndMgr {
    std::map<std::uint32_t, FakeSnd*> sounds;   // soundId -> snd handed out
    std::vector<std::uint32_t> requested;
    std::vector<FakeSnd*> owned;
    std::function<void(std::uint32_t, FakeSnd&)> configure;   // called on first creation of an id
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 2; }
    std::uint32_t Release() override { return 1; }
    bool GetSoundById(std::uint32_t id, std::uint32_t, cIGZVariant*, std::uint32_t iid, void** out) override {
        requested.push_back(id);
        if (iid != kGZIID_cIGZSnd) return false;
        auto* snd = new FakeSnd();
        owned.push_back(snd);
        if (configure) configure(id, *snd);
        sounds[id] = snd;
        snd->AddRef();   // the caller's reference
        *out = static_cast<cIGZSnd*>(snd);
        return true;
    }
    ~FakeSndMgr() { for (auto* s : owned) delete s; }
    // Unused slots
    bool GetResource(const cGZPersistResourceKey&, std::uint32_t, void**) override { return false; }
    void SetSystemSndVol(std::int32_t) override {}
    std::int32_t GetSystemSndVol() const override { return 0; }
    std::uint32_t SystemSndID(std::int32_t) override { return 0; }
    bool SetSystemSndID(std::int32_t, std::uint32_t) override { return false; }
    bool SetDefaultSndDataSourcePlayer2DClassId(std::uint32_t) override { return false; }
    bool GetDefaultSndDataSourcePlayer2DClassId(std::uint32_t&) const override { return false; }
    bool SetDefaultSndDataSourcePlayer3DClassId(std::uint32_t) override { return false; }
    bool GetDefaultSndDataSourcePlayer3DClassId(std::uint32_t&) const override { return false; }
    void RegisterSnd(cIGZSnd*, std::uint32_t) override {}
    void UnregisterSndByPointer(cIGZSnd*) override {}
    void UnregisterSndById(std::uint32_t) override {}
    bool RegisterSndCreator(cIGZSoundCreator*) override { return false; }
    bool RemoveSndCreator(cIGZSoundCreator*) override { return false; }
    bool GetSoundByName(cIGZString&, std::uint32_t, cIGZVariant*, std::uint32_t, void**) override { return false; }
    bool SndAutoPlayWithVolume(std::uint32_t, std::int32_t) override { return false; }
    bool SndAutoPlay(std::uint32_t) override { return false; }
    bool SystemSndAutoPlayWithVolume(std::uint32_t, std::int32_t) override { return false; }
    bool SystemSndAutoPlay(std::uint32_t) override { return false; }
    bool AddSndToAutoPlayList(cIGZSnd*) override { return false; }
    bool GetSndDataSource(std::uint32_t, std::uint32_t, cIGZVariant*, std::uint32_t, void**) override { return false; }
    bool RegisterSndDataSourceCreator(cIGZSndDataSourceCreator*, std::uint32_t) override { return false; }
    bool RemoveSndDataSourceCreatorByPointer(cIGZSndDataSourceCreator*) override { return false; }
    bool RemoveSndDataSourceCreatorById(std::uint32_t) override { return false; }
    bool GetSndDataSourceCreator(std::uint32_t, std::uint32_t, void**) override { return false; }
    void RegisterSndDataSource(cIGZSndDataSource*, std::uint32_t) override {}
    void UnregisterSndDataSourceByPointer(cIGZSndDataSource*) override {}
    void UnregisterSndDataSourceById(std::uint32_t) override {}
};

// ---- listener --------------------------------------------------------------------------------
struct FakeListener final : cIGZSnd3DListener, cIGZSnd3DListenerEx {
    float position[3] = {};
    float orientation[6] = {};
    float rolloff = -1;
    int positionSets = 0, orientationSets = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 2; }
    std::uint32_t Release() override { return 1; }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    bool CommitDeferredSettings() override { return true; }
    bool GetPosition(float*, float*, float*) const override { return true; }
    bool SetPosition(float x, float y, float z) override { position[0] = x; position[1] = y; position[2] = z; ++positionSets; return true; }
    bool GetOrientation(float*, float*, float*, float*, float*, float*) const override { return true; }
    bool SetOrientation(float a, float b, float c, float d, float e, float f) override {
        orientation[0] = a; orientation[1] = b; orientation[2] = c; orientation[3] = d; orientation[4] = e; orientation[5] = f;
        ++orientationSets; return true;
    }
    cIGZSnd3DListenerEx* AsIGZSnd3DListenerEx() override { return this; }
    bool GetVelocity(float*, float*, float*) const override { return true; }
    bool SetVelocity(float, float, float) override { return true; }
    bool GetAll(float*, float*, float*, float*, float*, float*, float*, float*, float*, float*, float*, float*) const override { return true; }
    bool SetAll(float, float, float, float, float, float, float, float, float, float, float, float) override { return true; }
    bool GetDopplerFactor(float*) const override { return true; }
    bool SetDopplerFactor(float) override { return true; }
    bool GetRolloffFactor(float*) const override { return true; }
    bool SetRolloffFactor(float f) override { rolloff = f; return true; }
    bool GetDistanceFactor(float*) const override { return true; }
    bool SetDistanceFactor(float) override { return true; }
};

// ---- sound system ----------------------------------------------------------------------------
struct FakeSndSys : cIGZSndSys {
    std::vector<cIGZSndEventHandler*> handlers;
    std::vector<cIGZSndUpdateTarget*> targets;
    std::vector<std::uint32_t> targetIntervals;
    std::vector<std::uint32_t> events;   // ids passed to DoEvent
    int panics = 0;
    std::vector<FakeEvent*> allocated;
    FakeListener listener;
    std::vector<std::uint32_t> posted;

    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 2; }
    std::uint32_t Release() override { return 1; }
    ~FakeSndSys() { for (auto* e : allocated) delete e; }

    bool EventCreateWithData(std::uint32_t eventId, std::uint32_t, void** out, std::uint32_t d1, std::uint32_t d2,
                             std::uint32_t d3, std::uint32_t d4) override {
        auto* e = new FakeEvent();
        allocated.push_back(e);
        e->id = eventId;
        e->message.type = eventId;
        e->message.SetData1(d1); e->message.SetData2(d2); e->message.SetData3(d3); e->message.SetData4(d4);
        e->refs = 2;   // the creator's reference
        *out = static_cast<cIGZSndEventStd*>(e);
        return true;
    }
    bool DoEvent(cIGZSndEventStd* event) override {
        events.push_back(event->GetEventId());
        bool any = false;
        for (auto* h : handlers) any = h->DoEvent(event) || any;
        return any;
    }
    bool RegisterEventHandler(cIGZSndEventHandler* h) override { handlers.push_back(h); return true; }
    bool UnregisterEventHandler(cIGZSndEventHandler* h) override {
        for (auto it = handlers.begin(); it != handlers.end(); ++it) if (*it == h) { handlers.erase(it); return true; }
        return false;
    }
    bool RegisterTarget(cIGZSndUpdateTarget* t, SndCallbackInterval interval) override {
        targets.push_back(t); targetIntervals.push_back(interval); return true;
    }
    bool UnregisterTarget(cIGZSndUpdateTarget* t) override {
        for (std::size_t i = 0; i < targets.size(); ++i) if (targets[i] == t) {
            targets.erase(targets.begin() + static_cast<long>(i));
            targetIntervals.erase(targetIntervals.begin() + static_cast<long>(i));
            return true;
        }
        return false;
    }
    bool EnumerateTargets(UpdateTargetFunction f, std::uint32_t arg, void* user) override {
        for (auto* t : std::vector<cIGZSndUpdateTarget*>(targets)) if (!f(t, arg, user)) return false;
        return true;
    }
    void Panic() override { ++panics; }

    // Unused slots
    cIGZSnd3DListener* AsIGZSnd3DListener() override { return &listener; }
    cIGZSnd3DListenerEx* AsIGZSnd3DListenerEx() override { return &listener; }
    bool DoEventIds(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { return false; }
    bool DoEventUnknown(std::uint32_t, cIGZUnknown*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { return false; }
    bool DoEventString(std::uint32_t, cIGZString*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { return false; }
    bool PostEvent(cIGZSndEventStd* e) override { posted.push_back(e->GetEventId()); return true; }
    bool PostEventIds(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { return false; }
    bool PostEventUnknown(std::uint32_t, cIGZUnknown*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { return false; }
    bool PostEventString(std::uint32_t, cIGZString*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override { return false; }
    bool EventCreate(std::uint32_t, void**) override { return false; }
    bool Update(std::uint32_t) override { return true; }
    bool GetInitFlag() const override { return true; }
    void SetDataDirectory(const cIGZString&) override {}
    cIGZString* DataDirectory() override { return nullptr; }
    bool GetEnabledFlag() const override { return true; }
    void SetEnabledFlag(bool) override {}
    bool SetExtensionDefaultDataSourceClassId(const cIGZString&, std::uint32_t) override { return false; }
    bool GetExtensionDefaultDataSourceClassId(const cIGZString&, std::uint32_t&) override { return false; }
    std::uint32_t GetNumExtensionDefaultDataSourceClassIdMappings() override { return 0; }
    bool GetExtensionDefaultDataSourceClassIdMappingClassId(std::uint32_t, std::uint32_t&) override { return false; }
    bool GetExtensionDefaultDataSourceClassIdMappingExtension(std::uint32_t, cIGZString&) override { return false; }
    std::int32_t GetMasterVolume() const override { return 0; }
    bool SetMasterVolume(std::int32_t) override { return true; }
    bool EnableUpdates() override { return true; }
    bool DisableUpdates() override { return true; }
    bool RegisterRenderer(cIGZSndRenderer*) override { return false; }
    bool UnregisterRenderer(cIGZSndRenderer*) override { return false; }
    bool IsRegisteredRenderer(cIGZSndRenderer*) override { return false; }
    std::uint32_t NumRenderers() override { return 0; }
    bool FindPlayingGZSnd(std::uint32_t, std::uint32_t, void**) override { return false; }
    bool IsSndPlaying(std::uint32_t) override { return false; }
    cIGZCriticalSection* GetCriticalSection() override { return nullptr; }
    bool GetSndPropertyCacheByIid(cIGZSndPropertyCache**) override { return false; }
    cIGZSndPropertyCache* GetSndPropertyCache() override { return nullptr; }
    bool GetSndInstanceCacheByIid(cIGZSndInstanceCache**) override { return false; }
    cIGZSndInstanceCache* GetSndInstanceCache() override { return nullptr; }
    void AddToMainlineReleaseList(cIGZUnknown*) override {}
    void SetTestMode(bool) override {}
    bool GetTestMode() const override { return false; }
    std::uint32_t GetSoundSystemThreadId() override { return 0; }
};

struct FakeServer : cIGZMessageServer2 {
    std::vector<std::pair<cIGZMessageTarget2*, std::uint32_t>> added, removed;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 2; }
    std::uint32_t Release() override { return 1; }
    bool MessageSend(cIGZMessage2*) override { return false; }
    std::vector<std::uint32_t> posted;
    std::vector<cIGZUnknown*> postedUnknown;
    std::vector<void*> postedData1;
    bool MessagePost(cIGZMessage2* m, bool) override {
        posted.push_back(m->GetType());
        void* raw = nullptr;
        if (m->QueryInterface(kGZIID_cIGZMessage2Standard, &raw)) {
            auto* s = static_cast<cIGZMessage2Standard*>(raw);
            postedUnknown.push_back(s->GetIGZUnknown());
            postedData1.push_back(s->GetVoid1());
            s->Release();
        }
        return true;
    }
    bool AddNotification(cIGZMessageTarget2* t, std::uint32_t id) override { added.push_back({t, id}); return true; }
    bool RemoveNotification(cIGZMessageTarget2* t, std::uint32_t id) override { removed.push_back({t, id}); return true; }
    bool GeneralMessagePostToTarget(cIGZMessage2*, cIGZMessageTarget2*) override { return false; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return false; }
    bool OnTick(std::uint32_t) override { return false; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return 0; }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }
};

struct FakeGame : SC4Audio::cIGameAudioContext {
    SC4Audio::AudioPreferences prefs{true, 800};
    bool paused = false;
    bool haveListener = true;
    SC4Audio::ListenerView view;
    bool hasOccupants = true;
    float cityX = 0, cityZ = 0, seaLevel = 0;
    bool HasOccupantManager() override { return hasOccupants; }
    void GetCityExtentMeters(float& x, float& z) override { x = cityX; z = cityZ; }
    float GetSeaLevel() override { return seaLevel; }
    SC4Audio::AudioPreferences GetAudioPreferences() override { return prefs; }
    bool IsSimulationPaused() override { return paused; }
    bool GetListenerView(SC4Audio::ListenerView& out) override { out = view; return haveListener; }
};

struct FakeExemplars : SC4Audio::cIExemplarSource {
    bool exists = false;
    std::vector<float> curve;
    std::map<std::uint32_t, std::uint32_t> uints;
    int lastKeyType = 0;
    bool ExemplarExists(const cGZPersistResourceKey& key) override { lastKeyType = static_cast<int>(key.type); return exists; }
    bool GetUint32Property(const cGZPersistResourceKey&, std::uint32_t id, std::uint32_t& out) override {
        const auto it = uints.find(id);
        if (it == uints.end()) return false;
        out = it->second;
        return true;
    }
    std::map<std::uint32_t, float> floats;
    std::map<std::uint32_t, std::vector<float>> arrays;
    bool GetFloatProperty(const cGZPersistResourceKey&, std::uint32_t id, float& out) override {
        const auto it = floats.find(id);
        if (it == floats.end()) return false;
        out = it->second;
        return true;
    }
    bool GetFloatArrayProperty(const cGZPersistResourceKey&, std::uint32_t id, std::vector<float>& out) override {
        const auto it = arrays.find(id);
        if (it != arrays.end()) { out = it->second; return true; }
        if (id != 0x6a946a50 || curve.empty()) return false;
        out = curve;
        return true;
    }
};

}  // namespace fakes

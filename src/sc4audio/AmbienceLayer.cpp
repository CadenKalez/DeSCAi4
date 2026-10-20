#include "sc4audio/AmbienceLayer.h"

#include <climits>
#include <cmath>
#include <cstdio>

#include "audio/interfaces/cIFreshPlayer.h"
#include "sc4/SCPropertyHelp.h"
#include "sc4audio/SoundManager.h"

namespace SC4Audio {

namespace {

IAmbienceLayerHost* g_host = nullptr;

// Statics of cSC4AudioAmbienceLayer.
int g_staticInitCount = 0;                                  // m_nStaticInit
IAmbienceSndSink* g_sndSys = nullptr;                       // m_pSndSys (0x100b6de30)
bool g_resManager = false;                                  // m_pResManager != null
std::unordered_map<std::uint32_t, cIAmbienceFilterBase*> g_filterMap;   // m_AmbienceFilterMap
// HandleCell's last-cell cache (m_sLastXCell / m_sLastZCell / m_sfLastX / m_sfLastZ / m_sfLastAltitude)
int g_lastXCell = INT_MIN, g_lastZCell = INT_MIN;
float g_lastX = 0, g_lastZ = 0, g_lastAltitude = 0;

// x86 cvttss2si (32 bit): out-of-range and NaN give the "integer indefinite" value.
int Trunc32(float f) {
    if (!(f > -2147483904.0f && f < 2147483648.0f)) return INT_MIN;
    return static_cast<int>(f);
}
// x86 cvttss2si (64 bit) whose low 32 bits are used (the position event passes (long)f as a 32 bit argument):
// out-of-range and NaN give 0x8000000000000000, so the low half is 0.
std::uint32_t Trunc64Low32(float f) {
    if (!(f > -9223373136366403584.0f && f < 9223372036854775808.0f)) return 0;
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(f));
}
// (long)f then used as an unsigned 64 bit value, as the original does for the score events.
std::uint64_t FloatToU64(float f) {
    if (9.223372e+18f <= f) {
        std::int64_t v = static_cast<std::int64_t>(f - 9.223372e+18f);
        return static_cast<std::uint64_t>(v) ^ 0x8000000000000000ull;
    }
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(f));
}
std::uint64_t SignExtend(std::int32_t v) { return static_cast<std::uint64_t>(static_cast<std::int64_t>(v)); }

}  // namespace

IAmbienceLayerHost* AmbienceLayerHost() { return g_host; }
void SetAmbienceLayerHost(IAmbienceLayerHost* host) { g_host = host; }

bool cAmbienceSndSysAdapter::PostEvent(const AmbienceSndEvent& e) {
    void* raw = nullptr;
    // EventCreateWithData(eventId, class 0x5b523ba4, &event, d1..d4)
    if (!system_->EventCreateWithData(e.eventId, kGZCLSID_cGZSndEventStd, &raw, e.ctor[0], e.ctor[1], e.ctor[2], e.ctor[3]))
        return false;
    auto* event = static_cast<cIGZSndEventStd*>(raw);
    cIGZMessage2Standard* msg = event->AsMessage2Standard();
    if (e.unknown) msg->SetIGZUnknown(e.unknown);
    if (e.has[0]) msg->SetData1(e.data[0]);
    if (e.has[1]) msg->SetData2(e.data[1]);
    if (e.has[2]) msg->SetData3(e.data[2]);
    system_->PostEvent(event);
    event->Release();
    return true;
}

// ---------------------------------------------------------------------------------------------
// construction
// ---------------------------------------------------------------------------------------------

// cSC4AudioAmbienceLayer::cSC4AudioAmbienceLayer(uint) @ 0x10040ea7a: refcount starts at 0, enabled.
cSC4AudioAmbienceLayer::cSC4AudioAmbienceLayer(std::uint32_t layerInstanceId) : instanceId_(layerInstanceId) {}

// ~cSC4AudioAmbienceLayer @ 0x10040ee06: frees the vectors and lists and releases the three layers
// (intersection, cell, occupant order).
cSC4AudioAmbienceLayer::~cSC4AudioAmbienceLayer() {
    if (intersectLayer_) intersectLayer_->Release();
    if (cellLayer_) cellLayer_->Release();
    if (occupantLayer_) occupantLayer_->Release();
    // the score objects of leftover list nodes are released by the list node erase in the original
    // (std::list clear does not touch them); keep that behaviour: nothing to do here.
}

bool cSC4AudioAmbienceLayer::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x10040e41c
    if (iid == kGZIID_cISC4AudioAmbienceLayer || iid == 1) {
        *out = static_cast<cISC4AudioAmbienceLayer*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cSC4AudioAmbienceLayer::AddRef() { return ++refCount_; }   // @ 0x10040e43e

std::uint32_t cSC4AudioAmbienceLayer::Release() {   // @ 0x10040e44c: destroys when the count is below 2
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return --refCount_;
}

void cSC4AudioAmbienceLayer::SetName(const cIGZString& name) {   // @ 0x10040f004
    name_.Copy(name);
    name_.MakeLower();
}

void cSC4AudioAmbienceLayer::GetAveragePosition(cS3DVector3& out) {   // @ 0x10040f042
    out.x = average_[0];
    out.y = average_[1];
    out.z = average_[2];
}

void cSC4AudioAmbienceLayer::SetScoreMasterVolume(std::uint32_t volume) {   // @ 0x10040e288
    scoreMasterVolume_ = volume;
    UpdateAmbienceScores();
}

void cSC4AudioAmbienceLayer::Pause(bool pause) {   // @ 0x10040e298
    paused_ = pause;
    UpdateAmbienceScores();
    UpdateAmbienceLoops(0x7fffffff);
}

bool cSC4AudioAmbienceLayer::PostEvent(const AmbienceSndEvent& e) { return g_sndSys && g_sndSys->PostEvent(e); }

// ---------------------------------------------------------------------------------------------
// statics
// ---------------------------------------------------------------------------------------------

int cSC4AudioAmbienceLayer::StaticInitCount() { return g_staticInitCount; }
std::size_t cSC4AudioAmbienceLayer::SharedFilterCount() { return g_filterMap.size(); }

// _StaticInit @ 0x10040b34a: needs the resource manager and the sound system.
bool cSC4AudioAmbienceLayer::StaticInit() {
    IAmbienceLayerHost* host = AmbienceLayerHost();
    if (!host) return false;
    g_resManager = host->HasResourceManager();
    if (!g_resManager) return false;
    IAmbienceSndSink* sink = host->AcquireSoundSink();
    if (!sink) return false;
    g_sndSys = sink;
    return true;
}

// _StaticRemoveAllFilters @ 0x10040e5a0: every shared filter is Shutdown, and the map's reference dropped
// (AddRef, Shutdown, Release - a zero result is logged -, Release).
void cSC4AudioAmbienceLayer::StaticRemoveAllFilters() {
    for (auto& entry : g_filterMap) {
        cIAmbienceFilterBase* filter = entry.second;
        filter->AddRef();
        filter->Shutdown();
        if (filter->Release() == 0) {
            if (AmbienceLayerHost()) {
                char buffer[160];
                std::snprintf(buffer, sizeof buffer, "Error: cSC4AudioAmbienceLayer::_StaticRemoveAllFilters : Failed to release filter (%08x)\n", entry.first);
                AmbienceLayerHost()->LogError(buffer);
            }
        }
        filter->Release();
    }
    g_filterMap.clear();
}

// _StaticShutdown @ 0x10040c22a
void cSC4AudioAmbienceLayer::StaticShutdown() {
    StaticRemoveAllFilters();
    g_resManager = false;
    g_sndSys = nullptr;
}

// ---------------------------------------------------------------------------------------------
// Init / Shutdown / DoMessage
// ---------------------------------------------------------------------------------------------

bool cSC4AudioAmbienceLayer::Init() {   // @ 0x10040b266
    if (!initialised_) {
        initialised_ = true;
        bool first = (g_staticInitCount == 0);
        ++g_staticInitCount;
        if (first && !StaticInit()) return false;    // quirk: the initialised flag and the count stay set
        ReadTuningValues(instanceId_);
        if (cIGZMessageServer2* server = GetServices().messageServer) server->AddNotification(this, 0xc53d10aa);
        CreateFilters();
        CreateLayers();
        LoadFreshnessScore();                         // vslot 0x70
        AddAmbienceLoop(freshnessLoopId_);            // vslot 0x60 (0x260)
    }
    return true;
}

bool cSC4AudioAmbienceLayer::Shutdown() {   // @ 0x10040bd2c
    if (initialised_) {
        if (cIGZMessageServer2* server = GetServices().messageServer) server->RemoveNotification(this, 0xc53d10aa);
        RemoveLayers();
        RemoveAllFilters();
        RemoveAllFreshnessScores();
        RemoveAllAmbienceLoops();
        --g_staticInitCount;
        if (g_staticInitCount == 0) StaticShutdown();
        initialised_ = false;
    }
    return true;
}

// DoMessage @ 0x10040e39a: the tuning-reloaded message (0xc53d10aa) rebuilds the layer.
bool cSC4AudioAmbienceLayer::DoMessage(cIGZMessage2* message) {
    if (message && message->GetType() == 0xc53d10aa) {
        ReadTuningValues(instanceId_);
        RemoveAllFilters();
        CreateFilters();
        RemoveLayers();
        CreateLayers();
        Enable(!enabled_);     // quirk: two toggles in a row, the net effect is none
        Enable(!enabled_);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// tuning
// ---------------------------------------------------------------------------------------------

bool cSC4AudioAmbienceLayer::ReadTuningValues(std::uint32_t id) {   // @ 0x10040b438
    using sc4::cSCPropertyHelp;
    IAmbienceLayerHost* host = AmbienceLayerHost();
    ease_.SetDelta(100.0f, 4000.0f);
    {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "Noname_0x%08x", id);
        name_.AssignFromCString(buffer);
    }
    flag280_ = true; flag281_ = true;      // 0x1f1f: both bytes 0x1f (non-zero)
    scoreMasterVolume_ = 0x400;            // +0x288
    value28c_ = 0x400;                     // +0x28c
    radiusA_ = radiusB_ = 50.0f;
    masterPercent_ = 100;
    mode_ = 1;
    amountSource_ = 5;
    spread_ = 1.0f;
    quality_ = host ? host->RenderAudioQuality() : 0;
    // default curves: score parameter 100 flat, score volume 0..1024 over 0..20, loop volume 1024 flat
    parameterCurve_.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 100.0f}, {20.0f, 100.0f}});
    volumeCurve_.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 0.0f}, {20.0f, 1024.0f}});
    loopCurve_.SetResponseCurveData(std::vector<SC4Point>{{0.0f, 1024.0f}, {20.0f, 1024.0f}});

    cGZPersistResourceKey key{kTuningType, kTuningGroup, id};
    sc4::cISCPropertyHolder* holder = host ? host->GetTuningExemplar(key) : nullptr;
    if (!holder) {
        if (host) {
            char buffer[128];
            std::snprintf(buffer, sizeof buffer, "Error cSC4AudioAmbienceLayer::_ReadTuningValues : Failed to get resource (%08x)\n", id);
            host->LogError(buffer);
        }
        return false;
    }
    name_.AssignFromCString("");     // quirk: the default name is wiped, a missing name property leaves it empty
    cSCPropertyHelp::GetPropertyValue(holder, 0x20, name_);
    scoreKeys_.clear();
    cSCPropertyHelp::GetPropertyValue(holder, 0xea3fbdd7, scoreKeys_);
    filterIds_[0].clear();
    cSCPropertyHelp::GetPropertyValue(holder, 0x2a3fbde1, filterIds_[0]);
    filterIds_[1].clear();
    cSCPropertyHelp::GetPropertyValue(holder, 0x2a3fbde5, filterIds_[1]);
    filterIds_[2].clear();
    cSCPropertyHelp::GetPropertyValue(holder, 0xa410ba0, filterIds_[2]);
    freshnessLoopId_ = 0;
    cSCPropertyHelp::GetPropertyValue(holder, 0x6a3fbdda, freshnessLoopId_);
    std::uint8_t byte = 0;
    cSCPropertyHelp::GetPropertyValue(holder, 0xa25896b, byte);
    flag280_ = byte != 0;
    byte = 0;
    cSCPropertyHelp::GetPropertyValue(holder, 0xaa3fbdde, byte);
    flag281_ = byte != 0;
    cSCPropertyHelp::GetPropertyValue(holder, 0xea3fc42a, value28c_);
    cSCPropertyHelp::GetPropertyValue(holder, 0xaa3fc48e, scoreMasterVolume_);
    cSCPropertyHelp::GetPropertyValue(holder, 0x6a3fc4c7, masterPercent_);
    std::uint32_t u = static_cast<std::uint32_t>(mode_);
    cSCPropertyHelp::GetPropertyValue(holder, 0x8a552758, u);
    mode_ = static_cast<std::int32_t>(u);
    u = static_cast<std::uint32_t>(amountSource_);
    cSCPropertyHelp::GetPropertyValue(holder, 0x8a70b1a2, u);
    amountSource_ = static_cast<std::int32_t>(u);
    cSCPropertyHelp::GetPropertyValue(holder, 0x8a3fc5dd, spread_);
    // SetResponseCurveData(key, property): the curve keeps its default when the property is missing
    std::vector<float> values;
    if (cSCPropertyHelp::GetPropertyValue(holder, 0xca4132e9, values)) parameterCurve_.SetResponseCurveData(values.data(), values.size());
    if (cSCPropertyHelp::GetPropertyValue(holder, 0x6a4132f3, values)) volumeCurve_.SetResponseCurveData(values.data(), values.size());
    if (cSCPropertyHelp::GetPropertyValue(holder, 0x6a4132f6, values)) loopCurve_.SetResponseCurveData(values.data(), values.size());
    cSCPropertyHelp::GetPropertyValue(holder, 0xa4168e1, radiusA_);
    radiusB_ = radiusA_;
    cSCPropertyHelp::GetPropertyValue(holder, 0xa56212a, radiusB_);
    std::uint32_t positionSpeed = 100, volumeSpeed = 4000;
    cSCPropertyHelp::GetPropertyValue(holder, 0x8aa039b7, positionSpeed);
    cSCPropertyHelp::GetPropertyValue(holder, 0x4aa03db1, volumeSpeed);
    ease_.SetDelta(static_cast<float>(positionSpeed), static_cast<float>(volumeSpeed));
    holder->Release();
    return true;
}

// ---------------------------------------------------------------------------------------------
// filters
// ---------------------------------------------------------------------------------------------

void cSC4AudioAmbienceLayer::AddFilter(std::uint32_t filterId, int kind) {   // @ 0x10040e69c
    IAmbienceLayerHost* host = AmbienceLayerHost();
    cIAmbienceFilterBase* filter = nullptr;
    auto found = g_filterMap.find(filterId);
    if (found == g_filterMap.end()) {
        void* raw = nullptr;
        if (!host || !host->CreateObject(filterId, kGZIID_cIAmbienceFilterBase, &raw) || !raw) {
            if (host) {
                char buffer[128];
                std::snprintf(buffer, sizeof buffer, "Error: cSC4AudioAmbienceLayer::_AddFilter : Failed to create class object filter (%08x)\n", filterId);
                host->LogError(buffer);
            }
            return;
        }
        filter = static_cast<cIAmbienceFilterBase*>(raw);
        if (!filter->Init()) {
            if (host) {
                char buffer[128];
                std::snprintf(buffer, sizeof buffer, "Error: cSC4AudioAmbienceLayer::_AddFilter : Failed to init filter (%08x)\n", filterId);
                host->LogError(buffer);
            }
            filter->Release();
            return;
        }
        filter->AddRef();                 // the shared map's reference
        g_filterMap[filterId] = filter;
    } else {
        filter = found->second;
        filter->AddRef();                 // temporary reference (released below)
    }
    filter->AddRef();                     // the layer's reference
    // count which filter interfaces it supports (QueryInterface result released again at once)
    void* probe = nullptr;
    if (filter->QueryInterface(sc4::kGZIID_cIAmbienceCellFilter, &probe)) { ++cellFilterKinds_; static_cast<cIGZUnknown*>(probe)->Release(); }
    probe = nullptr;
    if (filter->QueryInterface(sc4::kGZIID_cIAmbienceOccupantFilter, &probe)) { ++occupantFilterKinds_; static_cast<cIGZUnknown*>(probe)->Release(); }
    probe = nullptr;
    if (filter->QueryInterface(sc4::kGZIID_cIAmbienceFilter, &probe)) { ++stateFilterKinds_; static_cast<cIGZUnknown*>(probe)->Release(); }
    if (kind >= 1 && kind <= 3) filters_[kind - 1].push_back(filter);
    filter->Release();                    // drop the temporary reference
}

void cSC4AudioAmbienceLayer::CreateFilters() {   // @ 0x10040ba4c
    cellFilterKinds_ = occupantFilterKinds_ = stateFilterKinds_ = 0;
    std::vector<bool>* bits[3] = {&bits1_, &bits2_, &bits3_};
    for (int k = 0; k < 3; ++k) {
        bits[k]->assign(filterIds_[k].size(), false);
        for (std::size_t i = 0; i < filterIds_[k].size(); ++i) AddFilter(filterIds_[k][i], k + 1);
    }
}

void cSC4AudioAmbienceLayer::RemoveAllFilters() {   // @ 0x10040be6e
    for (int k = 0; k < 3; ++k) {
        for (std::size_t i = filters_[k].size(); i-- > 0;) {
            cIAmbienceFilterBase* f = filters_[k][i];
            if (f) f->AddRef();
            f->Release();
            filters_[k][i] = nullptr;
            f->Release();            // net: the layer's reference is dropped
        }
        filters_[k].clear();         // the bit vectors are left alone
    }
}

template <class Iface, class F>
cSC4AudioAmbienceLayer::FilterEval cSC4AudioAmbienceLayer::EvaluateFilters(std::uint32_t iid, F&& isTrue) {
    FilterEval r;
    std::vector<bool>* bits[3] = {&bits1_, &bits2_, &bits3_};
    // list 1: every filter must be true
    for (std::size_t i = 0; i < filters_[0].size(); ++i) {
        void* raw = nullptr;
        if (filters_[0][i]->QueryInterface(iid, &raw)) {
            auto* q = static_cast<Iface*>(raw);
            bool t = isTrue(q);
            q->Release();
            if (!t) { r.rejected = true; return r; }
            ++r.counted;
            if (i < bits[0]->size()) (*bits[0])[i] = true;
        }
    }
    // list 2: every filter must be false
    for (std::size_t i = 0; i < filters_[1].size(); ++i) {
        void* raw = nullptr;
        if (filters_[1][i]->QueryInterface(iid, &raw)) {
            auto* q = static_cast<Iface*>(raw);
            bool t = isTrue(q);
            q->Release();
            if (t) { r.rejected = true; return r; }
            ++r.counted;
            if (i < bits[1]->size()) (*bits[1])[i] = true;
        }
    }
    // list 3: at least one filter must be true; the scan stops at the first
    std::size_t i = 0;
    for (; i < filters_[2].size(); ++i) {
        void* raw = nullptr;
        if (filters_[2][i]->QueryInterface(iid, &raw)) {
            auto* q = static_cast<Iface*>(raw);
            bool t = isTrue(q);
            q->Release();
            ++r.counted;
            if (t) {
                if (i < bits[2]->size()) (*bits[2])[i] = true;
                break;
            }
        }
    }
    r.orIndex = i;
    return r;
}

// ---------------------------------------------------------------------------------------------
// layers
// ---------------------------------------------------------------------------------------------

namespace {
// Replaces a layer pointer: the new layer is AddRef'd before the old one is released.
void AssignLayer(cGroupLayer*& slot, cGroupLayer* layer) {
    cGroupLayer* old = slot;
    if (old != layer) {
        if (layer) layer->AddRef();
        slot = layer;
        if (old) old->Release();
    }
}
}  // namespace

void cSC4AudioAmbienceLayer::CreateLayers() {   // @ 0x10040bb98
    AssignLayer(occupantLayer_, new cGroupLayer(radiusA_));
    occupantLayer_->Init();
    if (mode_ == 1) {
        AssignLayer(cellLayer_, occupantLayer_);       // one layer serves both
    } else {
        AssignLayer(cellLayer_, new cGroupLayer(radiusB_));
        cellLayer_->Init();
        AssignLayer(intersectLayer_, new cGroupLayer(mode_ == 2 ? radiusB_ : radiusA_));
        intersectLayer_->Init();
    }
}

void cSC4AudioAmbienceLayer::RemoveLayers() {   // @ 0x10040bddc
    if (intersectLayer_) {
        intersectLayer_->Shutdown();
        cGroupLayer* l = intersectLayer_;
        intersectLayer_ = nullptr;
        l->Release();
    }
    if (cellLayer_ && cellLayer_ != occupantLayer_) {
        cellLayer_->Shutdown();
        cGroupLayer* l = cellLayer_;
        cellLayer_ = nullptr;
        l->Release();
    }
    // quirk: when both pointers are the same layer the cell pointer (and its extra reference) is kept
    if (occupantLayer_) {
        occupantLayer_->Shutdown();
        cGroupLayer* l = occupantLayer_;
        occupantLayer_ = nullptr;
        l->Release();
    }
}

// ---------------------------------------------------------------------------------------------
// freshness scores / loops
// ---------------------------------------------------------------------------------------------

// _RemoveAllFreshnessScores @ 0x10040c036
bool cSC4AudioAmbienceLayer::RemoveAllFreshnessScores() {
    for (auto it = scores_.begin(); it != scores_.end();) {
        AmbienceSndEvent e;
        e.eventId = kEventScoreRemove;
        e.unknown = it->player;
        PostEvent(e);
        if (it->player) {
            cIGZUnknown* p = it->player;
            it->player = nullptr;
            p->Release();
        }
        it = scores_.erase(it);
    }
    return true;
}

// _RemoveAllAmbienceLoops @ 0x10040c13e
bool cSC4AudioAmbienceLayer::RemoveAllAmbienceLoops() {
    for (auto it = loops_.begin(); it != loops_.end();) {
        if (it->instanceId != 0) {
            AmbienceSndEvent e;
            e.eventId = kEventStopSound;
            e.ctor[0] = it->instanceId;
            PostEvent(e);
        }
        it = loops_.erase(it);
    }
    return true;
}

bool cSC4AudioAmbienceLayer::AddAmbienceLoop(std::uint32_t soundId) {   // @ 0x10040e2be
    if (soundId == 0) return false;
    for (const tAmbienceLoop& l : loops_)
        if (l.soundId == soundId) return false;
    tAmbienceLoop loop;
    loop.soundId = soundId;
    loops_.push_back(loop);
    return true;
}

bool cSC4AudioAmbienceLayer::RemoveAmbienceLoop(std::uint32_t soundId) {   // @ 0x10040e34a / 0x10040e372
    for (auto it = loops_.begin(); it != loops_.end(); ++it) {
        if (it->soundId == soundId) {
            loops_.erase(it);       // note: a playing sound is not stopped
            return true;
        }
    }
    return false;
}

void cSC4AudioAmbienceLayer::LoadFreshnessScore() {   // @ 0x10040dd8e
    IAmbienceLayerHost* host = AmbienceLayerHost();
    int n = static_cast<int>(scoreKeys_.size());
    for (int i = 0; i < n; ++i) {
        std::uint64_t packed = static_cast<std::uint64_t>(scoreKeys_[i]);
        cGZPersistResourceKey key{kScoreResourceType, static_cast<std::uint32_t>(packed), static_cast<std::uint32_t>(packed >> 32)};
        if (!AddFreshnessScore(key) && host) {
            char buffer[160];
            std::snprintf(buffer, sizeof buffer, "Error : cSC4AudioAmbienceLayer::_LoadFreshnessScore : Failed to load freshness score %08x:%08x:%08x\n",
                          key.type, key.group, key.instance);
            host->LogError(buffer);
        }
    }
}

bool cSC4AudioAmbienceLayer::AddFreshnessScore(const cGZPersistResourceKey& key) {   // @ 0x10040de80
    for (const tFreshnessScore& s : scores_)
        if (s.key == key) return false;
    IAmbienceLayerHost* host = AmbienceLayerHost();
    void* raw = nullptr;
    if (!host || !host->CreateObject(kClsidFreshPlayerObject, kIidFreshPlayerObject, &raw) || !raw) return false;
    auto* player = static_cast<cIGZUnknown*>(raw);
    bool result = false;
    AmbienceSndEvent e;
    e.eventId = kEventScoreAdd;
    e.unknown = player;
    e.has[0] = e.has[1] = e.has[2] = true;
    e.data[0] = key.type; e.data[1] = key.group; e.data[2] = key.instance;
    if (PostEvent(e)) {
        tFreshnessScore score;
        score.active = false;
        score.pending = true;
        score.key = key;
        player->AddRef();
        score.player = player;
        scores_.push_back(score);
        result = true;
    }
    player->Release();      // the creation reference
    return result;
}

bool cSC4AudioAmbienceLayer::RemoveFreshnessScore(std::uint32_t instance) {   // @ 0x10040e140
    for (auto it = scores_.begin(); it != scores_.end(); ++it) {
        if (it->key.instance != instance) continue;
        AmbienceSndEvent e;
        e.eventId = kEventScoreRemove;
        e.unknown = it->player;
        PostEvent(e);
        // quirk: the player is released once unconditionally and once more through the nulling release
        it->player->Release();
        if (cIGZUnknown* p = it->player) {
            it->player = nullptr;
            p->Release();
        }
        scores_.erase(it);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// handling cells and occupants
// ---------------------------------------------------------------------------------------------

bool cSC4AudioAmbienceLayer::CanHandle() {   // @ 0x10040c27c
    const SC4Audio::Services& services = GetServices();
    if (!services.game || !services.game->GetAudioPreferences().soundEnabled) return false;
    if (canHandleComputed_) return canHandleResult_;
    canHandleComputed_ = true;
    FilterEval r = EvaluateFilters<sc4::cIAmbienceFilter>(sc4::kGZIID_cIAmbienceFilter, [](sc4::cIAmbienceFilter* f) { return f->IsTrue(); });
    if (r.rejected || r.counted == 0) return false;
    if (filters_[2].empty() || r.orIndex != filters_[2].size()) {
        canHandleResult_ = true;
        return true;
    }
    return canHandleResult_;
}

bool cSC4AudioAmbienceLayer::HandleCell(int x, int z) {   // @ 0x10040c4fa
    if (!CanHandle()) return false;
    FilterEval r = EvaluateFilters<sc4::cIAmbienceCellFilter>(sc4::kGZIID_cIAmbienceCellFilter, [&](sc4::cIAmbienceCellFilter* f) { return f->IsTrueCell(x, z); });
    if (r.rejected) {
        ++cellsRejected_;
    } else if (r.counted != 0 && (filters_[2].empty() || r.orIndex != filters_[2].size())) {
        ++cellsAccepted_;
        IAmbienceLayerHost* host = AmbienceLayerHost();
        if (g_lastXCell != x || g_lastZCell != z) {
            host->CellToWorld(x, z, g_lastX, g_lastZ);
            g_lastAltitude = host->TerrainAltitude(g_lastX, g_lastZ);
            g_lastXCell = x;
            g_lastZCell = z;
        }
        cS3DVector3 position{g_lastX, g_lastAltitude, g_lastZ};
        cellLayer_->InsertInstance(position, 0.0f);
    }
    return false;       // quirk: always false
}

bool cSC4AudioAmbienceLayer::HandleOccupant(sc4::cISC4Occupant* occupant) {   // @ 0x10040c82e
    bool can = CanHandle();
    if (!occupant || !can) return false;
    FilterEval r = EvaluateFilters<sc4::cIAmbienceOccupantFilter>(sc4::kGZIID_cIAmbienceOccupantFilter, [&](sc4::cIAmbienceOccupantFilter* f) { return f->IsTrueOccupant(occupant); });
    if (r.rejected) {
        ++occupantsRejected_;
    } else if (r.counted != 0 && (filters_[2].empty() || r.orIndex != filters_[2].size())) {
        ++occupantsAccepted_;
        float p[3];
        occupant->GetPosition(p);
        cS3DVector3 position{p[0], p[1], p[2]};
        occupantLayer_->InsertInstance(position, 0.0f);
    }
    return false;       // quirk: always false
}

// The occupant's id in the group layer is the pointer truncated to 32 bits.
static std::uint32_t OccupantKey(sc4::cISC4Occupant* occupant) {
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(occupant));
}

bool cSC4AudioAmbienceLayer::InsertOccupant(sc4::cISC4Occupant* occupant) {   // @ 0x10040caec
    if (!occupant) return false;
    float p[3];
    occupant->GetPosition(p);
    cS3DVector3 position{p[0], p[1], p[2]};
    if (occupantLayer_->InsertInstance(OccupantKey(occupant), position, 0.0f)) {
        occupant->AddRef();
        ++occupantsAccepted_;
        return true;
    }
    return false;
}

bool cSC4AudioAmbienceLayer::MoveOccupant(sc4::cISC4Occupant* occupant) {   // @ 0x10040cb7c
    if (occupant) {
        float p[3];
        occupant->GetPosition(p);
        cS3DVector3 position{p[0], p[1], p[2]};
        occupantLayer_->UpdateInstance(OccupantKey(occupant), position, radiusA_);
    }
    return occupant != nullptr;
}

bool cSC4AudioAmbienceLayer::RemoveOccupant(sc4::cISC4Occupant* occupant) {   // @ 0x10040cbfa
    if (occupant && occupantLayer_->RemoveInstance(OccupantKey(occupant))) {
        occupant->Release();
        --occupantsAccepted_;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------------------------

void cSC4AudioAmbienceLayer::BeginUpdate(const SC4RectFloat& rect) {   // @ 0x10040cc36
    rect_ = rect;
    if (occupantLayer_) occupantLayer_->Clear(true);
    if (cellLayer_ && cellLayer_ != occupantLayer_) cellLayer_->Clear(true);
    occupantsRejected_ = cellsRejected_ = 0;
    occupantsAccepted_ = cellsAccepted_ = 0;
    canHandleResult_ = canHandleComputed_ = false;
    for (std::size_t i = 0; i < filters_[0].size() && i < bits1_.size(); ++i) bits1_[i] = false;
    for (std::size_t i = 0; i < filters_[1].size() && i < bits2_.size(); ++i) bits2_[i] = false;
    for (std::size_t i = 0; i < filters_[2].size() && i < bits3_.size(); ++i) bits3_[i] = false;
}

void cSC4AudioAmbienceLayer::EndUpdate() {   // @ 0x10040dd56
    if (occupantLayer_) occupantLayer_->ClearEmptyGroups();
    if (cellLayer_ && cellLayer_ != occupantLayer_) cellLayer_->ClearEmptyGroups();
}

// _GetEquallyDistributedPositions @ 0x10040d1f6: fewer than ten positions are copied as they are; otherwise
// the update rectangle is cut into 3x3 cells and the first position of each cell is kept.
void cSC4AudioAmbienceLayer::GetEquallyDistributedPositions(const std::list<cS3DVector3>& list, int count, float (*out)[3], int& outCount) {
    if (static_cast<std::uint32_t>(count) < 10) {
        outCount = 0;
        for (const cS3DVector3& p : list) {
            float* o = out[outCount++];
            o[0] = p.x; o[1] = p.y; o[2] = p.z;
        }
        return;
    }
    int cellCount[12] = {};
    const float x1 = rect_.x1, x0 = rect_.x0, z0 = rect_.z0, z1 = rect_.z1;
    outCount = 0;
    for (const cS3DVector3& p : list) {
        float dividerX = static_cast<float>(Trunc32((x1 - x0) / 3.0f));
        float dividerZ = static_cast<float>(Trunc32((z1 - z0) / 3.0f));
        int cx = Trunc32((p.x - x0) / dividerX);
        int cz = Trunc32((p.z - z0) / dividerZ);
        if (cx < 0) cx = 0;
        if (cz < 0) cz = 0;
        if (cx > 2) cx = 2;
        if (cz > 2) cz = 2;
        std::uint32_t cell = static_cast<std::uint32_t>(cz * 3 + cx);
        int before = cellCount[cell]++;
        if (before == 0) {
            float* o = out[outCount++];
            o[0] = p.x; o[1] = p.y; o[2] = p.z;
        }
    }
}

void cSC4AudioAmbienceLayer::Update(std::uint32_t elapsedMs, const cS3DVector3& eye, const cS3DVector3& target) {   // @ 0x10040cd3c
    eye_[0] = eye.x; eye_[1] = eye.y; eye_[2] = eye.z;
    target_[0] = target.x; target_[1] = target.y; target_[2] = target.z;
    // These bit scans set the enabled flag, but CanHandle() below overwrites it.
    for (std::size_t i = 0; i < filters_[0].size() && i < bits1_.size(); ++i) if (!bits1_[i]) enabled_ = false;
    for (std::size_t i = 0; i < filters_[1].size() && i < bits2_.size(); ++i) if (!bits2_[i]) enabled_ = false;
    for (std::size_t i = 0; i < filters_[2].size() && i < bits3_.size(); ++i) if (bits3_[i]) enabled_ = true;
    enabled_ = CanHandle();
    if (enabled_) {
        std::list<cS3DVector3> list;
        int total = 0;
        if (mode_ == 2 || mode_ == 3) {
            intersectLayer_->Intersect(occupantLayer_, cellLayer_, mode_ == 2);
            positionCount_ = 9;
            intersectLayer_->GetPositions(rect_, list, positionCount_, total, false);
        } else if (mode_ == 1) {
            positionCount_ = 9;
            occupantLayer_->GetPositions(rect_, list, positionCount_, total, false);
        }
        float amount = 3.4028235e+38f;
        bool store = true;
        IAmbienceLayerHost* host = AmbienceLayerHost();
        switch (amountSource_) {
            case 1: amount = static_cast<float>(total); break;
            case 2: amount = static_cast<float>(host ? host->ResidentialPopulation() : 0); break;
            case 3: break;      // stays at FLT_MAX
            case 4: amount = static_cast<float>(ViewUtilities() ? ViewUtilities()->GetZoomLevel() : 0); break;
            case 5: amount = positionCount_ == 0 ? 0.0f : static_cast<float>(total) / static_cast<float>(positionCount_); break;
            case 6: amount = static_cast<float>(host ? host->IndustrialPopulation() : 0u); break;
            default: store = false; break;
        }
        if (store) amount_ = amount;
        if (positionCount_ != 0) {
            int count = positionCount_;
            GetEquallyDistributedPositions(list, count, positions_, positionCount_);
            float sumX = 0.0f, sumY = 0.0f, sumZ = 0.0f, weightSum = 0.0f;
            if (positionCount_ >= 1) {
                // unit direction listener eye -> target
                float dy = eye_[1] - target_[1];
                float dz0 = eye_[2] - target_[2];
                float dx0 = eye_[0] - target_[0];
                float t0 = dz0 * dz0;
                float t1 = dy * dy;
                float t2 = dx0 * dx0;
                float s0 = t0 + t1;
                float len2 = s0 + t2;
                float invEye = 1.0f / std::sqrt(len2);
                for (int i = 0; i < positionCount_; ++i) {
                    float px = positions_[i][0], py = positions_[i][1], pz = positions_[i][2];
                    float dz = pz - target_[2];
                    float dx = px - target_[0];
                    float dyp = py - target_[1];
                    float q0 = dz * dz;
                    float q1 = dyp * dyp;
                    float q2 = dx * dx;
                    float q3 = q0 + q1;
                    float lenP2 = q3 + q2;
                    float invP = 1.0f / std::sqrt(lenP2);
                    // cosine between the two directions, accumulated exactly in the original order
                    float a0 = invP * dx;
                    float a1 = a0 * invEye;
                    float term1 = a1 * dx0;
                    float b0 = dyp * invP;
                    float b1 = b0 * dy;
                    float term2 = b1 * invEye;
                    float c0 = invP * dz;
                    float c1 = c0 * invEye;
                    float term3 = c1 * dz0;
                    float sum12 = term1 + term2;
                    float dot = sum12 + term3;
                    float cosine = std::fabs(dot);
                    float denominator = spread_ - cosine;
                    float inverse = 1.0f / denominator;
                    float weight = inverse * cosine;
                    weightSum = weightSum + weight;
                    float wx = px * weight;
                    sumX = sumX + wx;
                    float wy = py * weight;
                    sumY = sumY + wy;
                    float wz = pz * weight;
                    sumZ = sumZ + wz;
                }
            }
            float invWeight = 1.0f / weightSum;
            average_[0] = sumX * invWeight;
            average_[1] = sumY * invWeight;
            average_[2] = sumZ * invWeight;
        }
    }
    UpdateAmbienceScores();
    UpdateAmbienceLoops(elapsedMs);
}

// ---------------------------------------------------------------------------------------------
// parameters -> sound system
// ---------------------------------------------------------------------------------------------

void cSC4AudioAmbienceLayer::UpdateFreshnessParameters() {   // @ 0x10040e49a
    int parameter;
    if (!enabled_) {
        scoreVolume_ = 0;
        parameter = 0;
    } else {
        float v = volumeCurve_.GetOutputValue(amount_);
        scoreVolume_ = static_cast<std::int32_t>((static_cast<std::uint32_t>(Trunc32(v)) * scoreMasterVolume_) >> 10);
        float p = parameterCurve_.GetOutputValue(amount_);
        parameter = Trunc32(p);
        scoreParameter_ = parameter;
        if (quality_ == 1) parameter = (parameter * 0x4b) / 100;
        else if (quality_ == 0) parameter = parameter / 2;
        else return;
    }
    scoreParameter_ = parameter;
}

void cSC4AudioAmbienceLayer::UpdateLoopParameters() {   // @ 0x10040e548
    loopVolume_ = 0;
    if (enabled_) {
        if (amount_ != 0.0f || std::isnan(amount_)) loopVolume_ = Trunc32(loopCurve_.GetOutputValue(amount_));
        else loopVolume_ = 0;
    }
}

void cSC4AudioAmbienceLayer::UpdateAmbienceScores() {   // @ 0x10040d3ae
    UpdateFreshnessParameters();
    for (tFreshnessScore& s : scores_) {
        auto* player = static_cast<cIFreshPlayer*>(s.player);
        bool stopBranch = (scoreVolume_ == 0 || scoreParameter_ == 0);
        bool continueToUpdate = false;     // reaches LAB_10040d77b
        if (!stopBranch) {
            if (!s.active && s.pending && !paused_) {
                s.parameter = 0;
                s.volume = 0;
                float volume = (static_cast<float>(masterPercent_) / 100.0f) * ZoomDistances()[4];
                AmbienceSndEvent start;
                start.eventId = kEventScoreStart;
                start.unknown = s.player;
                start.has[0] = start.has[1] = true;
                start.data[0] = FloatToU64(volume);
                float volume16 = volume * 16.0f;
                start.data[1] = FloatToU64(volume16);
                PostEvent(start);
                AmbienceSndEvent mode;
                mode.eventId = kEventScoreStartMode;
                mode.unknown = s.player;
                mode.has[0] = true;
                mode.data[0] = mode_ == 4 ? 1 : 0;
                if (PostEvent(mode)) s.active = true;
            }
            if (scoreVolume_ == 0 || scoreParameter_ == 0 || paused_) stopBranch = true;
            else continueToUpdate = true;
        }
        if (stopBranch && s.active) {
            AmbienceSndEvent stop;
            stop.eventId = kEventScoreStop;
            stop.unknown = s.player;
            if (PostEvent(stop)) s.active = false;
            else continueToUpdate = true;       // quirk: a failed stop falls into the update code below
        }
        if (!continueToUpdate || !s.active) continue;
        if (mode_ != 4) {
            int n = positionCount_;
            int i = 0;
            for (; i < n; ++i) {
                std::uint32_t column = static_cast<std::uint32_t>(i) % 3, row = static_cast<std::uint32_t>(i) / 3;
                player->SetDivision3DPosition(column, row, Trunc32(positions_[i][0]), Trunc32(positions_[i][1]), Trunc32(positions_[i][2]));
                player->SetDivisionEnabled(column, row, true);
            }
            if (n < 1) i = 0;
            if (i <= 8) {
                for (; i < 9; ++i) player->SetDivisionEnabled(static_cast<std::uint32_t>(i) % 3, static_cast<std::uint32_t>(i) / 3, false);
            }
        }
        if (s.volume != scoreVolume_) {
            s.volume = scoreVolume_;
            AmbienceSndEvent e;
            e.eventId = kEventScoreVolume;
            e.unknown = s.player;
            e.has[0] = true;
            e.data[0] = SignExtend(s.volume);
            PostEvent(e);
        }
        if (s.parameter != scoreParameter_) {
            AmbienceSndEvent e;
            e.eventId = kEventScoreParameter;
            e.unknown = s.player;
            e.has[0] = true;
            e.data[0] = SignExtend(scoreParameter_);
            PostEvent(e);
            s.parameter = scoreParameter_;
        }
    }
}

void cSC4AudioAmbienceLayer::UpdateAmbienceLoops(std::uint32_t elapsedMs) {   // @ 0x10040d958
    UpdateLoopParameters();
    IAmbienceLayerHost* host = AmbienceLayerHost();
    for (tAmbienceLoop& loop : loops_) {
        bool stop = (loopVolume_ == 0);
        if (!stop && loop.instanceId == 0) {
            ease_.SetPosition(average_, true);
            ease_.SetVolume(loopVolume_, true);
            loop.x = loop.y = loop.z = 0.0f;       // the two 8-byte stores also clear the last volume
            loop.volume = 0;
            loop.instanceId = host->GenerateNewInstanceId();
            AmbienceSndEvent start;
            start.eventId = kEventStartSound;
            start.ctor[0] = loop.soundId;
            start.ctor[1] = loop.instanceId;
            start.ctor[2] = 1;
            start.ctor[3] = static_cast<std::uint32_t>(loopVolume_);
            PostEvent(start);
        }
        if (stop && loop.instanceId != 0) {
            AmbienceSndEvent e;
            e.eventId = kEventStopSound;
            e.ctor[0] = loop.instanceId;
            PostEvent(e);
            loop.instanceId = 0;
        }
        ease_.SetPosition(average_, false);
        ease_.GetPosition(average_);       // the eased position replaces the average position
        ease_.SetVolume(loopVolume_, false);
        int volume = ease_.GetVolume();
        loopVolume_ = volume;
        if (loop.volume != volume) {
            loop.volume = volume;
            if (loop.instanceId != 0) {
                AmbienceSndEvent e;
                e.eventId = kEventSetSoundVolume;
                e.ctor[0] = loop.instanceId;
                e.ctor[1] = static_cast<std::uint32_t>(volume);
                PostEvent(e);
            }
        }
        float dx = loop.x - average_[0];
        float dy = loop.y - average_[1];
        float dz = loop.z - average_[2];
        if ((std::fabs(dx) >= 0.0001f || std::fabs(dy) >= 0.0001f || std::fabs(dz) >= 0.0001f) && loop.instanceId != 0) {
            AmbienceSndEvent e;
            e.eventId = kEventSetSoundPosition;
            e.ctor[0] = loop.instanceId;
            e.ctor[1] = Trunc64Low32(average_[0]);   // (long) conversion, low 32 bits
            e.ctor[2] = Trunc64Low32(average_[1]);
            e.ctor[3] = Trunc64Low32(average_[2]);
            PostEvent(e);
            loop.x = average_[0];
            loop.y = average_[1];
            loop.z = average_[2];
        }
    }
    ease_.Update(static_cast<float>(elapsedMs));
}

}  // namespace SC4Audio

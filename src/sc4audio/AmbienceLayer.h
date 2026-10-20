// cSC4AudioAmbienceLayer — one "ambience layer" of the SimCity 4 ambient sound engine (functions
// 0x10040b266..0x10040f069). A layer is described by a tuning exemplar (type 0x6534284a, group
// 0xca25875d, instance = layer id): three lists of filters decide which cells / occupants belong to
// the layer ("all of" list 1, "none of" list 2, "any of" list 3), the matching cells / occupants are
// clustered by cGroupLayers, and every Update turns the clusters into
//   * an "amount" (cluster count, residential population, zoom, industrial population ...),
//   * up to nine representative positions and their weighted average (the ambience position),
//   * freshness-score parameters (volume / zoom of a cFreshPlayer score per layer),
//   * looping sounds started, moved, re-volumed and stopped through the sound manager.
//
// Object layout (vtable 0x100aacff0, cIGZMessageTarget2 base; class id of the layer interface 0x4a2835fe):
//   +0x08 initialised, +0x0c refcount, +0x10 enabled, +0x11 paused, +0x14 update rect (x0,z0,x1,z1),
//   +0x24 occupant group radius, +0x28 cell group radius, +0x30 occupant layer, +0x38 cell layer,
//   +0x40 intersection layer, +0x48 combine mode (1 occupants/cells share a layer, 2/3 intersect),
//   +0x4c amount source, +0x50 nine positions, +0xbc position count, +0xc0 amount, +0xc4 listener eye,
//   +0xd0 listener target, +0xdc average position, +0xf0 score volume, +0xf4 score parameter,
//   +0xf8 loop volume, +0xfc cEaseSound, +0x138/+0x13c accepted occupants/cells, +0x140 CanHandle result,
//   +0x141 CanHandle computed, +0x144/+0x148 rejected occupants/cells, +0x14c/+0x150/+0x154 number of
//   cell/occupant/state filters, +0x158/+0x170/+0x188 per-filter "was true" bit vectors,
//   +0x1a0 freshness score list (node 0x30), +0x1b8 ambience loop list (node 0x28), +0x1d0 name,
//   +0x200/+0x220/+0x240 response curves (score parameter / score volume / loop volume),
//   +0x260 freshness loop id, +0x268 score keys (group,instance pairs), +0x280/+0x281 flags,
//   +0x288 score master volume, +0x28c, +0x290 master percent, +0x298 layer instance id, +0x29c layer id,
//   +0x2a0 position spread, +0x2a4 audio quality, +0x2a8/+0x2e0/+0x318 filter id lists, +0x2c0/+0x2f8/+0x330
//   filter pointer lists with counts at +0x2d8/+0x310/+0x348.
//
// Quirks kept: HandleCell/HandleOccupant always return false; Init leaves the "initialised" flag set when
// the static init fails; _ReadTuningValues clears the default "Noname_0x%08x" name before reading the
// exemplar; RemoveFreshnessScore releases the score object twice; DoMessage toggles Enable twice (net
// no-op); Update recomputes the enabled flag from CanHandle (the per-filter bit loops before it are
// overwritten); error messages are built and thrown away in the original (routed to LogError here).
#pragma once
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio/interfaces/cIGZSndSys.h"
#include "gz/StreamOps.h"
#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZMessage2.h"
#include "rz/cRZString.h"
#include "sc4/AmbienceCellFilters.h"
#include "sc4/AmbienceOccupantFilters.h"
#include "sc4/AmbienceStateFilters.h"
#include "sc4/GroupLayer.h"
#include "sc4/QuadGrid.h"
#include "sc4/SCProperty.h"
#include "sc4/cSC4ResponseCurve.h"
#include "sc4audio/EaseSound.h"
#include "sc4audio/SC4AudioHost.h"

namespace SC4Audio {

constexpr std::uint32_t kGZIID_cISC4AudioAmbienceLayer = 0x4a2835fe;
constexpr std::uint32_t kGZIID_cIAmbienceFilterBase = 0x4a40074c;
constexpr std::uint32_t kTuningType = 0x6534284a, kTuningGroup = 0xca25875d;   // layer tuning exemplar key
constexpr std::uint32_t kClsidFreshPlayerObject = 0x3c1cf59e, kIidFreshPlayerObject = 0xdc1cf595;
constexpr std::uint32_t kScoreResourceType = 0x2026960b;

// Sound-system events the layer posts for its freshness scores (class 0x5b523ba4 = cGZSndEventStd).
constexpr std::uint32_t kEventScoreAdd = 0x2a2c0384, kEventScoreRemove = 0x4a2c0389, kEventScoreStop = 0x2a2c0393,
                        kEventScoreStart = 0xea2d827c, kEventScoreStartMode = 0xea2c0390,
                        kEventScoreVolume = 0x4a3da208, kEventScoreParameter = 0x0a414c5c;

// One sound-system event: the four constructor payload integers (EventCreateWithData) plus the optional
// message payload set afterwards (SetIGZUnknown / SetData1..3).
struct AmbienceSndEvent {
    std::uint32_t eventId = 0;
    std::uint32_t ctor[4] = {0, 0, 0, 0};
    cIGZUnknown* unknown = nullptr;
    bool has[3] = {false, false, false};
    std::uint64_t data[3] = {0, 0, 0};
};

// m_pSndSys as the layer uses it: create the event, fill it, post it (vslots 0x70, message 0x108/0x58/0x80/0xa8, 0x48).
class IAmbienceSndSink {
public:
    virtual ~IAmbienceSndSink() = default;
    virtual bool PostEvent(const AmbienceSndEvent& event) = 0;   // false when the event could not be created
};

// The production sink over the sound system service 0x23cecba0.
class cAmbienceSndSysAdapter final : public IAmbienceSndSink {
public:
    explicit cAmbienceSndSysAdapter(cIGZSndSys* system) : system_(system) {}
    bool PostEvent(const AmbienceSndEvent& event) override;
private:
    cIGZSndSys* system_;
};

// Everything the layer reaches through SL:: globals / framework services (see _StaticInit @ 0x10040b34a).
class IAmbienceLayerHost {
public:
    virtual ~IAmbienceLayerHost() = default;
    virtual bool HasResourceManager() = 0;                    // SL::spRM != null (static init needs it)
    virtual sc4::cISCPropertyHolder* GetTuningExemplar(const cGZPersistResourceKey& key) = 0;   // spRM->GetResource: AddRef'd holder or null
    virtual IAmbienceSndSink* AcquireSoundSink() = 0;         // sound system service; null fails the static init
    virtual bool CreateObject(std::uint32_t classId, std::uint32_t iid, void** out) = 0;   // GZCOM()->CreateInstance
    virtual int RenderAudioQuality() = 0;                     // SL::spRenderProperties+0x30 -> +0x590 (0 low, 1 medium)
    virtual bool CellToWorld(int cellX, int cellZ, float& x, float& z) = 0;   // SL::spCity vslot 0x308
    virtual float TerrainAltitude(float x, float z) = 0;      // SL::spTerrain vslot 0x60
    virtual int ResidentialPopulation() = 0;                  // SL::spResidentialSim vslot 0x80 (0 when there is no sim)
    virtual std::uint32_t IndustrialPopulation() = 0;         // SFX::AmbienceManager()->GetIndustrialPopulation()
    virtual std::uint32_t GenerateNewInstanceId() = 0;        // cSC4AudioSoundManager::GenerateNewInstanceId
    virtual void LogError(const char* /*message*/) {}         // the original formats these and drops them
};
IAmbienceLayerHost* AmbienceLayerHost();
void SetAmbienceLayerHost(IAmbienceLayerHost* host);

// Common head of the three filter interfaces (iid 0x4a40074c): Init [3], Shutdown [4].
class cIAmbienceFilterBase : public cIGZUnknown {
public:
    virtual bool Init() = 0;
    virtual bool Shutdown() = 0;
};

// tFreshnessScore (list node payload at +0x10, 0x20 bytes)
struct tFreshnessScore {
    bool active = false;                 // +0x10: the score is playing
    bool pending = true;                 // +0x11: start it when allowed (set when added)
    std::int32_t parameter = 0;          // +0x14: last sent parameter (score param 0xf4)
    std::int32_t volume = 0;             // +0x18: last sent volume (0xf0)
    cGZPersistResourceKey key;           // +0x1c
    cIGZUnknown* player = nullptr;       // +0x28 cIFreshPlayer (owned reference)
};

// tAmbienceLoop (list node payload at +0x10)
struct tAmbienceLoop {
    std::uint32_t soundId = 0;           // +0x10
    std::uint32_t instanceId = 0;        // +0x14 (0 = not playing)
    float x = 0, y = 0, z = 0;           // +0x18 last sent position
    std::int32_t volume = 0;             // +0x24 last sent volume
};

// vslots 3.. of the layer interface (vtable 0x100aacff0)
class cISC4AudioAmbienceLayer : public cIGZMessageTarget2 {
public:
    virtual bool Init() = 0;                                              // [4]  +0x20
    virtual bool Shutdown() = 0;                                          // [5]  +0x28
    virtual const cRZString& GetName() = 0;                               // [6]  +0x30
    virtual void SetName(const cIGZString& name) = 0;                     // [7]  +0x38
    virtual void SetLayerId(std::uint32_t id) = 0;                        // [8]  +0x40
    virtual std::uint32_t GetLayerId() = 0;                               // [9]  +0x48
    virtual void GetAveragePosition(cS3DVector3& out) = 0;                // [10] +0x50
    virtual void Enable(bool enable) = 0;                                 // [11] +0x58
    virtual bool AddAmbienceLoop(std::uint32_t soundId) = 0;              // [12] +0x60
    virtual bool RemoveAmbienceLoop(std::uint32_t soundId) = 0;           // [13] +0x68
    virtual void LoadFreshnessScore() = 0;                                // [14] +0x70
    virtual bool AddFreshnessScore(const cGZPersistResourceKey& key) = 0; // [15] +0x78
    virtual bool RemoveFreshnessScore(std::uint32_t instance) = 0;        // [16] +0x80
    virtual void SetScoreMasterVolume(std::uint32_t volume) = 0;          // [17] +0x88
    virtual bool CanHandleOccupant(sc4::cISC4Occupant* occupant) = 0;     // [18] +0x90
    virtual bool CanHandle() = 0;                                         // [19] +0x98
    virtual bool HandleCell(int x, int z) = 0;                            // [20] +0xa0
    virtual bool HandleOccupant(sc4::cISC4Occupant* occupant) = 0;        // [21] +0xa8
    virtual bool InsertOccupant(sc4::cISC4Occupant* occupant) = 0;        // [22] +0xb0
    virtual bool MoveOccupant(sc4::cISC4Occupant* occupant) = 0;          // [23] +0xb8
    virtual bool RemoveOccupant(sc4::cISC4Occupant* occupant) = 0;        // [24] +0xc0
    virtual void BeginUpdate(const SC4RectFloat& rect) = 0;               // [25] +0xc8
    virtual void Update(std::uint32_t elapsedMs, const cS3DVector3& eye, const cS3DVector3& target) = 0;   // [26] +0xd0
    virtual void EndUpdate() = 0;                                         // [27] +0xd8
    virtual void Pause(bool pause) = 0;                                   // [28] +0xe0
};

class cSC4AudioAmbienceLayer final : public cISC4AudioAmbienceLayer {
public:
    explicit cSC4AudioAmbienceLayer(std::uint32_t layerInstanceId);       // @ 0x10040ea7a (+ wrapper 0x10040edfc)
    static cSC4AudioAmbienceLayer* Create(std::uint32_t id) { return new cSC4AudioAmbienceLayer(id); }

    bool QueryInterface(std::uint32_t iid, void** out) override;          // @ 0x10040e41c
    std::uint32_t AddRef() override;                                      // @ 0x10040e43e
    std::uint32_t Release() override;                                     // @ 0x10040e44c
    bool DoMessage(cIGZMessage2* message) override;                       // @ 0x10040e39a
    bool Init() override;                                                 // @ 0x10040b266
    bool Shutdown() override;                                             // @ 0x10040bd2c
    const cRZString& GetName() override { return name_; }                 // @ 0x10040eff6
    void SetName(const cIGZString& name) override;                        // @ 0x10040f004
    void SetLayerId(std::uint32_t id) override { layerId_ = id; }         // @ 0x10040f02a
    std::uint32_t GetLayerId() override { return layerId_; }              // @ 0x10040f036
    void GetAveragePosition(cS3DVector3& out) override;                   // @ 0x10040f042
    void Enable(bool enable) override { enabled_ = enable; }              // @ 0x10040c272
    bool AddAmbienceLoop(std::uint32_t soundId) override;                 // @ 0x10040e2be
    bool RemoveAmbienceLoop(std::uint32_t soundId) override;              // @ 0x10040e34a (+ tail 0x10040e372)
    void LoadFreshnessScore() override;                                   // @ 0x10040dd8e
    bool AddFreshnessScore(const cGZPersistResourceKey& key) override;    // @ 0x10040de80
    bool RemoveFreshnessScore(std::uint32_t instance) override;           // @ 0x10040e140
    void SetScoreMasterVolume(std::uint32_t volume) override;             // @ 0x10040e288
    bool CanHandleOccupant(sc4::cISC4Occupant*) override { return false; }   // @ 0x10040f062
    bool CanHandle() override;                                            // @ 0x10040c27c
    bool HandleCell(int x, int z) override;                               // @ 0x10040c4fa
    bool HandleOccupant(sc4::cISC4Occupant* occupant) override;           // @ 0x10040c82e
    bool InsertOccupant(sc4::cISC4Occupant* occupant) override;           // @ 0x10040caec
    bool MoveOccupant(sc4::cISC4Occupant* occupant) override;             // @ 0x10040cb7c
    bool RemoveOccupant(sc4::cISC4Occupant* occupant) override;           // @ 0x10040cbfa
    void BeginUpdate(const SC4RectFloat& rect) override;                  // @ 0x10040cc36
    void Update(std::uint32_t elapsedMs, const cS3DVector3& eye, const cS3DVector3& target) override;   // @ 0x10040cd3c
    void EndUpdate() override;                                            // @ 0x10040dd56
    void Pause(bool pause) override;                                      // @ 0x10040e298

    // --- statics (m_nStaticInit, m_pSndSys, m_pResManager, m_AmbienceFilterMap) ---
    static bool StaticInit();            // @ 0x10040b34a
    static void StaticShutdown();        // @ 0x10040c22a
    static void StaticRemoveAllFilters();   // @ 0x10040e5a0
    static int StaticInitCount();
    static std::size_t SharedFilterCount();

    // --- inspection for tests ---
    bool IsInitialised() const { return initialised_; }
    bool IsEnabled() const { return enabled_; }
    bool IsPaused() const { return paused_; }
    int PositionCount() const { return positionCount_; }
    const float* Position(int i) const { return positions_[i]; }
    float Amount() const { return amount_; }
    int ScoreVolume() const { return scoreVolume_; }
    int ScoreParameter() const { return scoreParameter_; }
    int LoopVolume() const { return loopVolume_; }
    int AcceptedOccupants() const { return occupantsAccepted_; }
    int AcceptedCells() const { return cellsAccepted_; }
    int RejectedOccupants() const { return occupantsRejected_; }
    int RejectedCells() const { return cellsRejected_; }
    const std::list<tFreshnessScore>& Scores() const { return scores_; }
    const std::list<tAmbienceLoop>& Loops() const { return loops_; }
    int CurveQuality() const { return quality_; }
    int Mode() const { return mode_; }
    std::uint32_t FreshnessLoopId() const { return freshnessLoopId_; }
    cGroupLayer* OccupantLayer() const { return occupantLayer_; }
    cGroupLayer* CellLayer() const { return cellLayer_; }
    cGroupLayer* IntersectLayer() const { return intersectLayer_; }
    std::uint32_t ScoreMasterVolume() const { return scoreMasterVolume_; }
    float GroupRadiusA() const { return radiusA_; }
    float GroupRadiusB() const { return radiusB_; }

    bool ReadTuningValues(std::uint32_t id);   // @ 0x10040b438
    void CreateFilters();                      // @ 0x10040ba4c
    void CreateLayers();                       // @ 0x10040bb98
    void RemoveLayers();                       // @ 0x10040bddc
    void RemoveAllFilters();                   // @ 0x10040be6e
    bool RemoveAllFreshnessScores();           // @ 0x10040c036
    bool RemoveAllAmbienceLoops();             // @ 0x10040c13e

private:
    ~cSC4AudioAmbienceLayer();
    void AddFilter(std::uint32_t filterId, int kind);       // @ 0x10040e69c
    void GetEquallyDistributedPositions(const std::list<cS3DVector3>& list, int count, float (*out)[3], int& outCount);   // @ 0x10040d1f6
    void UpdateAmbienceScores();                            // @ 0x10040d3ae
    void UpdateAmbienceLoops(std::uint32_t elapsedMs);      // @ 0x10040d958
    void UpdateFreshnessParameters();                       // @ 0x10040e49a
    void UpdateLoopParameters();                            // @ 0x10040e548
    bool PostEvent(const AmbienceSndEvent& e);

    struct FilterEval {
        int counted = 0;            // filters that answered the interface query and were true / counted
        bool rejected = false;      // an "all of" / "none of" filter failed
        std::size_t orIndex = 0;    // index where the "any of" scan stopped (== list size when none matched)
    };
    template <class Iface, class F> FilterEval EvaluateFilters(std::uint32_t iid, F&& isTrue);

    bool initialised_ = false;           // +0x08
    std::uint32_t refCount_ = 0;         // +0x0c
    bool enabled_ = true;                // +0x10
    bool paused_ = false;                // +0x11
    SC4RectFloat rect_;                  // +0x14
    float radiusA_ = 50.0f;              // +0x24
    float radiusB_ = 50.0f;              // +0x28
    cGroupLayer* occupantLayer_ = nullptr;   // +0x30
    cGroupLayer* cellLayer_ = nullptr;       // +0x38
    cGroupLayer* intersectLayer_ = nullptr;  // +0x40
    std::int32_t mode_ = 1;              // +0x48
    std::int32_t amountSource_ = 5;      // +0x4c
    float positions_[9][3] = {};         // +0x50
    std::int32_t positionCount_ = 0;     // +0xbc
    float amount_ = 0;                   // +0xc0
    float eye_[3] = {0, 0, 0};           // +0xc4
    float target_[3] = {0, 0, 0};        // +0xd0
    float average_[3] = {0, 0, 0};       // +0xdc
    std::int32_t scoreVolume_ = 0;       // +0xf0
    std::int32_t scoreParameter_ = 0;    // +0xf4
    std::int32_t loopVolume_ = 0;        // +0xf8
    cEaseSound ease_;                    // +0xfc
    std::int32_t occupantsAccepted_ = 0, cellsAccepted_ = 0;    // +0x138, +0x13c
    bool canHandleResult_ = false, canHandleComputed_ = false;  // +0x140, +0x141
    std::int32_t occupantsRejected_ = 0, cellsRejected_ = 0;    // +0x144, +0x148
    std::int32_t cellFilterKinds_ = 0, occupantFilterKinds_ = 0, stateFilterKinds_ = 0;   // +0x14c.. (filters answering each interface)
    std::vector<bool> bits1_, bits2_, bits3_;       // +0x158, +0x170, +0x188
    std::list<tFreshnessScore> scores_;             // +0x1a0
    std::list<tAmbienceLoop> loops_;                // +0x1b8
    cRZString name_;                                // +0x1d0
    cSC4ResponseCurve parameterCurve_, volumeCurve_, loopCurve_;   // +0x200, +0x220, +0x240
    std::uint32_t freshnessLoopId_ = 0;             // +0x260
    std::vector<std::int64_t> scoreKeys_;           // +0x268 (group | instance << 32)
    bool flag280_ = false, flag281_ = false;        // +0x280, +0x281
    std::uint32_t scoreMasterVolume_ = 0;           // +0x288
    std::uint32_t value28c_ = 0;                    // +0x28c
    std::uint32_t masterPercent_ = 0;               // +0x290
    std::uint32_t instanceId_ = 0;                  // +0x298
    std::uint32_t layerId_ = 0;                     // +0x29c
    float spread_ = 1.0f;                           // +0x2a0
    std::int32_t quality_ = 0;                      // +0x2a4
    std::vector<std::uint32_t> filterIds_[3];       // +0x2a8, +0x2e0, +0x318
    std::vector<cIAmbienceFilterBase*> filters_[3]; // +0x2c0, +0x2f8, +0x330
};

}  // namespace SC4Audio

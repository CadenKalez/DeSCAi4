// cSC4AudioAEMSEventSystem — the game-side wrapper of the AEMS audio event system (28 functions at
// 0x1007b56a0..0x1007b6aa0, service id 0xac1fae31, priority INT32_MIN). AEMS (Audio Event Management
// System) is the Miles audio-event engine: the game starts and stops numbered events (fire, disaster,
// UI, ambient triggers, vehicle loops), loads sound banks and drives three global event parameters
// (velocity/throttle/wobble). Everything below the wrapper is the engine's C API
// (_SNDSYS_* / _SNDAEMS_*, recovered here as the IAemsEngineHost seam).
//
// The system owns:
//   - a 200,000-byte memory block the engine runs in (vector at +0x58),
//   - the engine AEMS-system blob loaded from resource {0, 0x8a5971c5, 0x6b955c37} (vector at +0x1a0),
//   - the loaded source banks (list at +0x1b8, one counted reference each),
//   - one state slot per AEMS event index (vector at +0x40, 21 entries; StartSound stores
//     engineHandle + 1, 0 means "not playing", EndSound passes engineHandle = state - 1),
//   - the master volume: the player's audio-option volume (+0x1d0, volume / 1024), the per-zoom-level
//     volume (+0x1d4), a tuning multiplier (+0x170) and a response curve (+0x180), applied in
//     UpdateVolume as  curve(volume * zoom * tuning) * 127  ->  _SNDmastervol.
//
// Bases / original vtable groups: cIGZSndEventHandler +0 (iid 0xbb523bb2), cISC4AEMSEvents +8 (iid
// 0x8c1faeb7), cIGZMessageTarget2 +0x10 (iid 0x452294aa), cRZSystemService +0x18 (iid 1 lands on its
// cRZUnknown subobject at +0x20). Original object size 0x1f0; the port's is 0x1e8 because its
// cSC4ResponseCurve is 0x18 bytes instead of the original's 0x20 (all later members sit 8 lower).
//
// Member offsets (original): +0x40 eventStates, +0x58 soundSystemMemory, +0x70 the engine options
// block (0x100 bytes - the size is inferred from the tuning values at +0x170, which are read after
// _SNDSYS_getopts; the original never initialises it, the port zero-fills it), +0x170
// tuning volume (float), +0x174 bus volume (float), +0x178 bus flags (uint32, default 0x25), +0x180
// response curve, +0x1a0 system blob, +0x1b8 bank list, +0x1d0 master volume scale, +0x1d4 zoom
// volume scale, +0x1d8 zoom-level volumes (vector<float>, 5 defaults from kAEMSZoomLevelVolumes).
//
// Port deviations: the SNDSYS/AEMS C API sits behind the IAemsEngineHost seam (no host = Init fails
// like a SNDSYSI_init error), the unrecovered cSC4ResponseCurve::SetResponseCurveData(key, property)
// @ 0x1000ac47e is inlined as a property read, and a null holder/viewport, an out-of-range zoom
// level or an out-of-range event index is ignored instead of dereferenced.
//
// Quirks kept: Init/Shutdown guard the whole object with one process-wide flag (a second instance's
// Init does nothing and reports success); DoEvent always returns false even when it handled the
// master-volume event; StartSound reports success even when the engine refused to begin the event
// (the slot then stays 0); StopAllSounds skips indices 0x0f and 0x10; the destructor clears the bank
// list without releasing the banks or removing their module banks (Shutdown does that).
#pragma once
#include <array>
#include <cstdint>
#include <list>
#include <vector>

#include "audio/interfaces/cIGZSndEventStd.h"
#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZMessage2.h"
#include "rz/cRZSystemService.h"
#include "sc4/cSC4ResponseCurve.h"
#include "sc4audio/AEMSEvents.h"
#include "sc4audio/SC4AudioHost.h"

class cIGZFile;

namespace sc4 {
class cSC4AudioAEMSSoundBank;
}

namespace SC4Audio {

// The Miles-style sound-system C API the AEMS code calls directly in the original:
//   _SNDSYSI_init(void* memory, int size, int flags)                 -> int (<0 = failure)
//   _SNDSYS_getopts(void* options) / _SNDSYS_setopts(void* options) -> int (<0 = failure)
//   _SNDfxinitbus(int bus, int volume, int flags, int, int)          -> int (<0 = failure)
//   _SNDmastervol(int volume)                                        -> void
//   _SNDSYS_entercritical() / _SNDSYS_leavecritical() / _SNDSYS_restore()
//   _SNDAEMS_addsystem(void* data) / _SNDAEMS_removesystem()
//   _SNDAEMS_addmodulebank(void* data, int, int, void* (*malloc)(void*, int, int))
//   _SNDAEMS_removemodulebank(int handle)
//   _SNDAEMS_beginevent(int* event) -> int handle (<0 = failure)
//   _SNDAEMS_endevent(int handle, int* event)
//   _SNDAEMS_setglobal(int index, int value) -> int (<0 = failure)
class IAemsEngineHost {
public:
    virtual ~IAemsEngineHost() = default;
    virtual int SndSysInit(void* memory, std::int32_t size, std::int32_t flags) = 0;
    virtual int SndSysGetOpts(void* options) = 0;
    virtual int SndSysSetOpts(void* options) = 0;
    virtual int SndFxInitBus(std::int32_t bus, std::int32_t volume, std::int32_t flags,
                             std::int32_t arg4, std::int32_t arg5) = 0;
    virtual void SndMasterVol(std::int32_t volume) = 0;
    virtual void SndSysEnterCritical() = 0;
    virtual void SndSysLeaveCritical() = 0;
    virtual void SndSysRestore() = 0;
    virtual int SndAemsAddSystem(void* data) = 0;
    virtual void SndAemsRemoveSystem() = 0;
    virtual int SndAemsAddModuleBank(void* data, std::int32_t a, std::int32_t b,
                                     void* (*alloc)(void*, int, int)) = 0;
    virtual void SndAemsRemoveModuleBank(std::int32_t handle) = 0;
    virtual int SndAemsBeginEvent(const std::int32_t* event) = 0;
    virtual void SndAemsEndEvent(std::int32_t handle, const std::int32_t* event) = 0;
    virtual int SndAemsSetGlobal(std::int32_t index, std::int32_t value) = 0;
};
// The engine, or null when the port runs without one (then Init fails like a SNDSYSI_init error).
IAemsEngineHost* AemsEngineHost();
void SetAemsEngineHost(IAemsEngineHost* host);

}  // namespace SC4Audio

class cSC4AudioAEMSEventSystem final : public cIGZSndEventHandler,
                                       public cISC4AEMSEvents,
                                       public cIGZMessageTarget2,
                                       public cRZSystemService {
public:
    static constexpr std::uint32_t kServiceId = 0xac1fae31;   // the id in cRZSystemService
    static constexpr std::int32_t kServicePriority = static_cast<std::int32_t>(0x80000000u);

    cSC4AudioAEMSEventSystem();          // @ 0x1007b56a0 (complete object) / 0x1007b58ec (base object)
    ~cSC4AudioAEMSEventSystem() override;   // @ 0x1007b58f6 (complete) / 0x1007b59fa (deleting)

    // --- cIGZUnknown (one implementation for all four bases) ---
    bool QueryInterface(std::uint32_t iid, void** out) override;      // @ 0x1007b6694
    std::uint32_t AddRef() override;                                  // @ 0x1007b6a22
    std::uint32_t Release() override;                                 // @ 0x1007b6a30

    // --- cIGZSndEventHandler (+0) ---
    bool DoEvent(cIGZSndEventStd* event) override;                    // @ 0x1007b65ca

    // --- cIGZMessageTarget2 (+0x10) ---
    bool DoMessage(cIGZMessage2* message) override;                   // @ 0x1007b663a

    // --- cISC4AEMSEvents (+8) ---
    void LoadSoundBank(const sc4::cISCPropertyHolder* holder, std::uint32_t propertyId) override;    // @ 0x1007b670e
    void UnloadSoundBank(const sc4::cISCPropertyHolder* holder, std::uint32_t propertyId) override;  // @ 0x1007b6798
    bool StartSound(AEMSEventType event) override;                    // @ 0x1007b680a
    bool EndSound(AEMSEventType event) override;                      // @ 0x1007b687e
    bool SetParamVelocity(sc4::SC4Percentage percentage) override;    // @ 0x1007b68ee
    bool SetParamThrottle(sc4::SC4Percentage percentage) override;    // @ 0x1007b694a
    bool SetParamWobble(sc4::SC4Percentage percentage) override;      // @ 0x1007b69aa

    // --- cIGZSystemService (+0x18) ---
    bool Init() override;                                             // @ 0x1007b5a44
    bool Shutdown() override;                                         // @ 0x1007b5ff6

    // --- system blob / banks ---
    bool LoadFromResource(cGZPersistResourceKey& key);                // @ 0x1007b5f64
    bool LoadFromFile(cIGZFile* file);                                // @ 0x1007b6516
    bool LoadBankFromResource(cGZPersistResourceKey& key);            // @ 0x1007b62e8
    bool AddBank(sc4::cSC4AudioAEMSSoundBank* bank);                       // @ 0x1007b6378
    bool RemoveBank(std::uint32_t resourceInstance);                  // @ 0x1007b6414
    void RemoveAllBanks();                                            // @ 0x1007b621c

    // --- engine side ---
    void StopAllSounds();                                             // @ 0x1007b6106
    bool ReadTuningValues();                                          // @ 0x1007b5c5c
    void UpdateVolume();                                              // @ 0x1007b5f2a

    // --- inspection (port additions for the test) ---
    std::size_t eventStateCount() const { return eventStates_.size(); }
    std::int32_t eventState(std::size_t index) const { return eventStates_[index]; }
    std::size_t bankCount() const { return banks_.size(); }
    const std::list<sc4::cSC4AudioAEMSSoundBank*>& banks() const { return banks_; }
    std::size_t soundSystemMemorySize() const { return soundSystemMemory_.size(); }
    std::uint8_t* soundSystemMemory() { return soundSystemMemory_.data(); }
    std::size_t systemDataSize() const { return systemData_.size(); }
    const std::uint8_t* systemData() const { return systemData_.data(); }
    const std::uint8_t* sndSysOptions() const { return sndSysOptions_.data(); }
    static constexpr std::size_t sndSysOptionsSize() { return 0x100; }
    float masterVolumeScale() const { return masterVolumeScale_; }
    float zoomVolumeScale() const { return zoomVolumeScale_; }
    float tuningVolume() const { return tuningVolume_; }
    float tuningBusVolume() const { return tuningBusVolume_; }
    std::uint32_t tuningBusFlags() const { return tuningBusFlags_; }
    const std::vector<float>& zoomLevelVolumes() const { return zoomLevelVolumes_; }
    const cSC4ResponseCurve& responseCurve() const { return responseCurve_; }
    static bool initialized() { return sbInitialized; }

private:
    static bool sbInitialized;                                  // 0x100b1acd0: one flag for the whole process
    std::vector<std::int32_t> eventStates_;                     // +0x40
    std::vector<std::uint8_t> soundSystemMemory_;               // +0x58 (200,000 bytes after Init)
    std::array<std::uint8_t, 0x100> sndSysOptions_{};           // +0x70 engine options block
    float tuningVolume_ = 1.0f;                                 // +0x170 property 0xcbfb03fa
    float tuningBusVolume_ = 1.0f;                              // +0x174 property 0xcbfb03ff
    std::uint32_t tuningBusFlags_ = 0x25;                       // +0x178 property 0xebfb0404
    cSC4ResponseCurve responseCurve_;                           // +0x180 (0x20 bytes in the original)
    std::vector<std::uint8_t> systemData_;                      // +0x1a0
    std::list<sc4::cSC4AudioAEMSSoundBank*> banks_;                  // +0x1b8 (one reference per bank)
    float masterVolumeScale_ = 1.0f;                            // +0x1d0
    float zoomVolumeScale_ = 1.0f;                              // +0x1d4
    std::vector<float> zoomLevelVolumes_;                       // +0x1d8
};

// Host services cHitMan reaches through the framework in the original (RZGetFramework() service
// lookups, GZCOM(), cRZFile, the persistence layer). The port injects them so the manager runs
// (and is tested) without the rest of the engine. Every method names the original call it replaces.
// The engine integration must implement this interface — see notes/audio-integration.md.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "audio/interfaces/cIGZSnd.h"
#include "audio/interfaces/cIGZSndUpdateTarget.h"
#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZAny.h"
#include "gz/cIGZString.h"

namespace Hit {

// One (property id, value) default written into a registered property-set resource.
struct DefaultProperty {
    std::uint32_t id;
    std::int32_t value;
};

class cIHitManServices {
public:
    virtual ~cIHitManServices() = default;

    // --- sound system (service 0x23cecba0, iid 0x106077d2) ---
    virtual bool HasSoundSystem() const = 0;
    // Sound-system slot +0x100: calls `callback(target, a, b)` for every registered update target.
    using EnumCallback = bool (*)(cIGZSndUpdateTarget* target, std::uint32_t a, void* b);
    virtual void EnumerateUpdateTargets(EnumCallback callback, std::uint32_t a, void* b) = 0;

    // --- persistence layer (service 0x56b906e, iid 0x656b8efc) ---
    // Slot +0x18: loads the persisted resource `key` and returns it as interface `iid`.
    virtual bool GetPersistResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out) = 0;
    // Creates a property-set resource holding `defaults` and registers it with the resource
    // manager under `key` (slot +0x38). Returns a reference the manager keeps until Shutdown.
    virtual cIGZUnknown* CreatePropertySetResource(const cGZPersistResourceKey& key,
                                                   const DefaultProperty* defaults, std::size_t count,
                                                   bool trackResource) = 0;
    // The bytes of a hit file stored as a persisted record, plus the name the original's file
    // adapter reports as its path (used as the data block's filename).
    virtual bool ReadPersistRecord(const cGZPersistResourceKey& key, std::vector<std::uint8_t>& bytes,
                                   std::string& name) = 0;

    // --- sound manager (service 0x1af85bfb / 0x3af85bb7) and COM ---
    // cIGZSndMgr slots +0x48 / +0x58: default class id of the 2D / 3D sound player.
    virtual bool GetDefaultSndPlayerClassId(bool positional3D, std::uint32_t& classId) = 0;
    // GZCOM()->CreateInstance(classId, iid, out) (slot +0x18); the object comes back AddRef'd.
    virtual bool CreateComInstance(std::uint32_t classId, std::uint32_t iid, void** out) = 0;

    // --- sound manager GetSound (slot +0x88) and the sound system's update targets / property cache ---
    virtual bool GetSoundById(std::uint32_t soundId, std::uint32_t iid, void** out) = 0;
    virtual void RegisterUpdateTarget(cIGZSndUpdateTarget* target, std::uint32_t intervalMs) = 0;  // SS +0xf0
    virtual void UnregisterUpdateTarget(cIGZSndUpdateTarget* target) = 0;                          // SS +0xf8
    virtual bool SetGlobalProperty(std::uint32_t id, const cIGZAny& value) = 0;                    // cache +0x78
    virtual bool GetGlobalProperty(std::uint32_t id, cIGZAny* value) = 0;                          // cache +0x58
    virtual bool SetInstanceProperty(std::uint32_t instance, std::uint32_t id, const cIGZAny& value) = 0;  // +0x70

    // --- plain files / directories ---
    virtual bool ReadFileBytes(const std::string& path, std::vector<std::uint8_t>& bytes) = 0;
    virtual std::string ApplicationDirectory() const = 0;  // RZ::ADir()
    virtual std::string DataDirectory() const = 0;         // RZ::DDir()

    // --- debug-info service (0xfbc9cd04 / 0x7bba0cbd), slot +0x48 ---
    virtual void ParseDebugInfo(const cIGZString& hitFileName, const std::uint8_t* data, std::uint32_t size) = 0;

    // --- object factories (the original news cHitTrackPlayer / cHitChannel directly) ---
    virtual bool CreateTrackPlayer(std::uint32_t iid, void** out) = 0;
    virtual bool CreateChannel(void** outSnd) = 0;  // a cIGZSnd, QueryInterface'd with iid 0x206077d2
};

cIHitManServices* GetServices();
void SetServices(cIHitManServices* services);

}  // namespace Hit

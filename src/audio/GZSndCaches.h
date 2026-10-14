// cGZSndPropertyCache and cGZSndInstanceCache. Recovered from Sim City 4 Deluxe Editionsub 1.2.1
// (property cache vtable 0x100ab4f40, ctor @ 0x10047a260, Create @ 0x10043cac2; instance cache
// vtable 0x100ab4e40, ctor @ 0x100478b2c, Create @ 0x10043cb32).
//
// Property cache: every instance owns a property-set resource (key type 0xebfee345, group
// 0xac31543d, instance = the id) in the persistence layer; global properties live in the
// instance 0xcc3014ee. Setting / removing a global property posts a sound-system event
// (0xaa232cf8 / 0xca232cff, data1 = property id).
//
// Instance cache quirks preserved: HasInstance(object, instance) is true for any object that has
// at least one instance; RemoveInstance removes the object's FIRST instance whatever its id;
// RemoveAllInstances is a no-op that always succeeds.
#pragma once
#include <cstdint>
#include <list>
#include <map>
#include <memory>

#include "audio/interfaces/cIGZSndInstanceCache.h"
#include "audio/interfaces/cIGZSndPropertyCache.h"
#include "gz/cIGZPersistRecord.h"
#include "rz/cRZAutoRefCount.h"

class cGZSndPropertyCache final : public cIGZSndPropertyCache {
public:
    static constexpr std::uint32_t kInstanceKeyType = 0xebfee345;
    static constexpr std::uint32_t kInstanceKeyGroup = 0xac31543d;
    static constexpr std::uint32_t kGlobalInstance = 0xcc3014ee;
    static constexpr std::uint32_t kEventGlobalPropertySet = 0xaa232cf8;
    static constexpr std::uint32_t kEventGlobalPropertyRemoved = 0xca232cff;

    cGZSndPropertyCache();                                        // @ 0x10047a260
    static cGZSndPropertyCache* Create() { return new cGZSndPropertyCache(); }   // @ 0x10043cac2
    ~cGZSndPropertyCache() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;  // ids 1, 0x5d4c6b90 @ 0x10047a3f4
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }   // @ 0x10047a3ca
    std::uint32_t Release() override;                             // @ 0x10047a3d8 (destroys at 0)

    bool AddInstance(std::uint32_t instance) override;            // @ 0x10047a416
    bool RemoveInstance(std::uint32_t instance) override;         // @ 0x10047a5e6 (returns whatever the manager says)
    bool HasInstance(std::uint32_t instance) override;            // @ 0x10047a656
    bool SetInstanceProperty(std::uint32_t instance, std::uint32_t property, const cIGZAny& value) override;   // @ 0x10047a686
    bool GetInstanceProperty(std::uint32_t instance, std::uint32_t property, cIGZAny* value) override;         // @ 0x10047a726
    bool HasInstanceProperty(std::uint32_t instance, std::uint32_t property) override;     // @ 0x10047a7c6
    bool RemoveInstanceProperty(std::uint32_t instance, std::uint32_t property) override;  // @ 0x10047a85c
    bool SetGlobalProperty(std::uint32_t property, const cIGZAny& value) override;         // @ 0x10047a8f2
    bool GetGlobalProperty(std::uint32_t property, cIGZAny* value) override;               // @ 0x10047a940
    bool HasGlobalProperty(std::uint32_t property) override;                               // @ 0x10047a95e
    bool RemoveGlobalProperty(std::uint32_t property) override;                            // @ 0x10047a976
    bool SetInstancePropertyUint32(std::uint32_t instance, std::uint32_t property, std::uint32_t value) override;   // @ 0x10047aa8c
    bool SetGlobalPropertyUint32(std::uint32_t property, std::uint32_t value) override;    // @ 0x10047aac0

    // @ 0x10047a9b4: posts event `id` (data1..data4) to the sound system's queue; silently
    // does nothing when there is no sound system.
    void PostPropertyCacheMessage(std::uint32_t id, std::uint32_t d1, std::uint32_t d2, std::uint32_t d3, std::uint32_t d4);

private:
    cRZAutoRefCount<cIGZPersistRecordService> persist_;           // +0x10
    std::map<std::uint32_t, cRZAutoRefCount<cIGZUnknown>> instances_;   // +0x18 resources created by AddInstance
    std::int32_t refCount_ = 0;                                   // +8
};

class cGZSndInstanceCache final : public cIGZSndInstanceCache {
public:
    cGZSndInstanceCache() = default;                              // @ 0x100478b2c
    static cGZSndInstanceCache* Create() { return new cGZSndInstanceCache(); }   // @ 0x10043cb32
    ~cGZSndInstanceCache() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;  // ids 1, 0x3df613a1 @ 0x100478c50
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override;                             // destroys at count 1

    bool AddObject(std::uint32_t object) override;                // @ 0x100478c72
    bool RemoveObject(std::uint32_t object) override;             // @ 0x100478d80
    bool HasObject(std::uint32_t object) override;                // @ 0x100478df4
    bool AddInstance(std::uint32_t object, std::uint32_t instance) override;      // @ 0x100478e74
    bool RemoveInstance(std::uint32_t object, std::uint32_t instance) override;   // @ 0x100478f08
    bool HasInstance(std::uint32_t object, std::uint32_t instance) override;      // @ 0x100478f7e
    std::list<std::uint32_t>* GetObjectInstanceIdList(std::uint32_t object) override;   // @ 0x100478e34
    bool RemoveAllInstances(std::uint32_t instance) override;     // @ 0x100478f9a
    bool FindFirstObjectFromInstance(std::uint32_t instance, std::uint32_t& object) override;   // @ 0x10047901e

private:
    std::int32_t refCount_ = 0;                                   // +8
    std::map<std::uint32_t, std::unique_ptr<std::list<std::uint32_t>>> objects_;   // +0x10
};

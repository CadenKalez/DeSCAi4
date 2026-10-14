// Interfaces of the persistence resource manager (service 0x056b906e, iid 0x656b8efc), the
// resource factories (iid 0xa56b8f17) and the cache strategies (iid 0x6bb923e4). Slot layouts
// recovered from the vtables of cGZPersistResourceManager (0x100aae8b0), cGZPersistResourceFactory
// (0x100a980b0) and cGZPersistCacheStrategyBasic (0x100adf490).
#pragma once
#include <cstdint>

#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZPersistDBSegment.h"
#include "gz/cIGZPersistRecord.h"
#include "gz/cIGZPersistResource.h"
#include "gz/cIGZPersistResourceKeyList.h"
#include "gz/cIGZPersistDBSerialRecord.h"

// ---- factories -----------------------------------------------------------------------------
class cIGZPersistResourceFactory : public cIGZUnknown {
public:
    // [3] creates an empty instance of the factory's resource for `type` (flags/owner are unused by
    // the base implementation, which forwards to [9]).
    virtual bool CreateInstance(std::uint32_t type, std::uint32_t iid, void** out, std::uint32_t flags,
                                cIGZUnknown* owner) = 0;
    // [4] creates the instance for `record` and loads it ([5]).
    virtual bool CreateInstance(cIGZPersistDBRecord& record, std::uint32_t iid, void** out, std::uint32_t flags,
                                cIGZUnknown* owner) = 0;
    virtual bool Read(cIGZPersistResource& resource, cIGZPersistDBRecord& record) = 0;          // [5]
    virtual bool Write(const cIGZPersistResource& resource, cIGZPersistDBRecord& record) = 0;   // [6]
    virtual ~cIGZPersistResourceFactory() = default;                                            // [7],[8]
    // [9..12] protected hooks implemented by concrete factories.
    virtual bool CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) = 0;         // [9]
    virtual bool CreateInstance(cIGZPersistDBRecord& record, std::uint32_t iid, void** out) = 0;  // [10]
    virtual bool SerialRead(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) = 0;   // [11]
    virtual bool SerialWrite(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) = 0;  // [12]
};
constexpr std::uint32_t kGZIID_cIGZPersistResourceFactory = 0xa56b8f17;

// ---- cache strategies ------------------------------------------------------------------------
class cIGZPersistCacheStrategy : public cIGZUnknown {
public:
    virtual void Init() = 0;                                                                   // [3]
    virtual void Shutdown() = 0;                                                               // [4]
    virtual void SetKeyFilter(cIGZPersistResourceKeyFilter* filter) = 0;                       // [5]
    virtual cIGZPersistResourceKeyFilter* GetKeyFilter() = 0;                                  // [6]
    virtual bool GetResource(const cGZPersistResourceKey& key, cIGZPersistResource** out) = 0;  // [7] AddRef'd
    virtual bool CacheResource(const cGZPersistResourceKey& key, cIGZPersistResource& r) = 0;  // [8]
    virtual bool UncacheResource(const cGZPersistResourceKey& key) = 0;                        // [9]
    virtual bool HasCachedResource(const cGZPersistResourceKey& key) = 0;                      // [10]
    virtual bool CanCacheResource(const cGZPersistResourceKey& key) = 0;                       // [11]
    virtual std::uint32_t GetCachedResourceList(cIGZPersistResourceKeyList* list,
                                                cIGZPersistResourceKeyFilter* filter) = 0;     // [12]
    virtual std::uint32_t GetCachedResourceListForType(cIGZPersistResourceKeyList* list, std::uint32_t type) = 0;  // [13]
    virtual bool OnIdle(std::uint32_t elapsed) = 0;                                            // [14]
    virtual bool IsGarbageCollectionActive() = 0;                                              // [15]
    virtual void SetGarbageCollectionActive(bool active) = 0;                                  // [16]
    virtual bool ForceGarbageCollection() = 0;                                                 // [17]
    virtual void FlushCache() = 0;                                                             // [18]
    virtual ~cIGZPersistCacheStrategy() = default;                                             // [19],[20]
};
constexpr std::uint32_t kGZIID_cIGZPersistCacheStrategy = 0x6bb923e4;

// ---- resource manager ------------------------------------------------------------------------
class cIGZPersistResourceManager : public cIGZUnknown {
public:
    // [3..6] Get*: `iid` is the interface requested on the resource, `flags`/`owner` go to the factory.
    virtual bool GetResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out,
                             std::uint32_t flags, cIGZUnknown* owner) = 0;
    virtual bool GetPrivateResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out,
                                    std::uint32_t flags, cIGZUnknown* owner) = 0;
    virtual bool GetNewResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out,
                                std::uint32_t flags, cIGZUnknown* owner) = 0;
    // [6] allocates a unique key {type, 0, counter} first.
    virtual bool GetNewResource(std::uint32_t type, std::uint32_t iid, void** out, std::uint32_t flags,
                                cIGZUnknown* owner) = 0;
    virtual void RegisterResource(const cGZPersistResourceKey& key, cIGZPersistResource& resource) = 0;  // [7]
    virtual bool RegisterResource(cIGZPersistResource* resource) = 0;                                    // [8]
    virtual bool UnregisterResource(const cGZPersistResourceKey& key) = 0;                               // [9]
    virtual bool HasRegisteredResource(const cGZPersistResourceKey& key) = 0;                            // [10]
    virtual bool Save(const cGZPersistResourceKey& key, cIGZPersistDBSegment& segment) = 0;              // [11]
    virtual bool Save(cIGZPersistResourceKeyList& keys, cIGZPersistDBSegment& segment) = 0;              // [12]
    virtual bool SaveResource(cIGZPersistResource* resource, const cGZPersistResourceKey& key,
                              cIGZPersistDBSegment& segment) = 0;                                        // [13]
    virtual bool TestForKey(cGZPersistResourceKey& key) = 0;                                             // [14]
    virtual std::uint32_t GetResourceList(cIGZPersistResourceKeyList** list, cIGZPersistResourceKeyFilter* filter) = 0;  // [15]
    virtual std::uint32_t GetResourceListForType(cIGZPersistResourceKeyList** list, std::uint32_t type) = 0;            // [16]
    virtual std::uint32_t GetAvailableResourceList(cIGZPersistResourceKeyList** list, cIGZPersistResourceKeyFilter* filter) = 0;  // [17]
    virtual std::uint32_t GetAvailableResourceListForType(cIGZPersistResourceKeyList** list, std::uint32_t type) = 0;      // [18]
    virtual bool RegisterObjectFactory(std::uint32_t classId, std::uint32_t type, cIGZPersistResourceFactory* factory) = 0;  // [19]
    virtual bool UnregisterObjectFactory(std::uint32_t classId) = 0;                                     // [20]
    virtual void FindObjectFactory(cIGZPersistResource& resource, cIGZPersistResourceFactory** out) = 0;   // [21]
    virtual void FindObjectFactory(const cGZPersistResourceKey& key, cIGZPersistResourceFactory** out) = 0;  // [22]
    virtual bool FindObjectFactory(std::uint32_t type, cIGZPersistResourceFactory** out) = 0;            // [23]
    virtual std::uint32_t GetFactoryCount() = 0;                                                         // [24]
    virtual cIGZPersistResourceFactory* GetFactoryByIndex(std::uint32_t index) = 0;                      // [25]
    virtual bool RegisterDBSegment(cIGZPersistDBSegment& segment) = 0;                                   // [26] = front
    virtual bool RegisterDBSegmentFront(cIGZPersistDBSegment& segment) = 0;                              // [27]
    virtual bool RegisterDBSegmentBack(cIGZPersistDBSegment& segment) = 0;                               // [28]
    virtual bool UnregisterDBSegment(cIGZPersistDBSegment& segment) = 0;                                 // [29]
    virtual bool TestDBSegment(cIGZPersistDBSegment& segment) = 0;                                       // [30]
    virtual bool FindDBSegment(const cGZPersistResourceKey& key, cIGZPersistDBSegment** out) = 0;        // [31] AddRef'd
    virtual bool FindDBSegment(std::uint32_t segmentId, cIGZPersistDBSegment** out) = 0;                 // [32]
    virtual std::uint32_t GetSegmentCount() = 0;                                                         // [33]
    virtual cIGZPersistDBSegment* GetSegmentByIndex(std::uint32_t index) = 0;                            // [34]
    virtual std::uint32_t EnumerateDBSegments(cIGZPersistDBSegment** out, std::uint32_t& count) = 0;     // [35]
    virtual void EnumerateDBSegments(bool (*callback)(cIGZPersistDBSegment*, void*), void* context) = 0;  // [36]
    virtual bool OpenDBRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record, bool write) = 0;  // [37]
    virtual bool CloseDBRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record) = 0;      // [38]
    virtual bool AddCacheStrategy(cIGZPersistCacheStrategy* strategy) = 0;                               // [39]
    virtual bool RemoveCacheStrategy(cIGZPersistCacheStrategy* strategy) = 0;                            // [40]
    virtual bool IsGarbageCollectionActive() = 0;                                                        // [41]
    virtual void SetGarbageCollectionActive(bool active) = 0;                                            // [42]
    virtual bool ForceGarbageCollection() = 0;                                                           // [43]
};
// (kGZIID_PersistResourceManager / kGZSID_PersistResourceManager live in cIGZPersistRecord.h)

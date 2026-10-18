// A resource manager whose only working parts are the ones the persistence reader uses (HasRegisteredResource /
// RegisterResource / UnregisterResource / GetResource); the rest answers "nothing". Generated from cIGZPersistResourceManager.
#pragma once
#include <map>

#include "gz/cIGZPersistResourceManager.h"

struct FakeResourceManager : cIGZPersistResourceManager {
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool GetResource(const cGZPersistResourceKey& p0, std::uint32_t p1, void** p2, std::uint32_t p3, cIGZUnknown* p4) override { return false;}
    bool GetPrivateResource(const cGZPersistResourceKey& p0, std::uint32_t p1, void** p2, std::uint32_t p3, cIGZUnknown* p4) override { return false;}
    bool GetNewResource(const cGZPersistResourceKey& p0, std::uint32_t p1, void** p2, std::uint32_t p3, cIGZUnknown* p4) override { return false;}
    bool GetNewResource(std::uint32_t p0, std::uint32_t p1, void** p2, std::uint32_t p3, cIGZUnknown* p4) override { return false;}
    void RegisterResource(const cGZPersistResourceKey& p0, cIGZPersistResource& p1) override {}
    bool RegisterResource(cIGZPersistResource* p0) override { return false;}
    bool UnregisterResource(const cGZPersistResourceKey& p0) override { return false;}
    bool HasRegisteredResource(const cGZPersistResourceKey& p0) override { return false;}
    bool Save(const cGZPersistResourceKey& p0, cIGZPersistDBSegment& p1) override { return false;}
    bool Save(cIGZPersistResourceKeyList& p0, cIGZPersistDBSegment& p1) override { return false;}
    bool SaveResource(cIGZPersistResource* p0, const cGZPersistResourceKey& p1, cIGZPersistDBSegment& p2) override { return false;}
    bool TestForKey(cGZPersistResourceKey& p0) override { return false;}
    std::uint32_t GetResourceList(cIGZPersistResourceKeyList** p0, cIGZPersistResourceKeyFilter* p1) override { return 0;}
    std::uint32_t GetResourceListForType(cIGZPersistResourceKeyList** p0, std::uint32_t p1) override { return 0;}
    std::uint32_t GetAvailableResourceList(cIGZPersistResourceKeyList** p0, cIGZPersistResourceKeyFilter* p1) override { return 0;}
    std::uint32_t GetAvailableResourceListForType(cIGZPersistResourceKeyList** p0, std::uint32_t p1) override { return 0;}
    bool RegisterObjectFactory(std::uint32_t p0, std::uint32_t p1, cIGZPersistResourceFactory* p2) override { return false;}
    bool UnregisterObjectFactory(std::uint32_t p0) override { return false;}
    void FindObjectFactory(cIGZPersistResource& p0, cIGZPersistResourceFactory** p1) override {}
    void FindObjectFactory(const cGZPersistResourceKey& p0, cIGZPersistResourceFactory** p1) override {}
    bool FindObjectFactory(std::uint32_t p0, cIGZPersistResourceFactory** p1) override { return false;}
    std::uint32_t GetFactoryCount() override { return 0;}
    cIGZPersistResourceFactory* GetFactoryByIndex(std::uint32_t p0) override { return nullptr;}
    bool RegisterDBSegment(cIGZPersistDBSegment& p0) override { return false;}
    bool RegisterDBSegmentFront(cIGZPersistDBSegment& p0) override { return false;}
    bool RegisterDBSegmentBack(cIGZPersistDBSegment& p0) override { return false;}
    bool UnregisterDBSegment(cIGZPersistDBSegment& p0) override { return false;}
    bool TestDBSegment(cIGZPersistDBSegment& p0) override { return false;}
    bool FindDBSegment(const cGZPersistResourceKey& p0, cIGZPersistDBSegment** p1) override { return false;}
    bool FindDBSegment(std::uint32_t p0, cIGZPersistDBSegment** p1) override { return false;}
    std::uint32_t GetSegmentCount() override { return 0;}
    cIGZPersistDBSegment* GetSegmentByIndex(std::uint32_t p0) override { return nullptr;}
    std::uint32_t EnumerateDBSegments(cIGZPersistDBSegment** p0, std::uint32_t& p1) override { return 0;}
    void EnumerateDBSegments(bool (*callback)(cIGZPersistDBSegment*, void*), void* p1) override {}
    bool OpenDBRecord(const cGZPersistResourceKey& p0, cIGZPersistDBRecord** p1, bool p2) override { return false;}
    bool CloseDBRecord(const cGZPersistResourceKey& p0, cIGZPersistDBRecord** p1) override { return false;}
    bool AddCacheStrategy(cIGZPersistCacheStrategy* p0) override { return false;}
    bool RemoveCacheStrategy(cIGZPersistCacheStrategy* p0) override { return false;}
    bool IsGarbageCollectionActive() override { return false;}
    void SetGarbageCollectionActive(bool p0) override {}
    bool ForceGarbageCollection() override { return false;}
};

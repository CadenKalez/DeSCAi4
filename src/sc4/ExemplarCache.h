// cSCExemplarCache (service 0x2533df21, priority 999999, view iid 0xa52ea0c8; 25 functions at 0x10052ed40..0x10053026a)
// keeps every loaded exemplar and cohort of the game's data files and answers queries over them. Plug-in segments are
// scanned when the "plugin added" message (0x2537d4ed) arrives; a cohort-changed message (0x853d11f1) makes every
// resource that inherits from the changed cohort re-attach to it so cached property views refresh.
//
// Queries are described by a cISCExemplarCacheQueryDef. A "retained" definition (non-zero id) is registered once; if it
// keeps its result the cache re-runs it whenever exemplars come or go and hands out the stored list.
// Quirks kept: PerformQuery on a retained definition registers it, so a second PerformQuery with the same id fails;
// RemoveExemplar forgets the key before it looks for the exemplar in the list; the cache posts message 0x2537f799 after
// a change instead of sending it.
#pragma once
#include <cstdint>
#include <list>
#include <map>
#include <set>

#include "gz/cIGZMessage2.h"
#include "gz/cIGZUnknownList.h"
#include "gz/cGZPersistResourceKey.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZSystemService.h"
#include "sc4/Exemplar.h"
#include "sc4/OccupantManager.h"   // kGZIID_cIGZMessageTarget2

namespace sc4 {

constexpr std::uint32_t kGZSID_cSCExemplarCache = 0x2533df21, kGZIID_cISCExemplarCache = 0xa52ea0c8;
constexpr std::uint32_t kMessageExemplarPluginAdded = 0x2537d4ed, kMessageExemplarCacheChanged = 0x2537f799;

class cISCExemplarCacheQueryDef : public cIGZUnknown {
public:
    virtual std::uint32_t GetQueryID() = 0;                              // +0x18
    virtual bool DoesExemplarMatch(cISCResExemplar* exemplar) = 0;       // +0x20
    virtual bool IsRetained() = 0;                                       // +0x28
    virtual bool KeepsResult() = 0;                                      // +0x30
};

class cISCExemplarCache : public cIGZUnknown {
public:
    virtual bool AddExemplar(cISCResExemplar* exemplar, bool post) = 0;
    virtual bool RemoveExemplar(cISCResExemplar* exemplar, bool post) = 0;
    virtual bool AddCohort(cISCResExemplarCohort* cohort) = 0;
    virtual bool RemoveCohort(cISCResExemplarCohort* cohort, bool post) = 0;
    virtual bool PerformQuery(cISCExemplarCacheQueryDef* query, cIGZUnknownList** result) = 0;
    virtual bool RegisterRetainedQuery(cISCExemplarCacheQueryDef* query) = 0;
    virtual bool UnregisterRetainedQuery(std::uint32_t id) = 0;
    virtual bool PerformRetainedQuery(std::uint32_t id) = 0;
    virtual bool GetRetainedQueryResult(std::uint32_t id, cIGZUnknownList** result) = 0;
};

class cSCExemplarCache final : public cRZSystemService, public cISCExemplarCache, public cIGZMessageTarget2 {
public:
    cSCExemplarCache() : cRZSystemService(kGZSID_cSCExemplarCache, 999999) {}   // @ 0x10052ed40
    ~cSCExemplarCache() override = default;                                     // @ 0x10052ee46

    bool QueryInterface(std::uint32_t iid, void** out) override;                // @ 0x100530112
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }      // @ 0x10053025c
    std::uint32_t Release() override { return cRZSystemService::Release(); }    // @ 0x10053026a
    bool Init() override;                                                       // @ 0x10052ef72
    bool Shutdown() override;                                                   // @ 0x10052effe
    bool DoMessage(cIGZMessage2* message) override;                             // @ 0x10052f100

    bool AddExemplar(cISCResExemplar* exemplar, bool post) override;            // @ 0x10052f556
    bool RemoveExemplar(cISCResExemplar* exemplar, bool post) override;         // @ 0x10052f6fa
    bool AddCohort(cISCResExemplarCohort* cohort) override;                     // @ 0x10052f7f4
    bool RemoveCohort(cISCResExemplarCohort* cohort, bool post) override;       // @ 0x10052f976
    bool PerformQuery(cISCExemplarCacheQueryDef* query, cIGZUnknownList** result) override;   // @ 0x10052fb00
    bool RegisterRetainedQuery(cISCExemplarCacheQueryDef* query) override;      // @ 0x10052fd52
    bool UnregisterRetainedQuery(std::uint32_t id) override;                    // @ 0x10052ffbe
    bool PerformRetainedQuery(std::uint32_t id) override;                       // @ 0x10053001e
    bool GetRetainedQueryResult(std::uint32_t id, cIGZUnknownList** result) override;   // @ 0x1005300ae

    virtual void updateRetainedQueries();                                       // @ 0x100530178
    virtual bool performQuery(cISCExemplarCacheQueryDef* query, cIGZUnknownList* result);   // @ 0x10053020a
    virtual bool processExemplarAdd(std::uint32_t segmentId, bool post);        // @ 0x10052f2d8
    virtual bool processCohortChange(std::uint32_t group, std::uint32_t instance);   // @ 0x10052f1d0

    static void exemplarPluginCallback(const cGZPersistResourceKey& key, void* context);   // @ 0x10052f3ee
    static void sendMessage();                                                  // @ 0x10052f4ae (the unused argument dropped)

    struct tRetainedQuery {
        cRZAutoRefCount<cISCExemplarCacheQueryDef> query;
        cRZAutoRefCount<cIGZUnknownList> result;
    };

private:
    std::list<cRZAutoRefCount<cISCResExemplar>> exemplars_;                 // +0x48
    std::set<cGZPersistResourceKey, cGZPersistResourceKeyOrder> keys_;      // +0x60
    std::list<cRZAutoRefCount<cISCResExemplarCohort>> cohorts_;             // +0x78
    std::map<std::uint32_t, tRetainedQuery> retained_;                      // +0x90
};

}  // namespace sc4

#include <cstdio>
#include <vector>

#include "fake_resourcemanager.h"
#include "gz/cIGZPersistDBSegment.h"
#include "rz/cRZUnknownList.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZSysServPtr.h"
#include "sc4/ExemplarCache.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Cohort final : cISCResExemplarCohort {
    cGZPersistResourceKey key;
    cISCResExemplarCohort* parent = nullptr;
    int setParentCalls = 0, refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Write(cIGZOStream&) const override { return false; }
    bool Read(cIGZIStream&) override { return false; }
    std::uint32_t GetGZCLSID() const override { return 0; }
    cISCResExemplarCohort* GetParent() override { return parent; }
    void SetParent(cISCResExemplarCohort* p, bool) override { parent = p; ++setParentCalls; }
    cISCPropertyHolder* AsISCPropertyHolder() override { return nullptr; }
    bool IsPropertyLocal(std::uint32_t) override { return false; }
    bool GetKey(cGZPersistResourceKey& k) override { k = key; return true; }
    bool SetKey(const cGZPersistResourceKey& k) override { key = k; return true; }
    void CompactProperties() override {}
    void GenerateCachedMap() override {}
};
struct Exemplar final : cISCResExemplar {
    cGZPersistResourceKey key;
    cISCResExemplarCohort* cohort = nullptr;
    int setCohortCalls = 0, refs = 0;
    bool QueryInterface(std::uint32_t iid, void** out) override { if (iid != 1) return false; *out = static_cast<cIGZUnknown*>(static_cast<cISCResExemplar*>(this)); AddRef(); return true; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Write(cIGZOStream&) const override { return false; }
    bool Read(cIGZIStream&) override { return false; }
    std::uint32_t GetGZCLSID() const override { return 0; }
    cISCPropertyHolder* AsISCPropertyHolder() override { return nullptr; }
    bool IsPropertyLocal(std::uint32_t) override { return false; }
    cISCResExemplarCohort* GetCohort() override { return cohort; }
    void SetCohort(cISCResExemplarCohort* c, bool) override { cohort = c; ++setCohortCalls; }
    bool GetKey(cGZPersistResourceKey& k) override { k = key; return true; }
    bool SetKey(const cGZPersistResourceKey& k) override { key = k; return true; }
    void CompactProperties() override {}
    void GenerateCachedMap() override {}
};
struct Query final : cISCExemplarCacheQueryDef {
    std::uint32_t id = 0;
    bool retained = false, keeps = false;
    std::uint32_t wantedGroup = 0;
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    std::uint32_t GetQueryID() override { return id; }
    bool DoesExemplarMatch(cISCResExemplar* e) override {
        cGZPersistResourceKey k;
        e->GetKey(k);
        return k.group == wantedGroup;
    }
    bool IsRetained() override { return retained; }
    bool KeepsResult() override { return keeps; }
};

struct Server final : cIGZMessageServer2 {
    std::vector<std::pair<cIGZMessageTarget2*, std::uint32_t>> listeners;
    std::vector<std::uint32_t> posted;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool MessageSend(cIGZMessage2*) override { return true; }
    bool MessagePost(cIGZMessage2* m, bool) override { posted.push_back(m->GetType()); return true; }
    bool AddNotification(cIGZMessageTarget2* t, std::uint32_t type) override { listeners.push_back({t, type}); return true; }
    bool RemoveNotification(cIGZMessageTarget2* t, std::uint32_t type) override {
        for (auto it = listeners.begin(); it != listeners.end(); ++it)
            if (*it == std::make_pair(t, type)) { listeners.erase(it); return true; }
        return false;
    }
    bool GeneralMessagePostToTarget(cIGZMessage2*, cIGZMessageTarget2*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget2*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
    std::uint32_t GetRefCount() const override { return 1; }
    bool CreateMessage(std::uint32_t, std::uint32_t, void**) override { return false; }
};

struct Segment final : cIGZPersistDBSegmentStub {
    std::vector<cGZPersistResourceKey> keys;
    std::uint32_t AddRef() override { return 2; }
    std::uint32_t Release() override { return 1; }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    bool GetResourceKeyList(cIGZPersistResourceKeyList& list) override {
        for (auto& k : keys) list.Insert(k);
        return true;
    }
};

struct Manager final : FakeResourceManager {
    Segment segment;
    std::vector<Exemplar*> exemplars;
    std::vector<Cohort*> cohorts;
    bool FindDBSegment(std::uint32_t id, cIGZPersistDBSegment** out) override { if (id != 7) return false; *out = &segment; return true; }
    bool GetResource(const cGZPersistResourceKey& key, std::uint32_t iid, void** out, std::uint32_t, cIGZUnknown*) override {
        if (iid == kGZIID_cISCResExemplar) {
            for (auto* e : exemplars) if (e->key == key) { *out = static_cast<cISCResExemplar*>(e); e->AddRef(); return true; }
        } else if (iid == kGZIID_cISCResExemplarCohort) {
            for (auto* c : cohorts) if (c->key == key) { *out = static_cast<cISCResExemplarCohort*>(c); c->AddRef(); return true; }
        }
        return false;
    }
};

Server g_server;
Manager g_manager;
bool Resolve(std::uint32_t service, std::uint32_t iid, void** out) {
    if (service == 0x04fa845b && iid == 0x652294c7) { *out = &g_server; return true; }
    if (service == 0x056b906e && iid == 0x656b8efc) { *out = static_cast<cIGZPersistResourceManager*>(&g_manager); return true; }
    return false;
}
std::uint32_t Count(cIGZUnknownList* l) { return l ? l->Size() : 0; }
}  // namespace

int main() {
    SetRZServiceResolver(&Resolve);
    auto* cache = new cSCExemplarCache();
    cache->AddRef();
    CHECK(cache->Init());
    CHECK(g_server.listeners.size() == 2);
    void* view = nullptr;
    CHECK(cache->QueryInterface(kGZIID_cISCExemplarCache, &view) && view == static_cast<cISCExemplarCache*>(cache));
    static_cast<cIGZUnknown*>(static_cast<cISCExemplarCache*>(cache))->Release();

    Exemplar e1, e2, e3;
    e1.key = {kResourceTypeExemplar, 1, 10};
    e2.key = {kResourceTypeExemplar, 2, 11};
    e3.key = {kResourceTypeExemplar, 1, 12};
    CHECK(cache->AddExemplar(&e1, false) && cache->AddExemplar(&e2, false));
    CHECK(!cache->AddExemplar(&e1, false));               // same key
    CHECK(g_server.posted.empty());
    CHECK(cache->AddExemplar(&e3, true));
    CHECK(g_server.posted.size() == 1 && g_server.posted[0] == kMessageExemplarCacheChanged);
    CHECK(e1.refs == 1);

    // plain query
    Query q;
    q.wantedGroup = 1;
    cIGZUnknownList* result = nullptr;
    CHECK(cache->PerformQuery(&q, &result) && Count(result) == 2);
    result->Release();
    CHECK(!cache->PerformQuery(nullptr, &result) && !cache->PerformQuery(&q, nullptr));

    // retained query that keeps its result
    Query r;
    r.id = 5; r.retained = true; r.keeps = true; r.wantedGroup = 2;
    CHECK(cache->RegisterRetainedQuery(&r));
    CHECK(!cache->RegisterRetainedQuery(&r));              // already registered
    Query zero;
    zero.retained = true;
    CHECK(!cache->RegisterRetainedQuery(&zero));           // id 0
    cIGZUnknownList* kept = nullptr;
    CHECK(cache->GetRetainedQueryResult(5, &kept) && Count(kept) == 1);
    kept->Release();
    Exemplar e4;
    e4.key = {kResourceTypeExemplar, 2, 13};
    CHECK(cache->AddExemplar(&e4, true));                  // refreshes retained results
    CHECK(cache->GetRetainedQueryResult(5, &kept) && Count(kept) == 2);
    kept->Release();
    CHECK(cache->PerformRetainedQuery(5) && cache->PerformRetainedQuery(0) && !cache->PerformRetainedQuery(99));
    CHECK(!cache->GetRetainedQueryResult(99, &kept));
    CHECK(cache->UnregisterRetainedQuery(5) && !cache->UnregisterRetainedQuery(5));
    // PerformQuery registers a retained query: the second call fails
    Query again;
    again.id = 6; again.retained = true; again.keeps = true; again.wantedGroup = 1;
    CHECK(cache->PerformQuery(&again, &result) && Count(result) == 2);
    result->Release();
    CHECK(!cache->PerformQuery(&again, &result));
    // retained but not keeping: registered, result computed fresh
    Query lazy;
    lazy.id = 7; lazy.retained = true; lazy.keeps = false; lazy.wantedGroup = 2;
    CHECK(cache->PerformQuery(&lazy, &result) && Count(result) == 2);
    result->Release();

    // removal
    CHECK(cache->RemoveExemplar(&e4, true));
    CHECK(!cache->RemoveExemplar(&e4, false));
    CHECK(e4.refs == 0);

    // cohorts
    Cohort parent, child;
    parent.key = {kResourceTypeCohort, 3, 20};
    child.key = {kResourceTypeCohort, 4, 21};
    child.parent = &parent;
    CHECK(cache->AddCohort(&parent) && cache->AddCohort(&child) && !cache->AddCohort(&parent));
    e1.cohort = &parent;
    // a change of the parent cohort makes dependants re-attach
    auto* change = new cRZMessage2Standard();
    change->AddRef();
    change->SetType(kCohortChangedMessage);
    change->SetData1(3);
    change->SetData2(20);
    CHECK(cache->DoMessage(change));
    CHECK(child.setParentCalls == 1 && e1.setCohortCalls == 1 && e2.setCohortCalls == 0);
    change->SetData2(21);
    CHECK(cache->DoMessage(change));
    CHECK(child.setParentCalls == 1);                       // child has no cohort of its own as parent... only parent==group/instance matches
    change->SetType(0x1234);
    CHECK(!cache->DoMessage(change));
    change->Release();
    // removing the parent moves the child and dependants up
    CHECK(cache->RemoveCohort(&parent, true));
    CHECK(child.parent == nullptr && e1.cohort == nullptr);
    CHECK(!cache->RemoveCohort(&parent, false));

    // plug-in scan
    Exemplar plugged;
    plugged.key = {kResourceTypeExemplar, 9, 30};
    Cohort pluggedCohort;
    pluggedCohort.key = {kResourceTypeCohort, 9, 31};
    g_manager.exemplars = {&plugged, &e1};
    g_manager.cohorts = {&pluggedCohort};
    g_manager.segment.keys = {plugged.key, pluggedCohort.key, e1.key, {0x1234, 0, 0}};
    const std::size_t postedBefore = g_server.posted.size();
    auto* added = new cRZMessage2Standard();
    added->AddRef();
    added->SetType(kMessageExemplarPluginAdded);
    added->SetData1(7);
    added->SetData2(1);
    CHECK(cache->DoMessage(added));
    CHECK(g_server.posted.size() == postedBefore + 1);
    Query nine;
    nine.wantedGroup = 9;
    CHECK(cache->PerformQuery(&nine, &result) && Count(result) == 1);
    result->Release();
    added->SetData1(8);
    CHECK(!cache->DoMessage(added));                        // unknown segment
    added->Release();

    CHECK(cache->Shutdown());
    CHECK(g_server.listeners.empty());
    CHECK(e1.refs == 0 && plugged.refs == 0);
    cache->Release();
    SetRZServiceResolver(nullptr);
    if (failures == 0) std::printf("sc4_exemplarcache: all passed\n");
    return failures == 0 ? 0 : 1;
}

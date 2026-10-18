#include "sc4/ExemplarCache.h"

#include <algorithm>

#include "gz/cGZPersistResourceKeyList.h"
#include "gz/cIGZPersistDBSegment.h"
#include "gz/cIGZPersistResourceManager.h"
#include "rz/cRZMessage2Standard.h"
#include "rz/cRZSysServPtr.h"
#include "rz/cRZUnknownList.h"

namespace sc4 {

namespace {
using MessageServerPtr = cRZSysServPtr<cIGZMessageServer2, kGZSID_MessageServer2, kGZIID_cIGZMessageServer2>;
using ResourceManagerPtr = cRZSysServPtr<cIGZPersistResourceManager, 0x056b906e, 0x656b8efc>;

struct PluginScan {
    cSCExemplarCache* cache;
    const std::set<cGZPersistResourceKey, cGZPersistResourceKeyOrder>* keys;
    cIGZPersistResourceManager* manager;
    std::int32_t exemplarsAdded;
    std::int32_t cohortsAdded;
};
}  // namespace

bool cSCExemplarCache::QueryInterface(std::uint32_t iid, void** out) {
    *out = nullptr;
    if (iid == kGZIID_cIGZMessageTarget2) *out = static_cast<cIGZMessageTarget2*>(this);
    else if (iid == kGZIID_cISCExemplarCache) *out = static_cast<cISCExemplarCache*>(this);
    else return cRZSystemService::QueryInterface(iid, out);
    AddRef();
    return true;
}

bool cSCExemplarCache::Init() {
    MessageServerPtr server;
    server->AddNotification(static_cast<cIGZMessageTarget2*>(this), kMessageExemplarPluginAdded);
    server->AddNotification(static_cast<cIGZMessageTarget2*>(this), kCohortChangedMessage);
    return true;
}

bool cSCExemplarCache::Shutdown() {
    bool result = false;
    {
        MessageServerPtr server;
        if (server.get() &&
            server->RemoveNotification(static_cast<cIGZMessageTarget2*>(this), kMessageExemplarPluginAdded))
            result = server->RemoveNotification(static_cast<cIGZMessageTarget2*>(this), kCohortChangedMessage);
    }
    retained_.clear();
    exemplars_.clear();
    cohorts_.clear();
    keys_.clear();
    return result;
}

bool cSCExemplarCache::DoMessage(cIGZMessage2* message) {
    cIGZMessage2Standard* standard = nullptr;
    if (!message->QueryInterface(kGZIID_cIGZMessage2Standard, reinterpret_cast<void**>(&standard))) return false;
    bool result = false;
    const std::uint32_t type = standard->GetType();
    if (type == kCohortChangedMessage)
        result = processCohortChange(static_cast<std::uint32_t>(standard->GetData1()), static_cast<std::uint32_t>(standard->GetData2()));
    else if (type == kMessageExemplarPluginAdded)
        result = processExemplarAdd(static_cast<std::uint32_t>(standard->GetData1()), standard->GetData2() != 0);
    standard->Release();
    return result;
}

void cSCExemplarCache::sendMessage() {
    auto* message = new cRZMessage2Standard();
    message->AddRef();
    message->SetType(kMessageExemplarCacheChanged);
    MessageServerPtr server;
    if (server.get()) server->MessagePost(message, false);
    message->Release();
}

bool cSCExemplarCache::processCohortChange(std::uint32_t group, std::uint32_t instance) {
    for (auto& entry : cohorts_) {
        cISCResExemplarCohort* cohort = entry.get();
        cISCResExemplarCohort* parent = cohort->GetParent();
        cGZPersistResourceKey key;
        if (parent && parent->GetKey(key) && key.group == group && key.instance == instance) cohort->SetParent(parent, true);
    }
    for (auto& entry : exemplars_) {
        cISCResExemplar* exemplar = entry.get();
        cISCResExemplarCohort* cohort = exemplar->GetCohort();
        cGZPersistResourceKey key;
        if (cohort && cohort->GetKey(key) && key.group == group && key.instance == instance) exemplar->SetCohort(cohort, true);
    }
    return true;
}

bool cSCExemplarCache::processExemplarAdd(std::uint32_t segmentId, bool post) {
    ResourceManagerPtr manager;
    if (!manager.get()) return false;
    cGZPersistResourceKeyList keys;
    PluginScan scan{this, &keys_, manager.get(), 0, 0};
    cIGZPersistDBSegment* segment = nullptr;
    if (!manager->FindDBSegment(segmentId, &segment)) return false;
    if (segment->GetResourceKeyList(keys)) keys.EnumKeys(exemplarPluginCallback, &scan);
    segment->Release();
    if (scan.exemplarsAdded != 0 && post) {
        updateRetainedQueries();
        sendMessage();
    }
    return true;
}

void cSCExemplarCache::exemplarPluginCallback(const cGZPersistResourceKey& key, void* context) {
    auto* scan = static_cast<PluginScan*>(context);
    if ((key.type != kResourceTypeExemplar && key.type != kResourceTypeCohort) || scan->keys->find(key) != scan->keys->end()) return;
    if (key.type == kResourceTypeExemplar) {
        cISCResExemplar* exemplar = nullptr;
        if (!scan->manager->GetResource(key, kGZIID_cISCResExemplar, reinterpret_cast<void**>(&exemplar), 0, nullptr)) return;
        if (scan->cache->AddExemplar(exemplar, false)) ++scan->exemplarsAdded;
        exemplar->Release();
    } else {
        cISCResExemplarCohort* cohort = nullptr;
        if (!scan->manager->GetResource(key, kGZIID_cISCResExemplarCohort, reinterpret_cast<void**>(&cohort), 0, nullptr)) return;
        if (scan->cache->AddCohort(cohort)) ++scan->cohortsAdded;
        cohort->Release();
    }
}

bool cSCExemplarCache::AddExemplar(cISCResExemplar* exemplar, bool post) {
    cGZPersistResourceKey key;
    if (!exemplar->GetKey(key) || keys_.find(key) != keys_.end()) return false;
    keys_.insert(key);
    exemplars_.push_back(cRZAutoRefCount<cISCResExemplar>(exemplar));
    if (post) {
        updateRetainedQueries();
        sendMessage();
    }
    return true;
}

bool cSCExemplarCache::RemoveExemplar(cISCResExemplar* exemplar, bool post) {
    cGZPersistResourceKey key;
    if (!exemplar->GetKey(key) || keys_.find(key) == keys_.end()) return false;
    keys_.erase(key);
    for (auto it = exemplars_.begin(); it != exemplars_.end(); ++it) {
        if (it->get() == exemplar) {
            exemplars_.erase(it);
            if (post) {
                updateRetainedQueries();
                sendMessage();
            }
            return true;
        }
    }
    return false;
}

bool cSCExemplarCache::AddCohort(cISCResExemplarCohort* cohort) {
    cGZPersistResourceKey key;
    if (!cohort->GetKey(key) || keys_.find(key) != keys_.end()) return false;
    keys_.insert(key);
    cohorts_.push_back(cRZAutoRefCount<cISCResExemplarCohort>(cohort));
    return true;
}

bool cSCExemplarCache::RemoveCohort(cISCResExemplarCohort* cohort, bool post) {
    cGZPersistResourceKey key;
    if (!cohort->GetKey(key) || keys_.find(key) == keys_.end()) return false;
    keys_.erase(key);
    cISCResExemplarCohort* parent = cohort->GetParent();
    auto found = cohorts_.end();
    for (auto it = cohorts_.begin(); it != cohorts_.end(); ++it) {
        if (it->get() == cohort) {
            found = it;
        } else if (it->get()->GetParent() == cohort) {
            it->get()->SetParent(parent, true);   // children move up to the removed cohort's parent
        }
    }
    if (found != cohorts_.end()) cohorts_.erase(found);
    for (auto& entry : exemplars_)
        if (entry->GetCohort() == cohort) entry->SetCohort(parent, true);
    if (post) {
        updateRetainedQueries();
        sendMessage();
    }
    return true;
}

bool cSCExemplarCache::performQuery(cISCExemplarCacheQueryDef* query, cIGZUnknownList* result) {
    for (auto& entry : exemplars_)
        if (query->DoesExemplarMatch(entry.get())) result->PushBack(entry.get());
    return true;
}

bool cSCExemplarCache::PerformQuery(cISCExemplarCacheQueryDef* query, cIGZUnknownList** result) {
    if (!query || !result) return false;
    if (query->IsRetained() && !RegisterRetainedQuery(query)) return false;
    auto* list = new cRZUnknownList();
    *result = list;
    list->AddRef();
    if (query->IsRetained() && query->KeepsResult()) {
        list->CopyFrom(*retained_[query->GetQueryID()].result.get());
        return true;
    }
    if (!performQuery(query, list)) {
        list->Release();
        return false;
    }
    return true;
}

bool cSCExemplarCache::RegisterRetainedQuery(cISCExemplarCacheQueryDef* query) {
    if (!query->IsRetained() || query->GetQueryID() == 0) return false;
    const std::uint32_t id = query->GetQueryID();
    if (retained_.find(id) != retained_.end()) return false;
    auto* list = new cRZUnknownList();
    cRZAutoRefCount<cIGZUnknownList> result(list);
    tRetainedQuery& entry = retained_[id];
    entry.query = query;
    entry.result = list;
    return query->KeepsResult() ? performQuery(query, list) : true;
}

bool cSCExemplarCache::UnregisterRetainedQuery(std::uint32_t id) {
    auto it = retained_.find(id);
    if (it == retained_.end()) return false;
    retained_.erase(it);
    return true;
}

bool cSCExemplarCache::PerformRetainedQuery(std::uint32_t id) {
    if (id == 0) {
        updateRetainedQueries();
        return true;
    }
    auto it = retained_.find(id);
    if (it == retained_.end()) return false;
    it->second.result->Clear();
    return performQuery(it->second.query.get(), it->second.result.get());
}

bool cSCExemplarCache::GetRetainedQueryResult(std::uint32_t id, cIGZUnknownList** result) {
    if (!result) return false;
    auto it = retained_.find(id);
    if (it == retained_.end()) return false;
    *result = it->second.result.get();
    (*result)->AddRef();
    return true;
}

void cSCExemplarCache::updateRetainedQueries() {
    for (auto& [id, entry] : retained_) {
        if (entry.query->KeepsResult()) {
            entry.result->Clear();
            performQuery(entry.query.get(), entry.result.get());
        }
    }
}

}  // namespace sc4

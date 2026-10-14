#include "gz/cGZPersistDBSegment.h"

bool cGZPersistDBSegment::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZPersistDBSegment) {
        *out = static_cast<cIGZPersistDBSegment*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZPersistDBSegment::AddRef() {
    std::lock_guard<std::recursive_mutex> guard(mutex_);
    return ++refCount_;
}

std::uint32_t cGZPersistDBSegment::Release() {
    std::unique_lock<std::recursive_mutex> guard(mutex_);
    if (refCount_ < 2) {
        guard.unlock();
        FinalRelease();
        return 0;
    }
    return --refCount_;
}

#define LOCKED(call)                                         \
    std::lock_guard<std::recursive_mutex> guard(mutex_);     \
    return call

bool cGZPersistDBSegment::TestForRecord(const cGZPersistResourceKey& key) { LOCKED(DoTestForRecord(key)); }
std::uint32_t cGZPersistDBSegment::GetRecordSize(const cGZPersistResourceKey& key) { LOCKED(DoGetRecordSize(key)); }
bool cGZPersistDBSegment::OpenRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record, std::uint32_t flags) {
    LOCKED(DoOpenRecord(key, record, flags));
}
bool cGZPersistDBSegment::CreateNewRecord(const cGZPersistResourceKey& key, cIGZPersistDBRecord** record) {
    LOCKED(DoCreateNewRecord(key, record));
}
bool cGZPersistDBSegment::CloseRecord(cIGZPersistDBRecord* record) { LOCKED(DoCloseRecord(record)); }
bool cGZPersistDBSegment::AbortRecord(cIGZPersistDBRecord* record) { LOCKED(DoAbortRecord(record)); }
bool cGZPersistDBSegment::DeleteRecord(const cGZPersistResourceKey& key) { LOCKED(DoDeleteRecord(key)); }
std::uint32_t cGZPersistDBSegment::ReadRecord(const cGZPersistResourceKey& key, void* buffer, std::uint32_t& size) {
    LOCKED(DoReadRecord(key, buffer, size));
}
bool cGZPersistDBSegment::WriteRecord(const cGZPersistResourceKey& key, void* buffer, std::uint32_t size) {
    LOCKED(DoWriteRecord(key, buffer, size));
}

bool cGZPersistDBSegment::CloseRecord(cIGZPersistDBRecord** record) {
    cIGZPersistDBRecord* r = *record;
    bool ok = CloseRecord(r);
    if (ok) r->Release();
    return ok;
}

bool cGZPersistDBSegment::AbortRecord(cIGZPersistDBRecord** record) {
    cIGZPersistDBRecord* r = *record;
    bool ok = AbortRecord(r);
    if (ok) r->Release();
    return ok;
}

bool cGZPersistDBSegment::GetResourceKeyList(cIGZPersistResourceKeyList& list) {
    return GetResourceKeyList(&list, nullptr) != 0;
}

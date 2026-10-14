#include "sc4/AutoRecord.h"

#include "sc4/DBSegment.h"

namespace sc4 {

namespace {

// The segment behind a cISC4DBSegment*-capable stream (null when the stream is not one).
template <class DBStream>
cIGZPersistDBSegment* SegmentOf(cIGZUnknown& stream, std::uint32_t iid) {
    void* raw = nullptr;
    if (!stream.QueryInterface(iid, &raw)) return nullptr;
    auto* db = static_cast<DBStream*>(raw);
    cISC4DBSegment* segment = db->GetSegment();
    cIGZPersistDBSegment* persist = segment ? segment->AsIGZPersistDBSegment() : nullptr;
    db->Release();
    return persist;
}

// Closes the record in its segment (releases through the owning handles).
void CloseRecordIn(cRZAutoRefCount<cIGZPersistDBSegment>& segment, cRZAutoRefCount<cIGZPersistDBSerialRecord>& record) {
    if (!segment) {
        record = nullptr;   // opened from a stream view: nothing to close, just let go
        return;
    }
    if (record) {
        cIGZPersistDBRecord* raw = record->AsIGZPersistDBRecord();
        segment->CloseRecord(raw);
        record = nullptr;
    }
    segment = nullptr;
}

}  // namespace

// ---- load ----------------------------------------------------------------------------------------
cSC4AutoLoadRecord::cSC4AutoLoadRecord(cIGZPersistDBSegment* segment, std::uint32_t type, std::uint32_t group,
                                       std::uint32_t instance) {
    OpenRecord(segment, cGZPersistResourceKey{type, group, instance});
}

cSC4AutoLoadRecord::cSC4AutoLoadRecord(cIGZIStream& stream, std::uint32_t type, std::uint32_t group,
                                       std::uint32_t instance) {
    OpenRecord(SegmentOf<cISC4DBSegmentIStream>(stream, kGZIID_cISC4DBSegmentIStream),
               cGZPersistResourceKey{type, group, instance});
}

bool cSC4AutoLoadRecord::OpenRecord(cIGZPersistDBSegment* segment, const cGZPersistResourceKey& key) {
    if (!segment) return false;
    segment_ = segment;
    cIGZPersistDBRecord* raw = nullptr;
    if (!segment_->OpenRecord(key, &raw, 1)) return false;
    bool ok = false;
    void* serial = nullptr;
    if (raw->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &serial)) {
        auto* record = static_cast<cIGZPersistDBSerialRecord*>(serial);
        record_ = record;
        record->Release();     // QueryInterface's reference now belongs to record_
        ok = record_->GetFieldUint16(version_);
        if (!ok) {
            record_ = nullptr;
            segment_->CloseRecord(raw);
            segment_ = nullptr;
        }
    }
    raw->Release();
    return ok;
}

void cSC4AutoLoadRecord::Close() { CloseRecordIn(segment_, record_); }

// ---- save ----------------------------------------------------------------------------------------
cSC4AutoSaveRecord::cSC4AutoSaveRecord(cIGZPersistDBSegment* segment, std::uint32_t type, std::uint32_t group,
                                       std::uint32_t instance, std::uint16_t version) {
    OpenRecord(segment, cGZPersistResourceKey{type, group, instance}, version);
}

cSC4AutoSaveRecord::cSC4AutoSaveRecord(cIGZOStream& stream, std::uint32_t type, std::uint32_t group,
                                       std::uint32_t instance, std::uint16_t version) {
    version_ = version;
    OpenRecord(SegmentOf<cISC4DBSegmentOStream>(stream, kGZIID_cISC4DBSegmentOStream),
               cGZPersistResourceKey{type, group, instance}, version);
}

bool cSC4AutoSaveRecord::OpenRecord(cIGZPersistDBSegment* segment, const cGZPersistResourceKey& key,
                                    std::uint16_t version) {
    version_ = version;
    if (!segment) return false;
    segment_ = segment;
    cIGZPersistDBRecord* raw = nullptr;
    if (!segment_->OpenRecord(key, &raw, 3)) return false;   // read | write (creating)
    bool ok = false;
    void* serial = nullptr;
    if (raw->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &serial)) {
        auto* record = static_cast<cIGZPersistDBSerialRecord*>(serial);
        record_ = record;
        record->Release();
        ok = record_->SetFieldUint16(version_);
        if (!ok) {
            record_ = nullptr;
            segment_->CloseRecord(raw);
            segment_ = nullptr;
        }
    }
    raw->Release();
    return ok;
}

void cSC4AutoSaveRecord::Close() { CloseRecordIn(segment_, record_); }

}  // namespace sc4

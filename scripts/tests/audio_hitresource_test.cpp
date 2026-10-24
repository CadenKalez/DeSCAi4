#include "audio/HitList.h"
#include "audio/HitManAccess.h"
#include "audio/HitResource.h"
#include "gz/cIGZPersistDBSerialRecord.h"
#include "rz/cRZString.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>

namespace {
struct FakeHM final : RZ::cHitManRegisterView {
    std::int32_t GetRegisterValue(std::int32_t) const override { return 0; }
    std::int32_t Random(std::int32_t, std::int32_t) override { return 0; }
} g_hm;

static_assert(HitIds::kIidRecordAccess == kGZIID_cIGZPersistDBSerialRecord,
              "cHitList::ReadResource queries 0xc56b8f08 for the serial-record view of the stream");

// The record view a persist-DB stream answers 0xc56b8f08 with. cIGZPersistDBSerialRecord's only
// operations the original uses are GetSize (+0x1a0) and GetFieldVoid (+0xd8).
struct FakeRecord final : cIGZPersistDBSerialRecordStub {
    explicit FakeRecord(std::vector<std::uint8_t> bytesIn) : bytes(std::move(bytesIn)) {}

    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == HitIds::kIidRecordAccess || iid == kGZIID_cIGZUnknown) {
            *out = this;
            ++refCount;
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return ++refCount; }
    std::uint32_t Release() override {
        if (--refCount == 0) { delete this; return 0; }
        return refCount;
    }
    std::uint32_t GetSize() override { return static_cast<std::uint32_t>(bytes.size()); }
    bool GetFieldVoid(void* destination, std::uint32_t byteCount) override {
        if (byteCount > bytes.size()) return false;
        std::memcpy(destination, bytes.data(), byteCount);
        return true;
    }

    std::vector<std::uint8_t> bytes;
    std::uint32_t refCount = 1;
};

// Minimal cIGZIStream / cIGZOStream fakes (the real cRZIStream/cRZOStream live in cRZStream.cpp,
// which this test does not link). The input stream, like cGZPersistDBSerialRecord::IStreamView,
// answers the serial-record id with the owning record (one counted reference).
class FakeInStream final : public cIGZIStream {
public:
    FakeInStream(FakeRecord& record, bool exposeRecord = true) : record_(record), expose_(exposeRecord) {}
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (expose_ && iid == HitIds::kIidRecordAccess) {
            *out = &record_;
            record_.AddRef();
            return true;
        }
        if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZIStream) {
            *out = this;
            ++refs_;
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override { return --refs_; }  // the test owns it on the stack
    bool Skip(std::uint32_t) override { return false; }
    bool GetSint8(std::int8_t&) override { return false; }
    bool GetUint8(std::uint8_t&) override { return false; }
    bool GetSint16(std::int16_t&) override { return false; }
    bool GetUint16(std::uint16_t&) override { return false; }
    bool GetSint32(std::int32_t&) override { return false; }
    bool GetUint32(std::uint32_t&) override { return false; }
    bool GetSint64(std::int64_t&) override { return false; }
    bool GetUint64(std::uint64_t&) override { return false; }
    bool GetFloat32(float&) override { return false; }
    bool GetFloat64(double&) override { return false; }
    bool GetRZCharStr(char*, std::uint32_t) override { return false; }
    bool GetGZStr(cIGZString&) override { return false; }
    bool GetGZSerializable(cIGZSerializable&) override { return false; }
    bool GetVoid(void*, std::uint32_t) override { return false; }
    bool GetError() const override { return false; }
    void SetUserData(cIGZVariant*) override {}
    cIGZVariant* GetUserData() override { return nullptr; }

private:
    FakeRecord& record_;
    bool expose_;
    std::uint32_t refs_ = 1;
};

class FakeOutStream final : public cIGZOStream {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZOStream) {
            *out = this;
            ++refs_;
            return true;
        }
        return false;
    }
    std::uint32_t AddRef() override { return ++refs_; }
    std::uint32_t Release() override { return --refs_; }
    void Flush() override {}
    bool SetSint8(std::int8_t) override { return true; }
    bool SetUint8(std::uint8_t) override { return true; }
    bool SetSint16(std::int16_t) override { return true; }
    bool SetUint16(std::uint16_t) override { return true; }
    bool SetSint32(std::int32_t) override { return true; }
    bool SetUint32(std::uint32_t value) override { words.push_back(value); return true; }
    bool SetSint64(std::int64_t) override { return true; }
    bool SetUint64(std::uint64_t) override { return true; }
    bool SetFloat32(float) override { return true; }
    bool SetFloat64(double) override { return true; }
    bool SetRZCharStr(const char*) override { return true; }
    bool SetGZStr(const cIGZString&) override { return true; }
    bool SetGZSerializable(const cIGZSerializable&) override { return true; }
    bool SetVoid(const void*, std::uint32_t) override { return true; }
    bool GetError() const override { return false; }
    void SetUserData(cIGZVariant*) override {}
    cIGZVariant* GetUserData() override { return nullptr; }

    std::vector<std::uint32_t> words;

private:
    std::uint32_t refs_ = 1;
};

std::vector<std::uint8_t> Dwords(std::initializer_list<std::uint32_t> words) {
    std::vector<std::uint8_t> bytes(words.size() * 4);
    std::memcpy(bytes.data(), words.begin(), bytes.size());
    return bytes;
}

// text layout: dword byte-length, then the "id,lo-hi,..." payload (the original NUL-terminates it).
std::vector<std::uint8_t> TextRecord(const char* payload) {
    const auto length = static_cast<std::uint32_t>(std::strlen(payload));
    std::vector<std::uint8_t> bytes(length + 4);
    std::memcpy(bytes.data(), &length, 4);
    std::memcpy(bytes.data() + 4, payload, length);
    return bytes;
}

std::vector<std::int32_t> Contents(cHitList& list) {
    std::vector<std::int32_t> values;
    std::int32_t value = 0;
    for (std::uint32_t i = 0; list.GetIntValueByIndex(i, &value); ++i) values.push_back(value);
    return values;
}

}  // namespace
namespace RZ { cHitManRegisterView* HM() { return &g_hm; } }

int main() {
    const auto before = cHitList::LiveListCount();
    {
        auto* res = new cHitResource<cHitList>();
        res->AddValue(5); res->AddValue(6);
        res->SetKey({HitIds::kResTypeHitList, 0x11, 0x22});
        assert(res->GetType() == HitIds::kResTypeHitList && res->GetGroup() == 0x11 && res->GetInstance() == 0x22);
        res->SetInstance(0x33); assert(res->GetKey().instance == 0x33);

        // persist-resource ids return the resource sub-object, others go to the payload
        void* p = nullptr;
        assert(res->QueryInterface(HitIds::kIidPersistResource, &p));
        assert(p == static_cast<cIHitPersistResource*>(res) && p != static_cast<void*>(res));
        void* q = nullptr;
        assert(res->QueryInterface(kGZIID_cIHitList, &q) && q == static_cast<cHitList*>(res));
        assert(!res->QueryInterface(0xdeadbeef, &q));
        assert(res->GetRefCount() == 2);       // two successful queries took references

        // private copy: same payload and key, independent object
        void* c = nullptr;
        assert(res->CreatePrivateCopy(HitIds::kIidPersistResource, &c));
        auto* copy = static_cast<cIHitPersistResource*>(c);
        assert(copy->GetType() == HitIds::kResTypeHitList && copy->GetInstance() == 0x33);
        assert(copy->GetRefCount() == 3);      // refcount copied (2) + the query reference
        res->AddValue(7); assert(res->NumItems() == 3);
        auto* copyList = static_cast<cHitResource<cHitList>*>(static_cast<cIHitResource*>(copy));
        assert(copyList->NumItems() == 2);

        copy->Release(); copy->Release(); copy->Release();   // drop to deletion (count 3 -> 0)
        assert(res->Release() == 1);                          // two query references outstanding
        assert(res->Release() == 0);                          // last one deletes the object
    }
    assert(cHitList::LiveListCount() == before);

    // ---------------------------------------------------------------------------------------------
    // DebugInstanceTag / DebugClassTag / DebugTypeTag @ 0x1004755e6/ee/f6: `return false`, and the
    // string is not touched (the binary never writes to the argument).
    // ---------------------------------------------------------------------------------------------
    {
        cHitResource<cHitList> res;
        cRZString tag("unchanged");
        assert(!res.DebugInstanceTag(tag));
        assert(!res.DebugClassTag(tag));
        assert(!res.DebugTypeTag(tag));
        assert(tag.str() == "unchanged");
    }

    // ---------------------------------------------------------------------------------------------
    // Read @ 0x1004755fe -> cHitList::ReadResource @ 0x100482366. The stream is asked for the
    // serial-record view, the whole record is copied out, the three accepted layouts are parsed and
    // the record reference the QueryInterface took is dropped again.
    // ---------------------------------------------------------------------------------------------
    {
        auto* record = new FakeRecord(TextRecord("1,3-5,0x10,7"));
        FakeInStream stream(*record);
        cHitResource<cHitList> res;
        assert(res.Read(stream, 0, nullptr));
        assert(record->refCount == 1);  // QI's reference was released
        assert((Contents(res) == std::vector<std::int32_t>{1, 3, 4, 5, 16, 7}));
        assert(res.NumItems() == 6);
        // ReadResource ends with SetCriterion(size/3 + 1), so 6 ids give a history of 3: four picks
        // and nothing was aged out yet.
        assert(res.Choose() == 1 && res.Choose() == 3 && res.Choose() == 4 && res.Choose() == 5);
        assert(res.NumItems() == 6);
        record->Release();
    }
    {
        auto* record = new FakeRecord(Dwords({1, 3, 10, 20, 30}));  // v1: dword 1, count, values
        FakeInStream stream(*record);
        cHitList list;
        assert(list.ReadResource(stream, 0, nullptr));
        assert((Contents(list) == std::vector<std::int32_t>{10, 20, 30}));
        record->Release();
    }
    {
        auto* record = new FakeRecord(Dwords({3, 10, 20, 30}));     // v2: count, values
        FakeInStream stream(*record);
        cHitList list;
        assert(list.ReadResource(stream, 0, nullptr));
        assert((Contents(list) == std::vector<std::int32_t>{10, 20, 30}));
        record->Release();
    }
    {
        // Unrecognised layout: false, and nothing is appended or re-criterioned.
        auto* record = new FakeRecord(Dwords({5, 1}));
        FakeInStream stream(*record);
        cHitList list;
        list.AddValue(42);
        assert(!list.ReadResource(stream, 0, nullptr));
        assert((Contents(list) == std::vector<std::int32_t>{42}));
        record->Release();
    }
    {
        // A stream without the serial-record view: QueryInterface fails. (The original dereferences
        // the null record here and crashes; the port takes the safe failure path, see HitList.cpp.)
        auto* record = new FakeRecord(TextRecord("1,2"));
        FakeInStream stream(*record, /*exposeRecord=*/false);
        cHitResource<cHitList> res;
        assert(!res.Read(stream, 0, nullptr));
        assert(res.NumItems() == 0 && record->refCount == 1);
        record->Release();
    }

    // ---------------------------------------------------------------------------------------------
    // Write @ 0x100475608 -> cHitList::WriteResource @ 0x100482812: dword 1, item count, then the
    // available ids and the recent ids in order. The original rotates each list while writing; the
    // order is preserved and the port iterates without mutating (asserted below).
    // ---------------------------------------------------------------------------------------------
    {
        cHitResource<cHitList> res;
        for (std::int32_t i = 1; i <= 5; ++i) res.AddValue(i);
        res.SetCriterion(2);
        assert(res.Choose() == 1 && res.Choose() == 2);  // available [3,4,5], recent [1,2]
        FakeOutStream stream;
        assert(res.Write(stream, 0, nullptr));
        assert((stream.words == std::vector<std::uint32_t>{1, 5, 3, 4, 5, 1, 2}));
        assert((Contents(res) == std::vector<std::int32_t>{3, 4, 5, 1, 2}));  // write did not rotate for real
        assert(res.NumItems() == 5);
    }
    {
        cHitResource<cHitList> res;  // empty list: just [1, 0]
        FakeOutStream stream;
        assert(res.Write(stream, 0, nullptr));
        assert((stream.words == std::vector<std::uint32_t>{1, 0}));
    }

    assert(HitIds::kClsidHitList == HitIds::kResTypeHitList);  // list class id doubles as its resource type
    std::puts("hit resource ok");
}

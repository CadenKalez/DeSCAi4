#include "gz/cGZPersistDBSerialRecord.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "gz/cIGZSerializable.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

namespace {

// Narrow-to-UTF8 view of UTF-16 code units (no surrogate pairing beyond the BMP pairs).
void AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) out.push_back(static_cast<char>(cp));
    else if (cp < 0x800) { out.push_back(static_cast<char>(0xc0 | (cp >> 6))); out.push_back(static_cast<char>(0x80 | (cp & 0x3f))); }
    else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Stream/audio views handed out by QueryInterface

class cGZPersistDBSerialRecord::IStreamView final : public cIGZIStream {
public:
    explicit IStreamView(cGZPersistDBSerialRecord* owner) : owner_(owner) {}
    bool QueryInterface(std::uint32_t iid, void** out) override { return owner_->QueryInterface(iid, out); }
    std::uint32_t AddRef() override { return owner_->AddRef(); }
    std::uint32_t Release() override { return owner_->Release(); }
    bool Skip(std::uint32_t n) override { return owner_->Skip(n); }
    bool GetSint8(std::int8_t& v) override { return owner_->GetFieldSint8(v); }
    bool GetUint8(std::uint8_t& v) override { return owner_->GetFieldUint8(v); }
    bool GetSint16(std::int16_t& v) override { return owner_->GetFieldSint16(v); }
    bool GetUint16(std::uint16_t& v) override { return owner_->GetFieldUint16(v); }
    bool GetSint32(std::int32_t& v) override { return owner_->GetFieldSint32(v); }
    bool GetUint32(std::uint32_t& v) override { return owner_->GetFieldUint32(v); }
    bool GetSint64(std::int64_t& v) override { return owner_->GetFieldSint64(v); }
    bool GetUint64(std::uint64_t& v) override { return owner_->GetFieldUint64(v); }
    bool GetFloat32(float& v) override { return owner_->GetFieldFloat32(v); }
    bool GetFloat64(double& v) override { return owner_->GetFieldFloat64(v); }
    bool GetRZCharStr(char* b, std::uint32_t n) override { return owner_->GetRZCharStr(b, n); }
    bool GetGZStr(cIGZString& v) override { return owner_->GetGZStr(v); }
    bool GetGZSerializable(cIGZSerializable& object) override { return object.Read(*this); }
    bool GetVoid(void* d, std::uint32_t n) override { return owner_->GetVoid(d, n); }
    bool GetError() const override { return owner_->GetError(); }
    void SetUserData(cIGZVariant* u) override {
        if (u) u->AddRef();
        if (userData_) userData_->Release();
        userData_ = u;
    }
    cIGZVariant* GetUserData() override { return userData_; }
    ~IStreamView() { if (userData_) userData_->Release(); }

private:
    cGZPersistDBSerialRecord* owner_;
    cIGZVariant* userData_ = nullptr;
};

class cGZPersistDBSerialRecord::OStreamView final : public cIGZOStream {
public:
    explicit OStreamView(cGZPersistDBSerialRecord* owner) : owner_(owner) {}
    bool QueryInterface(std::uint32_t iid, void** out) override { return owner_->QueryInterface(iid, out); }
    std::uint32_t AddRef() override { return owner_->AddRef(); }
    std::uint32_t Release() override { return owner_->Release(); }
    void Flush() override { owner_->Flush(); }
    bool SetSint8(std::int8_t v) override { return owner_->SetFieldSint8(v); }
    bool SetUint8(std::uint8_t v) override { return owner_->SetFieldUint8(v); }
    bool SetSint16(std::int16_t v) override { return owner_->SetFieldSint16(v); }
    bool SetUint16(std::uint16_t v) override { return owner_->SetFieldUint16(v); }
    bool SetSint32(std::int32_t v) override { return owner_->SetFieldSint32(v); }
    bool SetUint32(std::uint32_t v) override { return owner_->SetFieldUint32(v); }
    bool SetSint64(std::int64_t v) override { return owner_->SetFieldSint64(v); }
    bool SetUint64(std::uint64_t v) override { return owner_->SetFieldUint64(v); }
    bool SetFloat32(float v) override { return owner_->SetFieldFloat32(v); }
    bool SetFloat64(double v) override { return owner_->SetFieldFloat64(v); }
    bool SetRZCharStr(const char* s) override { return owner_->SetRZCharStr(s); }
    bool SetGZStr(const cIGZString& s) override { return owner_->SetGZStr(s); }
    bool SetGZSerializable(const cIGZSerializable& object) override { return object.Write(*this); }
    bool SetVoid(const void* s, std::uint32_t n) override { return owner_->SetVoid(s, n); }
    bool GetError() const override { return owner_->GetError(); }
    void SetUserData(cIGZVariant* u) override {
        if (u) u->AddRef();
        if (userData_) userData_->Release();
        userData_ = u;
    }
    cIGZVariant* GetUserData() override { return userData_; }
    ~OStreamView() { if (userData_) userData_->Release(); }

private:
    cGZPersistDBSerialRecord* owner_;
    cIGZVariant* userData_ = nullptr;
};

// ---------------------------------------------------------------------------------------------

cGZPersistDBSerialRecord::cGZPersistDBSerialRecord(const cGZPersistResourceKey& key, cIGZPersistDBSegment* segment,
                                                   bool littleEndian)
    : key_(key), segment_(segment), littleEndian_(littleEndian) {
    if (segment_) segment_->AddRef();
}

cGZPersistDBSerialRecord::cGZPersistDBSerialRecord(cIGZPersistDBSegment* segment, bool littleEndian)
    : segment_(segment), littleEndian_(littleEndian) {
    if (segment_) segment_->AddRef();
}

cGZPersistDBSerialRecord::~cGZPersistDBSerialRecord() {
    if (segment_) segment_->Release();
}

bool cGZPersistDBSerialRecord::QueryInterface(std::uint32_t iid, void** out) {
    switch (iid) {
        case kGZIID_cIGZOStream: *out = new OStreamView(this); break;
        case 0x051b11bd: *out = new IStreamView(this); break;   // cIGZIStream
        case kGZIID_cIGZPersistDBSerialRecord:
            *out = static_cast<cIGZPersistDBSerialRecord*>(this);
            break;
        case kGZIID_cIGZUnknown:
        case kGZIID_cIGZPersistDBRecord:
            *out = static_cast<cIGZPersistDBRecord*>(this);
            break;
        default: return false;
    }
    AddRef();
    return true;
}

std::uint32_t cGZPersistDBSerialRecord::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cGZPersistDBSerialRecord::Close() {
    if (!segment_) return false;
    bool ok = segment_->CloseRecord(static_cast<cIGZPersistDBRecord*>(this));
    if (segment_) {
        segment_->Release();
        segment_ = nullptr;
    }
    return ok;
}

bool cGZPersistDBSerialRecord::GetFieldResKey(cGZPersistResourceKey& v) {
    return GetFieldUint32(v.type) && GetFieldUint32(v.group) && GetFieldUint32(v.instance);
}

bool cGZPersistDBSerialRecord::SetFieldResKey(const cGZPersistResourceKey& v) {
    return SetFieldUint32(v.type) && SetFieldUint32(v.group) && SetFieldUint32(v.instance);
}

bool cGZPersistDBSerialRecord::SetFieldString(const cIGZString& v) {
    std::uint32_t length = v.Strlen();
    if (!SetFieldUint32(length)) return false;
    return SetFieldVoid(v.Data(), length);
}

bool cGZPersistDBSerialRecord::SetRZCharStr(const char* s) {
    std::string str(s);
    cIGZString* none = nullptr;
    (void)none;
    std::uint32_t length = static_cast<std::uint32_t>(str.size());
    if (!SetFieldUint32(length)) return false;
    return SetFieldVoid(str.data(), length);
}

// Layout: u32 header (low 24 bits = character count, top byte 0x10 = UTF-16 payload, 0 = narrow),
// followed by the characters. Quirks kept: headers with another top byte are the pre-header legacy
// form (the whole record is the string, read one byte at a time); a narrow string whose second
// byte is 0 is decoded as UTF-16; more than 200000 characters is refused.
bool cGZPersistDBSerialRecord::GetFieldString(cIGZString& out) {
    std::uint32_t header = 0;
    if (!GetFieldUint32(header)) {
        out.AssignFromChars("", 0);
        std::uint8_t c;
        while (GetFieldUint8(c)) out.AppendChars(reinterpret_cast<const char*>(&c), 1);
        return true;
    }
    std::uint32_t top = header >> 24;
    if ((top | 0x10) != 0x10) {  // legacy: no header, rewind the 4 bytes we consumed
        std::uint32_t size = GetSize();
        SeekRelative(-4);
        out.AssignFromChars("", 0);
        for (std::uint32_t i = 0; i < size; ++i) {
            std::uint8_t c = 0;
            GetFieldUint8(c);
            out.AppendChars(reinterpret_cast<const char*>(&c), 1);
        }
        return true;
    }
    std::uint32_t length = header & 0xffffff;
    if (length > 200000) return false;
    std::uint32_t byteCount = length << (top == 0x10 ? 1 : 0);
    std::vector<std::uint8_t> data(byteCount ? byteCount : 1);
    if (byteCount && !GetFieldVoid(data.data(), byteCount)) return false;
    bool wide = top == 0x10 || (byteCount >= 2 && data[1] == 0);
    if (!wide) {
        out.AssignFromChars(reinterpret_cast<const char*>(data.data()), length);
        return true;
    }
    std::string utf8;
    std::uint32_t units = length;
    for (std::uint32_t i = 0; i < units; ++i) {
        std::uint16_t u;
        std::memcpy(&u, &data[i * 2], 2);
        if (!littleEndian_) u = persist_detail::SwapBytes(u);
        std::uint32_t cp = u;
        if (u >= 0xd800 && u < 0xdc00 && i + 1 < units) {
            std::uint16_t lo;
            std::memcpy(&lo, &data[(i + 1) * 2], 2);
            if (!littleEndian_) lo = persist_detail::SwapBytes(lo);
            if (lo >= 0xdc00 && lo < 0xe000) {
                cp = 0x10000 + ((u - 0xd800u) << 10) + (lo - 0xdc00u);
                ++i;
            }
        }
        AppendUtf8(utf8, cp);
    }
    out.AssignFromChars(utf8.data(), static_cast<std::uint32_t>(utf8.size()));
    return true;
}

namespace {
// kRZToDBTable (0x100a1e180): cIGZVariant type (low 15 bits) -> stored type code.
constexpr std::uint32_t kRZToDB[20] = {0, 11, 1, 5, 2, 6, 3, 7, 4, 8, 9, 10, 17, 16, 12, 18, 13, 14, 15, 19};
constexpr std::uint32_t kStoredArrayFlag = 0x8000;
}  // namespace

// Stored form: u32 type (kRZToDB code | 0x8000 for arrays), then for scalars the value in its own
// width, for arrays u32 count and the elements (strings: a length-prefixed string field).
bool cGZPersistDBSerialRecord::GetFieldVariant(cIGZVariant** out) {
    std::uint32_t type = 0;
    if (!GetFieldUint32(type)) return false;
    auto* variant = new cRZVariant();
    variant->AddRef();
    auto fail = [&] { variant->Release(); return false; };
    auto commit = [&] { *out = variant; return true; };
    std::uint32_t count = 0;
    if (type & kStoredArrayFlag) {
        if (type == 0x800d || type == 0x800e || type == 0x800f) return fail();
        if (type == 0x800c) {
            cRZString text;
            if (!GetFieldString(text)) return fail();
            variant->RefRZChar(text.Data(), text.Strlen());
            return commit();
        }
        if (!GetFieldUint32(count)) return fail();
        switch (type) {
            case 0x8000: { std::vector<char> b(count); bool ok = GetFieldVoid(b.data(), count); variant->RefVoid(b.data(), count); if (!ok) return fail(); return commit(); }
            case 0x8001: case 0x800b: case 0x8011: { std::vector<std::uint8_t> b(count); bool ok = GetFieldUint8(b.data(), count);
                if (type == 0x800b) variant->RefBool(reinterpret_cast<const bool*>(b.data()), count);
                else if (type == 0x8011) variant->RefChar(reinterpret_cast<const char*>(b.data()), count);
                else variant->RefUint8(b.data(), count);
                return ok ? commit() : fail(); }
            case 0x8002: case 0x8010: { std::vector<std::uint16_t> b(count); bool ok = GetFieldUint16(b.data(), count);
                if (type == 0x8010) variant->RefRZUnicodeChar(b.data(), count); else variant->RefUint16(b.data(), count);
                return ok ? commit() : fail(); }
            case 0x8003: { std::vector<std::uint32_t> b(count); bool ok = GetFieldUint32(b.data(), count); variant->RefUint32(b.data(), count); return ok ? commit() : fail(); }
            case 0x8004: { std::vector<std::uint64_t> b(count); bool ok = GetFieldUint64(b.data(), count); variant->RefUint64(b.data(), count); return ok ? commit() : fail(); }
            case 0x8005: { std::vector<std::int8_t> b(count); bool ok = GetFieldSint8(b.data(), count); variant->RefSint8(b.data(), count); return ok ? commit() : fail(); }
            case 0x8006: { std::vector<std::int16_t> b(count); bool ok = GetFieldSint16(b.data(), count); variant->RefSint16(b.data(), count); return ok ? commit() : fail(); }
            case 0x8007: { std::vector<std::int32_t> b(count); bool ok = GetFieldSint32(b.data(), count); variant->RefSint32(b.data(), count); return ok ? commit() : fail(); }
            case 0x8008: { std::vector<std::int64_t> b(count); bool ok = GetFieldSint64(b.data(), count); variant->RefSint64(b.data(), count); return ok ? commit() : fail(); }
            case 0x8009: { std::vector<float> b(count); bool ok = GetFieldFloat32(b.data(), count); variant->RefFloat32(b.data(), count); return ok ? commit() : fail(); }
            case 0x800a: { std::vector<double> b(count); bool ok = GetFieldFloat64(b.data(), count); variant->RefFloat64(b.data(), count); return ok ? commit() : fail(); }
            default: return fail();
        }
    }
    switch (type) {
        case 0: return commit();
        case 1: { std::uint8_t v; if (!GetFieldUint8(v)) return fail(); variant->SetValUint8(v); return commit(); }
        case 2: { std::uint16_t v; if (!GetFieldUint16(v)) return fail(); variant->SetValUint16(v); return commit(); }
        case 3: { std::uint32_t v; if (!GetFieldUint32(v)) return fail(); variant->SetValUint32(v); return commit(); }
        case 4: { std::uint64_t v; if (!GetFieldUint64(v)) return fail(); variant->SetValUint64(v); return commit(); }
        case 5: { std::int8_t v; if (!GetFieldSint8(v)) return fail(); variant->SetValSint8(v); return commit(); }
        case 6: { std::int16_t v; if (!GetFieldSint16(v)) return fail(); variant->SetValSint16(v); return commit(); }
        case 7: { std::int32_t v; if (!GetFieldSint32(v)) return fail(); variant->SetValSint32(v); return commit(); }
        case 8: { std::int64_t v; if (!GetFieldSint64(v)) return fail(); variant->SetValSint64(v); return commit(); }
        case 9: { float v; if (!GetFieldFloat32(v)) return fail(); variant->SetValFloat32(v); return commit(); }
        case 10: { double v; if (!GetFieldFloat64(v)) return fail(); variant->SetValFloat64(v); return commit(); }
        case 11: { std::uint8_t v; if (!GetFieldUint8(v)) return fail(); variant->SetValBool(v != 0); return commit(); }
        case 12: { std::uint8_t v; if (!GetFieldUint8(v)) return fail(); variant->SetValRZChar(static_cast<char>(v)); return commit(); }
        default: return fail();
    }
}

bool cGZPersistDBSerialRecord::SetFieldVariant(const cIGZVariant* v) {
    const std::uint32_t raw = v->GetType();
    const std::uint32_t base = raw & 0x7fff;
    const std::uint32_t type = (raw & kStoredArrayFlag) | (base < 20 ? kRZToDB[base] : 0);
    if (!SetFieldUint32(type)) return false;
    if (type & kStoredArrayFlag) {
        if (type == 0x800c) {
            const char* data = reinterpret_cast<const char*>(v->RefRZChar());
            cRZString text;
            text.AssignFromChars(data, v->GetCount());
            return SetFieldString(text);
        }
        const std::uint32_t count = v->GetCount();
        if (!SetFieldUint32(count)) return false;
        switch (type) {
            case 0x8000: return SetFieldVoid(v->RefVoid(), count);
            case 0x8001: return SetFieldUint8(v->RefUint8(), count);
            case 0x800b: return SetFieldUint8(reinterpret_cast<const std::uint8_t*>(v->RefBool()), count);
            case 0x8011: return SetFieldUint8(reinterpret_cast<const std::uint8_t*>(v->RefChar()), count);
            case 0x8002: return SetFieldUint16(v->RefUint16(), count);
            case 0x8010: return SetFieldUint16(v->RefRZUnicodeChar(), count);
            case 0x8003: return SetFieldUint32(v->RefUint32(), count);
            case 0x8004: return SetFieldUint64(v->RefUint64(), count);
            case 0x8005: return SetFieldSint8(v->RefSint8(), count);
            case 0x8006: return SetFieldSint16(v->RefSint16(), count);
            case 0x8007: return SetFieldSint32(v->RefSint32(), count);
            case 0x8008: return SetFieldSint64(v->RefSint64(), count);
            case 0x8009: return SetFieldFloat32(v->RefFloat32(), count);
            case 0x800a: return SetFieldFloat64(v->RefFloat64(), count);
            default: return false;
        }
    }
    switch (type) {
        case 0: return true;
        case 1: return SetFieldUint8(v->AsUint8());
        case 2: return SetFieldUint16(v->AsUint16());
        case 3: return SetFieldUint32(v->AsUint32());
        case 4: return SetFieldUint64(v->AsUint64());
        case 5: return SetFieldSint8(v->AsSint8());
        case 6: return SetFieldSint16(v->AsSint16());
        case 7: return SetFieldSint32(v->AsSint32());
        case 8: return SetFieldSint64(v->AsSint64());
        case 9: return SetFieldFloat32(v->AsFloat32());
        case 10: return SetFieldFloat64(v->AsFloat64());
        case 11: return SetFieldUint8(v->AsBool() ? 1 : 0);
        case 12: case 17: return SetFieldUint8(static_cast<std::uint8_t>(v->AsRZChar()));
        case 16: return SetFieldUint16(v->AsRZUnicodeChar());
        default: return false;
    }
}


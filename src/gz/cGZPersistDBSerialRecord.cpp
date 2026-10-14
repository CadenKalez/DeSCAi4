#include "gz/cGZPersistDBSerialRecord.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "gz/cIGZSerializable.h"

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

bool cGZPersistDBSerialRecord::GetFieldVariant(cIGZVariant**) { return false; }
bool cGZPersistDBSerialRecord::SetFieldVariant(const cIGZVariant*) { return false; }


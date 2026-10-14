#include "sc4/TokenRecord.h"

#include <cstdio>
#include <cstring>

namespace sc4 {

namespace {
cIGZCOMClassFactory* g_com = nullptr;
}
cIGZCOMClassFactory* GZCOM() { return g_com; }
void SetGZCOM(cIGZCOMClassFactory* com) { g_com = com; }

// =============================================================================================
// binary IO
// =============================================================================================
bool cSLTokenSerialRecordIO::ReadTokenDef(cGZPersistTokenDef& def) {
    const bool fits = Advance(8);
    if (!fits) return false;
    if (!record_->GetFieldUint32(def.id)) return false;
    if (!record_->GetFieldUint16(def.type)) return false;
    return record_->GetFieldUint16(def.count);
}

#define SL_BINARY_READ(T, Method, Size)                                  \
    bool cSLTokenSerialRecordIO::Read(T& v) {                            \
        return Advance(Size) && record_->Method(v);                      \
    }
SL_BINARY_READ(std::uint8_t, GetFieldUint8, 1)
SL_BINARY_READ(std::int8_t, GetFieldSint8, 1)
SL_BINARY_READ(std::uint16_t, GetFieldUint16, 2)
SL_BINARY_READ(std::int16_t, GetFieldSint16, 2)
SL_BINARY_READ(std::uint32_t, GetFieldUint32, 4)
SL_BINARY_READ(std::int32_t, GetFieldSint32, 4)
SL_BINARY_READ(std::uint64_t, GetFieldUint64, 8)
SL_BINARY_READ(std::int64_t, GetFieldSint64, 8)
SL_BINARY_READ(float, GetFieldFloat32, 4)
SL_BINARY_READ(double, GetFieldFloat64, 8)
SL_BINARY_READ(cGZPersistResourceKey, GetFieldResKey, 12)
#undef SL_BINARY_READ

bool cSLTokenSerialRecordIO::Read(cRZString& v) {
    if (!record_->GetFieldString(v)) return false;
    position_ += v.Strlen() + 4;
    return position_ <= limit_;
}

bool cSLTokenSerialRecordIO::WriteTokenDef(cRZString&, cGZPersistTokenDef& def) {
    const bool ok = record_->SetFieldUint32(def.id);
    if (ok) {
        record_->SetFieldUint16(def.type);
        record_->SetFieldUint16(def.count);
    }
    return ok;
}

bool cSLTokenSerialRecordIO::Write(std::uint8_t& v) { return record_->SetFieldUint8(v); }
bool cSLTokenSerialRecordIO::Write(std::int8_t& v) { return record_->SetFieldSint8(v); }
bool cSLTokenSerialRecordIO::Write(std::uint16_t& v) { return record_->SetFieldUint16(v); }
bool cSLTokenSerialRecordIO::Write(std::int16_t& v) { return record_->SetFieldSint16(v); }
bool cSLTokenSerialRecordIO::Write(std::uint32_t& v) { return record_->SetFieldUint32(v); }
bool cSLTokenSerialRecordIO::Write(std::int32_t& v) { return record_->SetFieldSint32(v); }
bool cSLTokenSerialRecordIO::Write(std::uint64_t& v) { return record_->SetFieldUint64(v); }
bool cSLTokenSerialRecordIO::Write(std::int64_t& v) { return record_->SetFieldSint64(v); }
bool cSLTokenSerialRecordIO::Write(float& v) { return record_->SetFieldFloat32(v); }
bool cSLTokenSerialRecordIO::Write(double& v) { return record_->SetFieldFloat64(v); }
bool cSLTokenSerialRecordIO::Write(cRZString& v) { return record_->SetFieldString(v); }
bool cSLTokenSerialRecordIO::Write(cGZPersistResourceKey& v) { return record_->SetFieldResKey(v); }

// =============================================================================================
// text IO
// =============================================================================================
bool cSLTokenSerialRecordIO_Text::GetToken(cRZString& token, char delimiter) {
    token.Erase(0, 0xffffffffu);
    char c = 0;
    for (;;) {
        bool ok = false;
        if (Advance(1)) ok = record_->GetFieldVoid(&c, 1);
        if (c == delimiter) {
            const char nul = 0;
            token.AppendChars(&nul, 1);
            token.Trim();
            return true;
        }
        bool append = true;
        if (c <= '\f') {                // <= 12 as a signed char
            if (c == '\t') c = ' ';
            else if (c == '\n') append = false;
        } else if (c == '\r') {
            c = ' ';
        } else if (c == ';') {
            // comment: skip to the end of the line (the carriage return is consumed)
            if (!ok) return false;
            for (;;) {
                if (!Advance(1)) return false;
                if (!record_->GetFieldVoid(&c, 1)) return false;
                if (c == '\r') break;
            }
            continue;
        }
        if (append) token.AppendChars(&c, 1);
        if (!ok) return false;
    }
}

bool cSLTokenSerialRecordIO_Text::ReadUnsigned(std::uint32_t& v, char delimiter) {
    cRZString token;
    if (GetToken(token, delimiter)) {
        if (std::sscanf(token.ToChar(), "%u", &v) == 1) return true;
        v = 0;
    }
    return false;
}

bool cSLTokenSerialRecordIO_Text::ReadSigned(std::int32_t& v, char delimiter) {
    cRZString token;
    if (GetToken(token, delimiter)) {
        if (std::sscanf(token.ToChar(), "%d", &v) == 1) return true;
        v = 0;
    }
    return false;
}

bool cSLTokenSerialRecordIO_Text::ReadTokenDef(cGZPersistTokenDef& def) {
    cRZString name;
    std::uint32_t id = 0, type = 0, count = 0;
    if (GetToken(name, '[') && ReadUnsigned(id, ',') && ReadUnsigned(type, ',') && ReadUnsigned(count, ']') &&
        GetToken(name, '=')) {
        def.id = id;
        def.type = static_cast<std::uint16_t>(type);
        def.count = static_cast<std::uint16_t>(count);
        return true;
    }
    return false;
}

// Narrow integers are parsed as a 32-bit value and truncated (as the original does).
bool cSLTokenSerialRecordIO_Text::Read(std::uint8_t& v) {
    std::uint32_t wide = 0;
    const bool ok = ReadUnsigned(wide, ',');
    v = static_cast<std::uint8_t>(wide);
    return ok;
}
bool cSLTokenSerialRecordIO_Text::Read(std::int8_t& v) {
    std::uint32_t wide = 0;
    const bool ok = ReadUnsigned(wide, ',');
    v = static_cast<std::int8_t>(wide);
    return ok;
}
bool cSLTokenSerialRecordIO_Text::Read(std::uint16_t& v) {
    std::uint32_t wide = 0;
    const bool ok = ReadUnsigned(wide, ',');
    v = static_cast<std::uint16_t>(wide);
    return ok;
}
bool cSLTokenSerialRecordIO_Text::Read(std::int16_t& v) {
    std::uint32_t wide = 0;
    const bool ok = ReadUnsigned(wide, ',');
    v = static_cast<std::int16_t>(wide);
    return ok;
}

bool cSLTokenSerialRecordIO_Text::Read(std::int64_t& v) {
    cRZString token;
    if (GetToken(token, ',')) {
        long long parsed = 0;
        if (std::sscanf(token.ToChar(), "%lld", &parsed) == 1) {
            v = parsed;
            return true;
        }
        v = 0;
    }
    return false;
}

bool cSLTokenSerialRecordIO_Text::Read(float& v) {
    cRZString token;
    if (!GetToken(token, ',')) return false;
    double parsed = 0;
    const bool ok = std::sscanf(token.ToChar(), "%lf", &parsed) == 1;
    v = ok ? static_cast<float>(parsed) : 0.0f;
    return ok;
}

bool cSLTokenSerialRecordIO_Text::Read(double& v) {
    cRZString token;
    if (GetToken(token, ',')) {
        if (std::sscanf(token.ToChar(), "%lf", &v) == 1) return true;
        v = 0.0;
    }
    return false;
}

bool cSLTokenSerialRecordIO_Text::Read(cGZPersistResourceKey& v) {
    std::uint32_t a = 0, b = 0, c = 0;
    if (!Read(a) || !Read(b) || !Read(c)) return false;
    v.type = a;
    v.group = b;
    v.instance = c;
    return true;
}

bool cSLTokenSerialRecordIO_Text::WriteText(const cRZString& text) {
    return record_->SetFieldVoid(text.Data(), text.Strlen());
}

bool cSLTokenSerialRecordIO_Text::WriteTokenDef(cRZString& name, cGZPersistTokenDef& def) {
    cRZString line;
    line.Sprintf("%c%c%s[ %d, %d, %d ]= ", 0x0d, 0x0a, name.ToChar(), static_cast<int>(def.id),
                 static_cast<int>(def.type), static_cast<int>(def.count));
    return WriteText(line);
}

#define SL_TEXT_WRITE(T, Format, Arg)                      \
    bool cSLTokenSerialRecordIO_Text::Write(T& v) {        \
        cRZString piece;                                   \
        piece.Sprintf(Format, Arg);                        \
        return WriteText(piece);                           \
    }
SL_TEXT_WRITE(std::uint8_t, "%u, ", static_cast<unsigned>(v))
SL_TEXT_WRITE(std::int8_t, "%d, ", static_cast<int>(v))
SL_TEXT_WRITE(std::uint16_t, "%u, ", static_cast<unsigned>(v))
SL_TEXT_WRITE(std::int16_t, "%d, ", static_cast<int>(v))
SL_TEXT_WRITE(std::uint32_t, "%u, ", v)
SL_TEXT_WRITE(std::int32_t, "%d, ", v)
// The original passes the Windows-style "%I64u"/"%I64d" to vsnprintf; on this platform they are
// printed literally ("I64u, "), so 64-bit values are lost in text records. Kept.
SL_TEXT_WRITE(std::uint64_t, "%I64u, ", static_cast<unsigned long long>(v))
SL_TEXT_WRITE(std::int64_t, "%I64d, ", static_cast<long long>(v))
SL_TEXT_WRITE(float, "%f, ", static_cast<double>(v))
SL_TEXT_WRITE(double, "%g, ", v)
#undef SL_TEXT_WRITE

bool cSLTokenSerialRecordIO_Text::Write(cRZString& v) {
    cRZString piece;
    piece.Sprintf("%s, ", v.ToChar());
    return WriteText(piece);
}

bool cSLTokenSerialRecordIO_Text::Write(cGZPersistResourceKey& v) {
    std::uint32_t a = v.type, b = v.group, c = v.instance;
    return Write(a) && Write(b) && Write(c);
}

// =============================================================================================
// cSLTokenRecord
// =============================================================================================
cSLTokenRecord::~cSLTokenRecord() { Shutdown(); }

std::uint32_t cSLTokenRecord::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cSLTokenRecord::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cSLTokenRecord) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

bool cSLTokenRecord::GetKey(cGZPersistResourceKey& key) const {
    if (!record_) return false;
    key = record_->AsIGZPersistDBRecord()->GetKey();
    return true;
}

bool cSLTokenRecord::ReadTokensFromSerialRecord(cSLTokenSerialRecordIO& io) {
    cSLTokenFieldBase* field = nullptr;
    for (;;) {
        cGZPersistTokenDef def;
        bool ok = false;
        if (io.ReadTokenDef(def)) {
            if (def.id == kTokenEndId) return true;
            ok = CreateField(def, &field);
        }
        if (!ok) return false;
        if (!field->ReadData(io)) return false;
    }
}

bool cSLTokenRecord::InitRead(cIGZPersistDBSerialRecord& record, std::uint32_t cookie) {
    if (valid_) return false;
    writing_ = false;
    record_ = &record;
    cookie_ = cookie;
    bool ok = false;
    if (cookie == kTokenCookieBinary) {
        isText_ = false;
        cSLTokenSerialRecordIO io(&record, record.GetSize());
        ok = ReadTokensFromSerialRecord(io);
        valid_ = ok;
    } else if (cookie == kTokenCookieText) {
        isText_ = true;
        cSLTokenSerialRecordIO_Text io(&record, record.GetSize());
        ok = ReadTokensFromSerialRecord(io);
        valid_ = ok;
    }
    if (!ok) Shutdown();
    return valid_;
}

bool cSLTokenRecord::InitRead(cIGZPersistDBSerialRecord& record) {
    if (valid_) return false;
    record_ = &record;
    writing_ = false;
    std::uint32_t cookie = 0;
    if (!record.GetFieldUint32(cookie)) cookie = 0;
    cookie_ = cookie;
    bool ok = false;
    if (cookie == kTokenCookieBinary) {
        isText_ = false;
        cSLTokenSerialRecordIO io(&record, record.GetSize());
        ok = ReadTokensFromSerialRecord(io);
        valid_ = ok;
    } else if (cookie == kTokenCookieText) {
        isText_ = true;
        cSLTokenSerialRecordIO_Text io(&record, record.GetSize());
        ok = ReadTokensFromSerialRecord(io);
        valid_ = ok;
    }
    if (!ok) Shutdown();
    return valid_;
}

bool cSLTokenRecord::InitWriteText(cIGZPersistDBSerialRecord& record) {
    const bool wasValid = valid_;
    if (!wasValid) {
        valid_ = true;
        isText_ = true;
        writing_ = true;
        cookie_ = kTokenCookieText;
        record_ = &record;
    }
    return !wasValid;
}

bool cSLTokenRecord::InitWriteBinary(cIGZPersistDBSerialRecord& record) {
    const bool wasValid = valid_;
    if (!wasValid) {
        valid_ = true;
        isText_ = false;
        writing_ = true;
        cookie_ = kTokenCookieBinary;
        record_ = &record;
    }
    return !wasValid;
}

bool cSLTokenRecord::WriteTokensToSerialRecord(cSLTokenSerialRecordIO& io) {
    bool ok = true;
    for (auto& entry : fields_) {
        cSLTokenFieldBase& field = *entry.second;
        cRZString name(field.GetName());
        cGZPersistTokenDef def = field.GetDef();
        ok = io.WriteTokenDef(name, def) && field.WriteData(io);
        if (!ok) return false;
    }
    // terminator: {1, u32, 1} = 0 named TK_TKR_End
    cRZString endName("TK_TKR_End");
    cGZPersistTokenDef endDef{kTokenEndId, kTokenUint32, 1};
    if (!io.WriteTokenDef(endName, endDef)) return false;
    std::uint32_t zero = 0;
    return io.Write(zero);
}

void cSLTokenRecord::DestroyFields() { fields_.clear(); }

void cSLTokenRecord::Shutdown() {
    if (record_) {
        if (writing_) {
            const std::uint32_t cookie = isText_ ? kTokenCookieText : kTokenCookieBinary;
            const bool wrote = record_->SetFieldUint32(cookie);
            if (!isText_) {
                cSLTokenSerialRecordIO io(record_.get(), record_->GetSize());
                if (wrote) WriteTokensToSerialRecord(io);
            } else {
                cSLTokenSerialRecordIO_Text io(record_.get(), record_->GetSize());
                if (wrote) WriteTokensToSerialRecord(io);
            }
        }
        record_ = nullptr;
    }
    DestroyFields();
    valid_ = false;
}

bool cSLTokenRecord::GetField(const cGZPersistTokenDef& def, cSLTokenFieldBase** out) {
    auto it = fields_.find(def.id);
    if (it != fields_.end()) {
        cSLTokenFieldBase* field = it->second.get();
        *out = field;
        if (field->GetDef() == def) return true;
        *out = nullptr;
    }
    return false;
}

bool cSLTokenRecord::CreateField(const cGZPersistTokenDef& def, cSLTokenFieldBase** out) {
    if (def.count == 0) return false;
    auto it = fields_.find(def.id);
    if (it != fields_.end()) {
        *out = it->second.get();
        if (it->second->GetDef() == def) return false;   // an identical token exists: not created
        // (the original drops the map entry without freeing the old field)
        fields_.erase(it);
    }
    std::unique_ptr<cSLTokenFieldBase> field;
    switch (def.type) {
        case kTokenUint8: field = std::make_unique<cSLTokenField<std::uint8_t>>(def); break;
        case kTokenUint16: field = std::make_unique<cSLTokenField<std::uint16_t>>(def); break;
        case kTokenUint32: field = std::make_unique<cSLTokenField<std::uint32_t>>(def); break;
        case kTokenSint32: field = std::make_unique<cSLTokenField<std::int32_t>>(def); break;
        case kTokenFloat32: field = std::make_unique<cSLTokenField<float>>(def); break;
        case kTokenSint64: field = std::make_unique<cSLTokenField<std::int64_t>>(def); break;
        case kTokenString: field = std::make_unique<cSLTokenField<cRZString>>(def); break;
        case kTokenResKey: field = std::make_unique<cSLTokenField<cGZPersistResourceKey>>(def); break;
        case kTokenSint8: field = std::make_unique<cSLTokenField<std::int8_t>>(def); break;
        case kTokenSint16: field = std::make_unique<cSLTokenField<std::int16_t>>(def); break;
        case kTokenUint64: field = std::make_unique<cSLTokenField<std::uint64_t>>(def); break;
        case kTokenFloat64: field = std::make_unique<cSLTokenField<double>>(def); break;
        default: return false;
    }
    *out = field.get();
    fields_.emplace(def.id, std::move(field));
    return true;
}

bool cSLTokenRecord::IsTokenAvailable(const cGZPersistTokenDef& def) {
    auto it = fields_.find(def.id);
    if (it == fields_.end()) return false;
    const cGZPersistTokenDef& have = it->second->GetDef();
    return have.id == def.id && have.type == def.type && have.count == def.count;
}

bool cSLTokenRecord::SetTokenString(const cGZPersistTokenDef& def, cIGZString& name) {
    cSLTokenFieldBase* field = nullptr;
    if (!GetField(def, &field)) return false;
    field->GetName().AssignFromChars(name.Data(), name.Strlen());
    return true;
}

// Signed/unsigned pairs share the field accessor of the unsigned/wider element type (the original
// reuses the same vtable slot), so e.g. GetFieldSint8 reads through the u8 accessor.
#define SL_GET(Name, T, FieldT)                                                                   \
    bool cSLTokenRecord::GetField##Name(const cGZPersistTokenDef& def, T& v, std::uint32_t i) {  \
        return WithField(def, [&](cSLTokenFieldBase& f) {                                        \
            FieldT& as = reinterpret_cast<FieldT&>(v);                                           \
            return f.Get(as, i);                                                                 \
        });                                                                                      \
    }
#define SL_SET(Name, T, FieldT)                                                                          \
    bool cSLTokenRecord::SetField##Name(const cGZPersistTokenDef& def, const T& v, std::uint32_t i) {   \
        return WithFieldCreate(def, [&](cSLTokenFieldBase& f) {                                         \
            const FieldT& as = reinterpret_cast<const FieldT&>(v);                                      \
            return f.Set(as, i);                                                                        \
        });                                                                                             \
    }
SL_GET(Uint8, std::uint8_t, std::uint8_t)
SL_GET(Sint8, std::int8_t, std::uint8_t)
SL_GET(Uint16, std::uint16_t, std::uint16_t)
SL_GET(Sint16, std::int16_t, std::uint16_t)
SL_GET(Uint32, std::uint32_t, std::uint32_t)
SL_GET(Sint32, std::int32_t, std::int32_t)
SL_GET(Uint64, std::uint64_t, std::int64_t)
SL_GET(Sint64, std::int64_t, std::int64_t)
SL_GET(Float32, float, float)
SL_GET(Float64, double, double)
SL_SET(Uint8, std::uint8_t, std::uint8_t)
SL_SET(Sint8, std::int8_t, std::uint8_t)
SL_SET(Uint16, std::uint16_t, std::uint16_t)
SL_SET(Sint16, std::int16_t, std::uint16_t)
SL_SET(Uint32, std::uint32_t, std::uint32_t)
SL_SET(Sint32, std::int32_t, std::int32_t)
SL_SET(Uint64, std::uint64_t, std::int64_t)
SL_SET(Sint64, std::int64_t, std::int64_t)
SL_SET(Float32, float, float)
SL_SET(Float64, double, double)
#undef SL_GET
#undef SL_SET

bool cSLTokenRecord::GetFieldString(const cGZPersistTokenDef& def, cIGZString& v, std::uint32_t i) {
    return WithField(def, [&](cSLTokenFieldBase& f) { return f.Get(v, i); });
}
bool cSLTokenRecord::GetFieldResKey(const cGZPersistTokenDef& def, cGZPersistResourceKey& v, std::uint32_t i) {
    return WithField(def, [&](cSLTokenFieldBase& f) { return f.Get(v, i); });
}
bool cSLTokenRecord::SetFieldString(const cGZPersistTokenDef& def, const cIGZString& v, std::uint32_t i) {
    return WithFieldCreate(def, [&](cSLTokenFieldBase& f) { return f.Set(v, i); });
}
bool cSLTokenRecord::SetFieldResKey(const cGZPersistTokenDef& def, const cGZPersistResourceKey& v, std::uint32_t i) {
    return WithFieldCreate(def, [&](cSLTokenFieldBase& f) { return f.Set(v, i); });
}

// =============================================================================================
// resource factory
// =============================================================================================
bool cSLTokenResourceFactory::CreateInstance(std::uint32_t type, std::uint32_t iid, void** out) {
    cIGZCOMClassFactory* com = GZCOM();
    return com && com->CreateInstance(type, iid, out);
}

std::uint32_t cSLTokenResourceFactory::ReadCookie(cIGZPersistDBSerialRecord& record) {
    std::uint32_t cookie = 0;
    return record.GetFieldUint32(cookie) ? cookie : 0;
}

bool cSLTokenResourceFactory::SerialRead(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) {
    void* raw = nullptr;
    if (!resource.QueryInterface(kGZIID_cISLTokenResource, &raw)) return false;
    auto* holder = static_cast<cISLTokenResource*>(raw);
    const std::uint32_t cookie = ReadCookie(record);
    bool result = true;
    if (cookie != 0) {
        auto* tokens = new cSLTokenRecord();      // reference count starts at 1
        result = false;
        if (tokens->InitRead(record, cookie)) result = holder->SetTokenRecord(tokens);
        tokens->Release();
    }
    holder->Release();
    return result;
}

bool cSLTokenResourceFactory::SerialWrite(cIGZPersistResource& resource, cIGZPersistDBSerialRecord& record) {
    void* raw = nullptr;
    if (!resource.QueryInterface(kGZIID_cISLTokenResource, &raw)) return false;
    auto* holder = static_cast<cISLTokenResource*>(raw);
    const bool text = holder->IsTextFormat();
    auto* tokens = new cSLTokenRecord();
    if (text) tokens->InitWriteText(record);
    else tokens->InitWriteBinary(record);
    const bool result = holder->WriteTokens(tokens);
    tokens->Release();      // Shutdown writes the cookie and tokens into the record
    holder->Release();
    return result;
}

}  // namespace sc4

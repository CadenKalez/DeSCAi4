// Implementation of cRZIStream and cRZOStream (shared helpers keep the two halves symmetrical).
#include "rz/cRZIStream.h"
#include "rz/cRZOStream.h"

#include <bit>
#include <cstddef>
#include <cstring>
#include <utility>

#include "gz/cIGZSerializable.h"

namespace {

// Converts between the stream's byte order and host order (an involution).
template <class T>
T ToHost(T v, bool dataIsLittleEndian) noexcept {
    const bool hostIsLittle = std::endian::native == std::endian::little;
    if (dataIsLittleEndian == hostIsLittle || sizeof(T) == 1) return v;
    unsigned char bytes[sizeof(T)];
    std::memcpy(bytes, &v, sizeof(T));
    for (std::size_t i = 0; i < sizeof(T) / 2; ++i) std::swap(bytes[i], bytes[sizeof(T) - 1 - i]);
    std::memcpy(&v, bytes, sizeof(T));
    return v;
}

}  // namespace

// ---- cRZIStream --------------------------------------------------------------------------

// cRZIStream::~cRZIStream() @ 0x100027fac — drops the user data reference.
cRZIStream::~cRZIStream() {
    if (userData_ != nullptr) userData_->Release();
}

// cRZIStream::QueryInterface(unsigned int, void**) @ 0x10002844e
bool cRZIStream::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZIStream) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cRZIStream::Release() {
    if (refCount_ > 1) return static_cast<std::uint32_t>(--refCount_);
    delete this;
    return 0;
}

template <class T>
bool cRZIStream::GetScalar(T& value) {
    if (!GetVoid(&value, sizeof(T))) return false;
    value = ToHost(value, dataIsLittleEndian_);
    return true;
}

// cRZIStream::GetSint16/GetUint16/GetSint32/GetUint32/GetSint64/GetUint64/GetFloat32/GetFloat64
// @ 0x10002800c .. 0x1000281c2
bool cRZIStream::GetSint16(std::int16_t& v) { return GetScalar(v); }
bool cRZIStream::GetUint16(std::uint16_t& v) { return GetScalar(v); }
bool cRZIStream::GetSint32(std::int32_t& v) { return GetScalar(v); }
bool cRZIStream::GetUint32(std::uint32_t& v) { return GetScalar(v); }
bool cRZIStream::GetSint64(std::int64_t& v) { return GetScalar(v); }
bool cRZIStream::GetUint64(std::uint64_t& v) { return GetScalar(v); }
bool cRZIStream::GetFloat32(float& v) { return GetScalar(v); }
bool cRZIStream::GetFloat64(double& v) { return GetScalar(v); }

// cRZIStream::DecodeStringLength(unsigned int&) @ 0x10002820a
std::uint32_t cRZIStream::DecodeStringLength() {
    std::uint32_t result = 0;
    unsigned shift = 0;
    for (int i = 0; i < 5; ++i) {  // the original's scratch buffer holds at most 5 bytes
        std::uint8_t b = 0;
        if (!GetUint8(b)) break;
        result |= static_cast<std::uint32_t>(b & 0x7f) << shift;
        if (!(b & 0x80)) break;
        shift += 7;
    }
    return result;
}

// cRZIStream::GetRZCharStr(char*, unsigned int) @ 0x10002829e
bool cRZIStream::GetRZCharStr(char* buffer, std::uint32_t maxLength) {
    const std::uint32_t length = DecodeStringLength();
    if (length == 0 || GetError()) {
        buffer[0] = '\0';
    } else {
        const std::uint32_t n = length < maxLength ? length : maxLength;
        GetVoid(buffer, n);
        buffer[n] = '\0';
        if (!GetError() && length - n != 0) Skip(length - n);  // discard what did not fit
    }
    return !GetError();
}

// cRZIStream::GetGZStr(cIGZString&) @ 0x10002833c — reads in 255-byte chunks.
bool cRZIStream::GetGZStr(cIGZString& value) {
    value.AssignFromCString("");
    std::uint32_t remaining = DecodeStringLength();
    bool failed = GetError();
    while (remaining != 0 && !failed) {
        char chunk[256];
        const std::uint32_t n = remaining < 255 ? remaining : 255;
        GetVoid(chunk, n);
        chunk[n] = '\0';
        value.AppendChars(chunk, n);
        failed = GetError();
        remaining -= n;
    }
    return !GetError();
}

// cRZIStream::GetGZSerializable(cIGZSerializable&) @ 0x100028436
bool cRZIStream::GetGZSerializable(cIGZSerializable& object) { return object.Read(*this); }

// cRZIStream::SetUserData(cIGZVariant*) @ 0x10002849e — takes a reference, drops the old one.
void cRZIStream::SetUserData(cIGZVariant* userData) {
    cIGZVariant* old = userData_;
    if (old == userData) return;
    if (userData != nullptr) userData->AddRef();
    userData_ = userData;
    if (old != nullptr) old->Release();
}

// ---- cRZOStream --------------------------------------------------------------------------

cRZOStream::~cRZOStream() {
    if (userData_ != nullptr) userData_->Release();
}

// cRZOStream::QueryInterface(unsigned int, void**) @ 0x100027ed0
bool cRZOStream::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZOStream) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cRZOStream::Release() {
    if (refCount_ > 1) return static_cast<std::uint32_t>(--refCount_);
    delete this;
    return 0;
}

template <class T>
bool cRZOStream::SetScalar(T value) {
    value = ToHost(value, dataIsLittleEndian_);
    return SetVoid(&value, sizeof(T));
}

// cRZOStream::SetSint16 .. SetFloat64 @ 0x100027c20 .. 0x100027d82
bool cRZOStream::SetSint16(std::int16_t v) { return SetScalar(v); }
bool cRZOStream::SetUint16(std::uint16_t v) { return SetScalar(v); }
bool cRZOStream::SetSint32(std::int32_t v) { return SetScalar(v); }
bool cRZOStream::SetUint32(std::uint32_t v) { return SetScalar(v); }
bool cRZOStream::SetSint64(std::int64_t v) { return SetScalar(v); }
bool cRZOStream::SetUint64(std::uint64_t v) { return SetScalar(v); }
bool cRZOStream::SetFloat32(float v) { return SetScalar(v); }
bool cRZOStream::SetFloat64(double v) { return SetScalar(v); }

// cRZOStream::EncodeStringLength(unsigned int) @ 0x100027dba
void cRZOStream::EncodeStringLength(std::uint32_t length) {
    while (length >= 0x80) {
        SetUint8(static_cast<std::uint8_t>(length | 0x80));
        length >>= 7;
    }
    SetUint8(static_cast<std::uint8_t>(length));
}

// cRZOStream::SetRZCharStr(char const*) @ 0x100027e02
bool cRZOStream::SetRZCharStr(const char* value) {
    const auto length = static_cast<std::uint32_t>(std::strlen(value));
    EncodeStringLength(length);
    return length == 0 ? true : SetVoid(value, length);
}

// cRZOStream::SetGZStr(cIGZString const&) @ 0x100027e56
bool cRZOStream::SetGZStr(const cIGZString& value) {
    const std::uint32_t length = value.Strlen();
    EncodeStringLength(length);
    return length == 0 ? true : SetVoid(value.ToChar(), length);
}

// cRZOStream::SetGZSerializable(cIGZSerializable const&) @ 0x100027eb8
bool cRZOStream::SetGZSerializable(const cIGZSerializable& object) { return object.Write(*this); }

// cRZOStream::SetUserData(cIGZVariant*) @ 0x100027f20
void cRZOStream::SetUserData(cIGZVariant* userData) {
    cIGZVariant* old = userData_;
    if (old == userData) return;
    if (userData != nullptr) userData->AddRef();
    userData_ = userData;
    if (old != nullptr) old->Release();
}

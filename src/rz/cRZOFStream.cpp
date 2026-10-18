#include "rz/cRZOFStream.h"

#include <cstdio>
#include <cstring>

static RZTrueNameFn g_trueName;
void SetRZTrueNameFn(RZTrueNameFn fn) { g_trueName = std::move(fn); }

namespace {
template <class T>
bool Put(cRZTextOStream& stream, const char* format, T value) {
    char text[64];
    std::snprintf(text, sizeof(text), format, value);
    return stream.SetRZCharStr(text);
}
}  // namespace

bool cRZTextOStream::SetSint8(std::int8_t value) { return Put(*this, "%d", static_cast<std::int32_t>(value)); }
bool cRZTextOStream::SetUint8(std::uint8_t value) { return Put(*this, "%u", static_cast<std::uint32_t>(value)); }
bool cRZTextOStream::SetSint16(std::int16_t value) { return Put(*this, "%d", static_cast<std::int32_t>(value)); }
bool cRZTextOStream::SetUint16(std::uint16_t value) { return Put(*this, "%u", static_cast<std::uint32_t>(value)); }
bool cRZTextOStream::SetSint32(std::int32_t value) { return Put(*this, "%d", value); }
bool cRZTextOStream::SetUint32(std::uint32_t value) { return Put(*this, "%u", value); }
bool cRZTextOStream::SetSint64(std::int64_t value) { return Put(*this, "%lld", static_cast<long long>(value)); }
bool cRZTextOStream::SetUint64(std::uint64_t value) { return Put(*this, "%llu", static_cast<unsigned long long>(value)); }
bool cRZTextOStream::SetFloat32(float value) { return Put(*this, "%g", static_cast<double>(value)); }
bool cRZTextOStream::SetFloat64(double value) { return Put(*this, "%g", static_cast<double>(static_cast<float>(value))); }

void cRZOFStream::Open(const std::string& path, bool append) {
    const std::string name = g_trueName ? g_trueName(path) : path;
    std::ios::openmode mode = std::ios::out | (append ? std::ios::app : std::ios::trunc);
    if (binary_) mode |= std::ios::binary;
    file_.open(name, mode);
}

cRZOFStream::cRZOFStream(const cIGZString& path, bool binary, bool littleEndian, bool flushEachWrite, bool append)
    : cRZTextOStream(littleEndian), binary_(binary), flushEach_(flushEachWrite) {
    Open(std::string(path.Data(), path.Strlen()), append);
}

cRZOFStream::cRZOFStream(const char* path, bool binary, bool littleEndian, bool flushEachWrite, bool append)
    : cRZTextOStream(littleEndian), binary_(binary), flushEach_(flushEachWrite) {
    Open(path, append);
}

bool cRZOFStream::SetRZCharStr(const char* text) {
    bool ok;
    if (!binary_) {
        file_.write(text, static_cast<std::streamsize>(std::strlen(text)));
        ok = file_.good();
    } else {
        ok = SetVoid(text, static_cast<std::uint32_t>(std::strlen(text)));
    }
    if (flushEach_) Flush();
    return ok;
}

bool cRZOFStream::SetVoid(const void* data, std::uint32_t length) {
    file_.write(static_cast<const char*>(data), length);
    return file_.good();
}

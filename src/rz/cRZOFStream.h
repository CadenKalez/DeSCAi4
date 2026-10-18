// cRZTextOStream (11 functions, 0x100028644..0x100028c28) and cRZOFStream (21 functions, 0x100028d74..0x1000296f0).
// cRZTextOStream is the text half of an output stream: every number is written as decimal text through
// SetRZCharStr ("%d", "%u", "%lld", "%llu"; floats and doubles both as "%g" of the float value), a cIGZString as its
// characters. cRZOFStream writes to a file and is either binary (raw bytes, byte-swapped when the stream is
// not little-endian, strings as raw characters without a length prefix) or text. Constructor flags: (path, binary,
// dataIsLittleEndian, flushAfterEveryWrite, append). The path goes through GetTrueName first (a seam here).
// Layout: +8 refcount, +0xc little-endian, +0x10 user data, +0x18 the file stream, +0x20 binary, +0x21 flush each.
#pragma once
#include <cstdint>
#include <fstream>
#include <functional>
#include <string>

#include "rz/cRZOStream.h"

using RZTrueNameFn = std::function<std::string(const std::string&)>;
void SetRZTrueNameFn(RZTrueNameFn fn);

class cRZTextOStream : public cRZOStream {
public:
    explicit cRZTextOStream(bool littleEndian) : cRZOStream(littleEndian) {}

    bool SetSint8(std::int8_t value) override;
    bool SetUint8(std::uint8_t value) override;
    bool SetSint16(std::int16_t value) override;
    bool SetUint16(std::uint16_t value) override;
    bool SetSint32(std::int32_t value) override;
    bool SetUint32(std::uint32_t value) override;
    bool SetSint64(std::int64_t value) override;
    bool SetUint64(std::uint64_t value) override;
    bool SetFloat32(float value) override;
    bool SetFloat64(double value) override;
    bool SetGZStr(const cIGZString& value) override { return SetRZCharStr(value.ToChar()); }
};

class cRZOFStream final : public cRZTextOStream {
public:
    cRZOFStream(const cIGZString& path, bool binary, bool littleEndian, bool flushEachWrite, bool append);
    cRZOFStream(const char* path, bool binary, bool littleEndian, bool flushEachWrite, bool append);

    bool SetSint8(std::int8_t value) override { return binary_ ? cRZOStream::SetSint8(value) : cRZTextOStream::SetSint8(value); }
    bool SetUint8(std::uint8_t value) override { return binary_ ? cRZOStream::SetUint8(value) : cRZTextOStream::SetUint8(value); }
    bool SetSint16(std::int16_t value) override { return binary_ ? cRZOStream::SetSint16(value) : cRZTextOStream::SetSint16(value); }
    bool SetUint16(std::uint16_t value) override { return binary_ ? cRZOStream::SetUint16(value) : cRZTextOStream::SetUint16(value); }
    bool SetSint32(std::int32_t value) override { return binary_ ? cRZOStream::SetSint32(value) : cRZTextOStream::SetSint32(value); }
    bool SetUint32(std::uint32_t value) override { return binary_ ? cRZOStream::SetUint32(value) : cRZTextOStream::SetUint32(value); }
    bool SetSint64(std::int64_t value) override { return binary_ ? cRZOStream::SetSint64(value) : cRZTextOStream::SetSint64(value); }
    bool SetUint64(std::uint64_t value) override { return binary_ ? cRZOStream::SetUint64(value) : cRZTextOStream::SetUint64(value); }
    bool SetFloat32(float value) override { return binary_ ? cRZOStream::SetFloat32(value) : cRZTextOStream::SetFloat32(value); }
    bool SetFloat64(double value) override { return binary_ ? cRZOStream::SetFloat64(value) : cRZTextOStream::SetFloat64(value); }
    bool SetRZCharStr(const char* text) override;
    bool SetVoid(const void* data, std::uint32_t length) override;
    bool GetError() const override { return !file_.good(); }
    void Flush() override { file_.flush(); }

private:
    void Open(const std::string& path, bool append);
    std::ofstream file_;       // +0x18
    bool binary_;              // +0x20
    bool flushEach_;           // +0x21
};

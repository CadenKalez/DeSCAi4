#include "rz/PortMemoryStreams.h"
#include "rz/cRZString.h"
#include "rz/cRZVariant.h"
#include "gz/cIGZSerializable.h"
#include "gz/cIGZVariant.h"

#include <cassert>
#include <cstdio>
#include <string>

namespace {
struct Point final : cIGZSerializable {
    std::int32_t x = 0, y = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool Write(cIGZOStream& s) const override { return s.SetSint32(x) && s.SetSint32(y); }
    bool Read(cIGZIStream& s) override { return s.GetSint32(x) && s.GetSint32(y); }
    std::uint32_t GetGZCLSID() const override { return 0x1234; }
};
}  // namespace

int main() {
    // little-endian stream: bytes are exactly the host-LE representation
    {
        PortMemoryOStream o(true);
        o.SetUint8(0xAB); o.SetSint16(-2); o.SetUint32(0x01020304); o.SetUint64(0x1122334455667788ull);
        o.SetFloat32(1.5f); o.SetFloat64(-2.25);
        const unsigned char expect[] = {0xAB, 0xFE, 0xFF, 0x04, 0x03, 0x02, 0x01,
                                        0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11};
        assert(o.bytes().size() > sizeof expect && std::memcmp(o.bytes().data(), expect, sizeof expect) == 0);
        PortMemoryIStream in(o.bytes().data(), o.bytes().size(), true);
        std::uint8_t u8; std::int16_t s16; std::uint32_t u32; std::uint64_t u64; float f; double d;
        assert(in.GetUint8(u8) && u8 == 0xAB && in.GetSint16(s16) && s16 == -2 && in.GetUint32(u32) && u32 == 0x01020304);
        assert(in.GetUint64(u64) && u64 == 0x1122334455667788ull && in.GetFloat32(f) && f == 1.5f && in.GetFloat64(d) && d == -2.25);
        assert(!in.GetError());
        std::uint8_t more; assert(!in.GetUint8(more) && in.GetError());   // exhausted -> error latches
    }
    // big-endian stream
    {
        PortMemoryOStream o(false);
        o.SetUint32(0x01020304); o.SetUint16(0xA1B2); o.SetFloat32(2.0f);
        const unsigned char head[] = {0x01, 0x02, 0x03, 0x04, 0xA1, 0xB2};
        assert(std::memcmp(o.bytes().data(), head, 6) == 0);
        PortMemoryIStream in(o.bytes().data(), o.bytes().size(), false);
        std::uint32_t u32; std::uint16_t u16; float f;
        assert(in.GetUint32(u32) && u32 == 0x01020304 && in.GetUint16(u16) && u16 == 0xA1B2 && in.GetFloat32(f) && f == 2.0f);
    }
    // string length prefix = LEB128
    {
        for (std::uint32_t n : {0u, 1u, 127u, 128u, 300u, 20000u}) {
            PortMemoryOStream o;
            std::string s(n, 'z');
            cRZString gz(s.c_str());
            assert(o.SetGZStr(gz));
            const auto& b = o.bytes();
            const std::size_t prefix = n < 0x80 ? 1 : (n < 0x4000 ? 2 : 3);
            assert(b.size() == prefix + n);
            if (n == 300) assert(b[0] == 0xAC && b[1] == 0x02);          // 300 = 0b10_0101100
            if (n == 128) assert(b[0] == 0x80 && b[1] == 0x01);
            PortMemoryIStream in(b.data(), b.size());
            cRZString back; assert(in.GetGZStr(back) && back.str() == s);
        }
        // GetRZCharStr truncates but consumes the whole string
        PortMemoryOStream o; o.SetRZCharStr("hello world"); o.SetUint8(0x7E);
        PortMemoryIStream in(o.bytes().data(), o.bytes().size());
        char buf[6]; assert(in.GetRZCharStr(buf, 5) && std::string(buf) == "hello");
        std::uint8_t sentinel; assert(in.GetUint8(sentinel) && sentinel == 0x7E);
        // empty
        PortMemoryOStream e; e.SetRZCharStr("");
        PortMemoryIStream ie(e.bytes().data(), e.bytes().size());
        char b2[4] = {1, 1, 1, 1}; assert(ie.GetRZCharStr(b2, 3) && b2[0] == '\0');
    }
    // serializable round trip
    {
        Point p; p.x = -7; p.y = 99;
        PortMemoryOStream o; assert(o.SetGZSerializable(p));
        PortMemoryIStream in(o.bytes().data(), o.bytes().size());
        Point q; assert(in.GetGZSerializable(q) && q.x == -7 && q.y == 99);
    }
    // null streams
    {
        cRZNullIStream ni; std::uint32_t v;
        assert(!ni.GetError() && !ni.GetUint32(v) && ni.GetError());
        cRZNullOStream no; assert(no.SetUint32(5) && no.SetRZCharStr("x") && !no.GetError());
    }
    // refcounting, interface ids and user data
    {
        auto* o = new PortMemoryOStream; void* p = nullptr;
        assert(o->QueryInterface(kGZIID_cIGZOStream, &p) && p == o);
        assert(!o->QueryInterface(kGZIID_cIGZIStream, &p));
        auto* var = new cRZVariant; var->AddRef();                          // test's own reference
        o->SetUserData(var); assert(o->GetUserData() == var);
        o->SetUserData(var);                                               // same pointer: no change
        o->AddRef();
        assert(o->Release() == 1);
        assert(o->Release() == 0);                                          // deleted -> user data released
        assert(var->Release() == 0);                                       // only the test's reference was left
    }
    std::puts("streams ok");
}

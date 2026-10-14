// cGZAny value classes and cGZPropertySet.
#include <cassert>
#include <cstdio>

#include "gz/cGZAny.h"
#include "gz/cGZPropertySet.h"

int main() {
    // value classes: type ids, sizes, typed copies
    AnyUint32 u(7);
    AnyFloat32 f(1.5f);
    AnyBoolean b(true);
    AnySint32 s(-9);
    assert(u.TypeID() == 0xeb61e4f7 && f.TypeID() == 0xabc78708 && b.TypeID() == 0xcba908e1 && s.TypeID() == 0x0c264712);
    assert(u.DataSize() == 0x10 && u.MemberDataSize() == 8 && sizeof(AnyUint32) == 16);
    AnyUint32 dst;
    assert(dst.CopyFrom(u) && dst.get() == 7);
    assert(!dst.CopyFrom(f) && dst.get() == 7);               // different type: refused
    assert(u.CompareTo(AnyUint32(9)) < 0 && AnyUint32(9).CompareTo(u) > 0 && u.CompareTo(AnyUint32(7)) == 0);

    AnyString str("hello");
    AnyString str2;
    assert(str2.CopyFrom(str) && str2.string() == str.string());     // shares the string
    assert(str.CompareTo(AnyString("hello")) == 0 && str.CompareTo(AnyString("zzz")) < 0);
    assert(!str2.CopyFrom(u));

    // property set
    cGZPropertySet* ps = new cGZPropertySet();
    ps->AddRef();
    void* iface = nullptr;
    assert(ps->QueryInterface(kGZIID_cIGZPropertySet, &iface) && iface == static_cast<cIGZPropertySet*>(ps));
    assert(ps->QueryInterface(kGZIID_cIGZUnknown, &iface) && !ps->QueryInterface(0x1234, &iface));
    ps->Release(); ps->Release();
    std::uint32_t k1 = 0xff90aac8, k2 = 0xff3071b9, k3 = 0xff000001;
    assert(ps->SetValue(k1, AnyUint32(1024)) && ps->SetValue(k2, AnyFloat32(0.5f)) && ps->NumValues() == 2);
    AnyUint32 outU;
    AnyFloat32 outF;
    assert(ps->Value(k1, &outU) && outU.get() == 1024);
    assert(ps->Value(k2, &outF) && outF.get() == 0.5f);
    assert(!ps->Value(k2, &outU));                    // stored as float, read as uint: false, no conversion
    assert(!ps->Value(k3, &outU) && !ps->HasValue(k3) && ps->HasValue(k1));
    assert(ps->SetValue(k1, AnyFloat32(2.0f)));       // replacing may change the stored type
    assert(!ps->Value(k1, &outU) && ps->Value(k1, &outF) && outF.get() == 2.0f && ps->NumValues() == 2);
    assert(ps->SetValue(k3, AnyString("abc")));
    AnyString outS;
    assert(ps->Value(k3, &outS) && outS.CompareTo(AnyString("abc")) == 0);

    // iteration in key order
    std::uint32_t key = 0;
    cIGZAny* any = nullptr;
    assert(ps->FirstKeyAndValue(&key, &any) && key == 0xff000001 && any->TypeID() == kAnyTypeString);
    assert(ps->NextKeyAndValue(&key, &any) && key == 0xff3071b9);
    assert(ps->NextKeyAndValue(&key, &any) && key == 0xff90aac8);
    assert(!ps->NextKeyAndValue(&key, &any) && !ps->NextKeyAndValue(&key, &any));

    // copy, clear
    cGZPropertySet copy(*ps);
    assert(copy.NumValues() == 3 && copy.Value(k3, &outS));
    assert(ps->ClearValue(k2) && !ps->ClearValue(k2) && ps->NumValues() == 2);
    ps->ClearAll();
    assert(ps->NumValues() == 0 && copy.NumValues() == 3);
    assert(ps->Release() == 0);
    std::puts("propertyset ok");
}

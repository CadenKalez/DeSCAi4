#include <cmath>
#include <cstdio>
#include <cstring>

#include "rz/cRZString.h"
#include "rz/cRZVariant.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    cRZVariant v;
    CHECK(v.GetType() == cIGZVariant::kTypeNone && v.GetCount() == 0);

    // exact-type access and typed constructors
    v.SetValUint32(0xdeadbeef);
    CHECK(v.GetType() == cIGZVariant::kTypeUint32 && v.GetValUint32() == 0xdeadbeef);
    std::uint32_t u = 0;
    CHECK(v.GetValUint32(u) && u == 0xdeadbeef);
    cRZVariant f(2.75f);
    CHECK(f.GetType() == cIGZVariant::kTypeFloat32 && f.GetValFloat32() == 2.75f);

    // numeric conversions between types
    CHECK(v.GetValSint32() == static_cast<std::int32_t>(0xdeadbeef));      // truncating reinterpretation
    CHECK(v.GetValFloat64() == 3735928559.0);                              // unsigned -> double
    CHECK(f.GetValUint8() == 2 && f.GetValSint32() == 2 && f.GetValBool());
    cRZVariant neg;
    neg.SetValSint16(-5);
    CHECK(neg.GetValSint32() == -5 && neg.GetValUint16() == 0xfffb && neg.GetValFloat32() == -5.0f);
    cRZVariant zero(std::uint8_t{0});
    CHECK(!zero.GetValBool());

    // setting another type discards the old value
    v.SetValBool(true);
    CHECK(v.GetType() == cIGZVariant::kTypeBool && v.GetValUint32() == 1);

    // strings
    cRZString s("hello");
    cRZVariant text(s);
    CHECK(text.GetType() == (cIGZVariant::kTypeRZChar | cIGZVariant::kTypeArrayFlag) && text.GetCount() == 5);
    cRZString out;
    CHECK(text.GetValString(out) && !std::strcmp(out.ToChar(), "hello"));
    cIGZString* made = nullptr;
    CHECK(text.CreateValString(&made) && made && !std::strcmp(made->ToChar(), "hello"));
    made->Release();
    cRZVariant number;
    number.SetValSint32(-42);
    CHECK(!number.GetValString(out) && !std::strcmp(out.ToChar(), "-42"));   // converted text, reported as failure
    CHECK(!number.CreateValString(&made) && made == nullptr);
    cRZVariant numText(cRZString("123"));
    CHECK(numText.GetValSint32() == 123 && numText.GetValFloat32() == 123.0f);
    cRZVariant hexText(cRZString("0x1f"));
    CHECK(hexText.GetValUint32() == 31);
    cRZVariant realText(cRZString("2.5"));
    CHECK(realText.GetValFloat32() == 2.5f && realText.GetValSint32() == 2);
    cRZVariant boolText(cRZString("true"));
    CHECK(boolText.GetValBool());

    // arrays are copied in; same shape is overwritten in place
    const std::uint32_t data[3] = {1, 2, 3};
    cRZVariant arr;
    arr.RefUint32(data, 3);
    CHECK(arr.GetType() == (cIGZVariant::kTypeUint32 | cIGZVariant::kTypeArrayFlag) && arr.GetCount() == 3);
    CHECK(arr.RefUint32() != data && arr.RefUint32()[2] == 3);
    const void* before = arr.RefUint32();
    const std::uint32_t data2[3] = {7, 8, 9};
    arr.RefUint32(data2, 3);
    CHECK(arr.RefUint32() == before && arr.RefUint32()[0] == 7);
    cRZVariant copy(arr);
    CHECK(copy.GetCount() == 3 && copy.RefUint32() != arr.RefUint32() && copy.RefUint32()[1] == 8);
    CHECK(arr.GetValString(out) == false && !std::strcmp(out.ToChar(), "7"));   // first element

    // blobs
    const unsigned char blob[4] = {9, 8, 7, 6};
    cRZVariant raw(blob, 4, 0);
    unsigned char got[8];
    std::uint32_t size = 8;
    CHECK(raw.GetValVoid(got, size) && size == 4 && got[2] == 7);
    size = 2;
    CHECK(raw.GetValVoid(got, size) && size == 2);
    CHECK(!arr.GetValVoid(got, size));

    // object references are counted
    cRZString* obj = new cRZString("object");
    obj->AddRef();
    cRZVariant holder;
    holder.SetValUnknown(obj);
    CHECK(holder.GetType() == cIGZVariant::kTypeUnknown && holder.GetValUnknown() == obj);
    holder.GetValString(out);
    CHECK(!std::strcmp(out.ToChar(), "object"));
    holder.Erase();
    CHECK(holder.GetType() == cIGZVariant::kTypeNone);
    obj->Release();   // last reference

    // copies via the interface, and the 16-bit refcount lifecycle
    cIGZVariant* made2 = nullptr;
    CHECK(arr.CreateCopy(&made2) && made2->GetCount() == 3);
    CHECK(made2->AddRef() == 2 && made2->Release() == 1 && made2->Release() == 0);

    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("rz_variant ok");
    return 0;
}

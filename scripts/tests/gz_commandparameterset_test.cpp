#include <cstdio>

#include "gz/CommandParameterSet.h"
#include "rz/cRZString.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static cRZVariant Num(std::uint32_t v) { cRZVariant x; x.SetValUint32(v); return x; }
static cRZVariant Str(const char* s) { cRZString t(s); cRZVariant x; x.SetValString(t); return x; }

int main() {
    auto* set = new cGZCommandParameterSet();
    set->AddRef();
    CHECK(set->GetParameterCount() == 0 && set->GetFirstParameterID() == kGZNoParameterID);
    set->AppendParameter(Num(10));
    set->AppendParameter(Num(20));
    CHECK(set->GetParameterCount() == 2 && set->GetFirstParameterID() == 0 && set->GetNextParameterID(0) == 1 && set->GetNextParameterID(1) == kGZNoParameterID);
    CHECK(set->GetNextParameterID(7) == kGZNoParameterID);
    CHECK(set->GetParameter(1)->GetValUint32() == 20 && set->GetParameter(5) == nullptr);
    set->SetParameter(100, Num(7));
    set->AppendParameter(Num(8));                           // appended after the highest id
    CHECK(set->GetParameter(101) != nullptr && set->GetParameter(101)->GetValUint32() == 8 && set->GetNextParameterID(1) == 100);
    CHECK(set->operator[](100).GetValUint32() == 7 && set->operator[](55).GetType() == cIGZVariant::kTypeNone);
    set->RemoveParameter(100);
    CHECK(set->GetParameter(100) == nullptr && set->GetParameterCount() == 3);
    set->SetParameterCount(2);                              // drops the highest ids first
    CHECK(set->GetParameter(101) == nullptr && set->GetParameter(1) != nullptr);
    set->SetParameterCount(4);                              // grows with empty variants
    CHECK(set->GetParameterCount() == 4 && set->GetParameter(2) != nullptr && set->GetParameter(2)->GetType() == cIGZVariant::kTypeNone);
    set->SetStatusParameterValue(-3);
    CHECK(set->GetParameter(kGZStatusParameterID)->GetValSint32() == -3);
    set->SetParameter(9, Str("Hello"));
    CHECK(set->CompareParameterString(9, "hello", false) && !set->CompareParameterString(9, "hello", true) && !set->CompareParameterString(9, "Hell", false));
    CHECK(set->CompareParameterString(kGZAnyParameterID, "HELLO", false) && !set->CompareParameterString(kGZAnyParameterID, "nope", false));
    CHECK(!set->CompareParameterString(77, "x", false));
    CHECK(set->ValidateParameter(9, 0xff) && !set->ValidateParameter(9, 1) && !set->ValidateParameter(88, 0xff));
    void* iface = nullptr;
    CHECK(set->QueryInterface(kGZIID_cIGZCommandParameterSet, &iface) && iface == static_cast<cIGZCommandParameterSet*>(set) && !set->QueryInterface(2, &iface));
    set->Release(); set->Release();
    std::printf(failures ? "FAILED\n" : "gz_commandparameterset ok\n");
    return failures ? 1 : 0;
}

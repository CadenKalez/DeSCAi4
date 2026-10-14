#include <cstdio>
#include <string>
#include <unistd.h>

#include "gz/cGZDBSegmentPackedFile.h"
#include "rz/cRZString.h"
#include "sc4/Exemplar.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

static void RoundTrip(bool text) {
    const std::string path = "/tmp/sc4_exemplar_test_" + std::to_string(getpid()) + (text ? "_t.sc4" : "_b.sc4");
    const cGZPersistResourceKey key{kResourceTypeExemplar, 0xdead0001, 0x1};
    cSCResExemplarFactory factory;
    factory.SetWriteMode(text);
    {
        auto* seg = new cGZDBSegmentPackedFile();
        seg->AddRef();
        seg->Init();
        cRZString p(path.c_str());
        seg->SetPath(p);
        CHECK(seg->Open(false, true));
        cIGZPersistDBRecord* raw = nullptr;
        CHECK(seg->CreateNewRecord(key, &raw));
        void* serial = nullptr;
        CHECK(raw->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &serial));
        auto* record = static_cast<cIGZPersistDBSerialRecord*>(serial);
        void* made = nullptr;
        CHECK(factory.CreateInstance(kResourceTypeExemplar, kGZIID_cIGZPersistResource, &made));
        auto* resource = static_cast<cIGZPersistResource*>(made);
        resource->SetKey(key);
        void* raw2 = nullptr;
        CHECK(resource->QueryInterface(kGZIID_cISCResExemplar, &raw2));
        auto* ex = static_cast<cSCResExemplar*>(static_cast<cISCResExemplar*>(raw2));
        auto* a = new cSCProperty(0x10); a->AddRef(); a->GetPropertyValue()->SetValUint32(0x21);
        auto* b = new cSCProperty(0x20); b->AddRef(); { cRZString s("Hello"); a->GetPropertyValue(); b->GetPropertyValue()->SetValString(s); }
        auto* c = new cSCProperty(0x30); c->AddRef(); { float f[2] = {1.5f, 2.0f}; c->GetPropertyValue()->RefFloat32(f, 2); }
        ex->AddProperty(a, false); ex->AddProperty(b, false); ex->AddProperty(c, false);
        a->Release(); b->Release(); c->Release();
        CHECK(factory.SerialWrite(*resource, *record));
        ex->Release();
        resource->Release();
        record->Release();
        CHECK(seg->CloseRecord(raw));
        raw->Release();
        CHECK(seg->Close());
        seg->Release();
    }
    {
        auto* seg = new cGZDBSegmentPackedFile();
        seg->AddRef();
        seg->Init();
        cRZString p(path.c_str());
        seg->SetPath(p);
        CHECK(seg->Open(true, false));
        cIGZPersistDBRecord* raw = nullptr;
        CHECK(seg->OpenRecord(key, &raw, 1));
        void* serial = nullptr;
        CHECK(raw->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &serial));
        auto* record = static_cast<cIGZPersistDBSerialRecord*>(serial);
        void* made = nullptr;
        CHECK(factory.CreateInstance(kResourceTypeExemplar, kGZIID_cIGZPersistResource, &made));
        auto* resource = static_cast<cIGZPersistResource*>(made);
        resource->SetKey(key);
        CHECK(factory.SerialRead(*resource, *record));
        void* raw2 = nullptr;
        CHECK(resource->QueryInterface(kGZIID_cISCResExemplar, &raw2));
        auto* ex = static_cast<cSCResExemplar*>(static_cast<cISCResExemplar*>(raw2));
        CHECK(ex->HasProperty(0x10) && ex->HasProperty(0x20) && ex->HasProperty(0x30));
        std::uint32_t u = 0;
        if (cISCProperty* pr = ex->GetProperty(0x10)) pr->GetPropertyValue()->GetValUint32(u);
        CHECK(u == 0x21);
        if (cISCProperty* pr = ex->GetProperty(0x20)) {
            cRZString s;
            pr->GetPropertyValue()->GetValString(s);
            CHECK(std::string(s.ToChar()) == "Hello");
        }
        if (cISCProperty* pr = ex->GetProperty(0x30)) {
            CHECK(pr->GetPropertyValue()->GetCount() == 2);
            CHECK(pr->GetPropertyValue()->RefFloat32()[0] == 1.5f);
        }
        ex->Release();
        resource->Release();
        record->Release();
        seg->CloseRecord(raw);
        raw->Release();
        seg->Close();
        seg->Release();
    }
    unlink(path.c_str());
}

int main() {
    RoundTrip(false);
    RoundTrip(true);
    std::puts(failures ? "sc4_exemplar: FAILED" : "sc4_exemplar: ok");
    return failures != 0;
}

#include <cstdio>

#include "gz/GZCOM.h"
#include "s3d/Camera.h"
#include "sc4/OccupantFilters2.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    using namespace gzcom;
    struct Entry { void* (*create)(); std::uint32_t clsid; } entries[] = {
        {&GZCOM_CreatecSC4OccupantTypesFilter, kCLSID_cSC4OccupantTypesFilter},
        {&GZCOM_CreatecSC4OccupantTypesFilterHashed, kCLSID_cSC4OccupantTypesFilterHashed},
        {&GZCOM_CreatecSC4NetworkOccupantFilter, kCLSID_cSC4NetworkOccupantFilter},
        {&GZCOM_CreatecSC4ViewLevelOccupantFilter, kCLSID_cSC4ViewLevelOccupantFilter}};
    for (const Entry& e : entries) {
        void* object = e.create();
        CHECK(object != nullptr);
        const ClassFactoryInfo* info = FindClassFactory(e.clsid);
        CHECK(info != nullptr && info->create == e.create);
        auto* unknown = static_cast<cIGZUnknown*>(object);
        unknown->AddRef();    // a fresh object has no reference yet
        unknown->Release();
    }
    void* camera = GZCOM_CreatecS3DCamera();   // not a COM object: plain reference counted class
    CHECK(camera != nullptr && FindClassFactory(kCLSID_cS3DCamera)->create == &GZCOM_CreatecS3DCamera);
    delete static_cast<s3d::cS3DCamera*>(camera);
    std::printf(failures ? "FAILED\n" : "gz_gzcomcreators4 ok\n");
    return failures != 0;
}

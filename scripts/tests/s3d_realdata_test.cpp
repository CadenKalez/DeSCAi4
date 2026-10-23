// Parses every S3D resource of a game package and reports failures. usage: s3d_realdata_test [package.dat] [max]
#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZPersistResourceKeyList.h"
#include "rz/cRZString.h"
#include "s3d/S3DModel.h"

int main(int argc, char** argv) {
    // In the suite it runs on $SC4_S3D_PACKAGE when set and skips otherwise (the game data is not in the repo).
    const char* package = argc >= 2 ? argv[1] : std::getenv("SC4_S3D_PACKAGE");
    if (!package || !*package) { std::puts("s3d_realdata SKIP (no package: pass a .dat or set SC4_S3D_PACKAGE)"); return 0; }
    auto* seg = new cGZDBSegmentPackedFile();
    seg->AddRef();
    seg->Init();
    cRZString path(package);
    seg->SetPath(path);
    if (!seg->Open(true, false)) { std::puts("cannot open"); return 1; }
    auto* keys = new cGZPersistResourceKeyList();
    keys->AddRef();
    cGZPersistResourceKeyFilterByType filter(s3d::kResourceTypeModel);
    filter.AddRef();
    seg->GetResourceKeyList(keys, &filter);
    const unsigned limit = argc > 2 ? unsigned(std::atoi(argv[2])) : 0xffffffffu;
    unsigned ok = 0, bad = 0, total = 0, shown = 0;
    std::map<std::uint32_t, unsigned> formats;
    for (std::uint32_t i = 0; i < keys->Size() && total < limit; ++i, ++total) {
        const auto* k = keys->GetKey(i);
        cIGZPersistDBRecord* raw = nullptr;
        if (!seg->OpenRecord(*k, &raw, 1)) { ++bad; continue; }
        void* serial = nullptr;
        raw->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &serial);
        auto* rec = static_cast<cIGZPersistDBSerialRecord*>(serial);
        std::vector<std::uint8_t> bytes(rec->GetSize());
        rec->GetFieldVoid(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
        s3d::Model m;
        if (m.Parse(bytes.data(), bytes.size())) {
            ++ok;
            for (auto& g : m.vertexGroups) formats[(g.format << 8) | g.stride]++;
            if (shown < 3) {
                float lo[3], hi[3];
                m.GetBounds(lo, hi);
                std::printf("  %08x/%08x/%08x v%u.%u vg=%zu ig=%zu pg=%zu mat=%zu meshes=%zu frames=%u props=%zu bounds x[%.1f,%.1f] y[%.1f,%.1f] z[%.1f,%.1f]\n",
                            k->type, k->group, k->instance, m.major, m.minor, m.vertexGroups.size(), m.indexGroups.size(),
                            m.primitiveGroups.size(), m.materials.size(), m.meshes.size(), m.frameCount, m.properties.size(),
                            lo[0], hi[0], lo[1], hi[1], lo[2], hi[2]);
                ++shown;
            }
        } else {
            ++bad;
            if (bad < 4) std::printf("  FAIL %08x/%08x/%08x (%zu bytes)\n", k->type, k->group, k->instance, bytes.size());
        }
        serial ? static_cast<cIGZPersistDBSerialRecord*>(serial)->Release() : 0;
        seg->CloseRecord(raw);
        raw->Release();
    }
    for (auto& kv : formats) std::printf("  vertex format %08x stride %u: %u groups\n", kv.first >> 8, kv.first & 0xff, kv.second);
    std::printf("S3D models: %u parsed, %u failed of %u\n", ok, bad, total);
    return bad != 0;
}

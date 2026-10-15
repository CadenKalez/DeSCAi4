#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>
#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZPersistResourceKeyList.h"
#include "rz/cRZString.h"
int main(int argc, char** argv) {
    auto* seg = new cGZDBSegmentPackedFile(); seg->AddRef(); seg->Init();
    cRZString path(argv[1]); seg->SetPath(path);
    if (!seg->Open(true, false)) return 1;
    auto* keys = new cGZPersistResourceKeyList(); keys->AddRef();
    cGZPersistResourceKeyFilterByType filter(0x7ab50e44); filter.AddRef();
    seg->GetResourceKeyList(keys, &filter);
    std::map<unsigned, unsigned> codes; unsigned n = 0, shown = 0;
    for (std::uint32_t i = 0; i < keys->Size() && n < 3000; ++i, ++n) {
        const auto* k = keys->GetKey(i);
        cIGZPersistDBRecord* raw = nullptr;
        if (!seg->OpenRecord(*k, &raw, 1)) continue;
        void* serial = nullptr; raw->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &serial);
        auto* rec = static_cast<cIGZPersistDBSerialRecord*>(serial);
        std::vector<std::uint8_t> b(rec->GetSize()); rec->GetFieldVoid(b.data(), b.size());
        if (b.size() > 16 && b[0]=='S' && b[1]=='H' && b[2]=='P' && b[3]=='I') {
            unsigned entries = *(unsigned*)&b[8];
            for (unsigned e = 0; e < entries && 16 + e*8 + 8 <= b.size(); ++e) {
                unsigned off = *(unsigned*)&b[16 + e*8 + 4];
                if (off + 16 > b.size()) continue;
                unsigned code = b[off]; codes[code]++;
                if (shown < 12 && e == 0) {
                    std::printf("%08x/%08x/%08x entries=%u code=%02x w=%d h=%d misc=%08x size=%zu\n", k->type, k->group, k->instance, entries, code,
                      *(short*)&b[off+4], *(short*)&b[off+6], *(unsigned*)&b[off+12], b.size()); ++shown;
                }
            }
        }
        static_cast<cIGZPersistDBSerialRecord*>(serial)->Release(); seg->CloseRecord(raw); raw->Release();
    }
    for (auto& kv : codes) std::printf("code %02x: %u\n", kv.first, kv.second);
}

// fsh_dump <package.dat> <instance-hex> <out.png>: decode the first FSH frame of a texture to a PNG
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <zlib.h>
#include "gz/cGZDBSegmentPackedFile.h"
#include "gz/cGZPersistResourceKeyList.h"
#include "rz/cRZString.h"
#include "s3d/FSH.h"

static void Put32(std::vector<unsigned char>& v, unsigned x) { for (int i = 3; i >= 0; --i) v.push_back(x >> (8 * i)); }
static void Chunk(std::vector<unsigned char>& out, const char* t, const std::vector<unsigned char>& d) {
    Put32(out, d.size());
    std::vector<unsigned char> b(t, t + 4); b.insert(b.end(), d.begin(), d.end());
    out.insert(out.end(), b.begin(), b.end());
    Put32(out, crc32(0, b.data(), b.size()));
}
int main(int argc, char** argv) {
    auto* seg = new cGZDBSegmentPackedFile(); seg->AddRef(); seg->Init();
    cRZString path(argv[1]); seg->SetPath(path);
    if (!seg->Open(true, false)) return 1;
    const unsigned want = std::strtoul(argv[2], nullptr, 16);
    auto* keys = new cGZPersistResourceKeyList(); keys->AddRef();
    cGZPersistResourceKeyFilterByType filter(0x7ab50e44); filter.AddRef();
    seg->GetResourceKeyList(keys, &filter);
    for (std::uint32_t i = 0; i < keys->Size(); ++i) {
        const auto* k = keys->GetKey(i);
        if (k->instance != want) continue;
        cIGZPersistDBRecord* raw = nullptr;
        seg->OpenRecord(*k, &raw, 1);
        void* serial = nullptr; raw->QueryInterface(kGZIID_cIGZPersistDBSerialRecord, &serial);
        auto* rec = static_cast<cIGZPersistDBSerialRecord*>(serial);
        std::vector<std::uint8_t> b(rec->GetSize()); rec->GetFieldVoid(b.data(), b.size());
        s3d::FshFile f;
        std::vector<std::uint8_t> rgba; int w, h;
        if (!f.Open(b.data(), b.size()) || !f.DecodeRgba8(0, rgba, w, h)) { std::puts("decode failed"); return 1; }
        std::vector<unsigned char> raws;
        for (int y = 0; y < h; ++y) { raws.push_back(0); raws.insert(raws.end(), &rgba[y * w * 4], &rgba[y * w * 4] + w * 4); }
        uLongf n = compressBound(raws.size());
        std::vector<unsigned char> z(n);
        compress(z.data(), &n, raws.data(), raws.size()); z.resize(n);
        std::vector<unsigned char> png = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
        std::vector<unsigned char> ihdr; Put32(ihdr, w); Put32(ihdr, h); ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
        Chunk(png, "IHDR", ihdr); Chunk(png, "IDAT", z); Chunk(png, "IEND", {});
        FILE* o = std::fopen(argv[3], "wb"); std::fwrite(png.data(), 1, png.size(), o); std::fclose(o);
        std::printf("wrote %s %dx%d code %02x group %08x\n", argv[3], w, h, b[16 + 4 + 0] , k->group);
        return 0;
    }
    std::puts("not found");
    return 1;
}

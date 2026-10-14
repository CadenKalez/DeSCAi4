#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>

#include "dbpf/DBPFReader.h"
#include "rz/cRZFastCompression3.h"
#include "rz/cRZString.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static bool RoundTrip(cRZFastCompression3& c, const std::vector<std::uint8_t>& in) {
    // callers pad two zero bytes after the data; mimic that
    std::vector<std::uint8_t> src(in.size() + 2, 0);
    std::copy(in.begin(), in.end(), src.begin());
    std::vector<std::uint8_t> comp(c.GetMaxLengthRequiredForCompressedData(static_cast<std::uint32_t>(in.size())));
    std::uint32_t clen = 0;
    if (!c.CompressData(src.data(), static_cast<std::uint32_t>(in.size()), comp.data(), clen)) return false;
    if (!c.ValidateCompressedData(comp.data(), clen)) return false;
    if (c.GetLengthOfCompressedData(comp.data()) != clen) return false;
    if (c.GetLengthOfDecompressedData(comp.data()) != in.size()) return false;
    std::vector<std::uint8_t> out(in.size() + 8);
    std::uint32_t olen = static_cast<std::uint32_t>(out.size());
    if (!c.DecompressData(comp.data(), clen, out.data(), olen)) return false;
    return olen == in.size() && std::memcmp(out.data(), in.data(), in.size()) == 0;
}

int main(int argc, char** argv) {
    cRZFastCompression3 c;
    std::mt19937 rng(1234);
    for (std::size_t n : {0u, 1u, 2u, 3u, 5u, 17u, 100u, 111u, 112u, 113u, 500u, 5000u, 70000u, 300000u}) {
        std::vector<std::uint8_t> v(n);
        for (auto& b : v) b = static_cast<std::uint8_t>(rng() % 7);  // compressible
        CHECK(RoundTrip(c, v));
        for (auto& b : v) b = static_cast<std::uint8_t>(rng());      // incompressible
        CHECK(RoundTrip(c, v));
        for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(i % 251);  // long matches
        CHECK(RoundTrip(c, v));
    }
    c.EnableQuickCompression(true);
    std::vector<std::uint8_t> big(400000);
    for (std::size_t i = 0; i < big.size(); ++i) big[i] = static_cast<std::uint8_t>((i * 31) ^ (i >> 7));
    CHECK(RoundTrip(c, big));
    c.EnableQuickCompression(false);

    // malformed input is rejected, not trusted
    std::uint8_t junk[16] = {16, 0, 0, 0, 0x10, 0xfb, 0, 0, 8, 0xe0, 1, 2, 3, 4, 5, 6};
    std::uint32_t cap = 8;
    CHECK(!c.DecompressData(junk, 16, junk, cap));

    cRZString v;
    c.GetCompressionVersionString(v);
    CHECK(!std::strcmp(v.ToChar(), "0.90"));

    // real data: every compressed record of a game save decodes, and re-encoding is byte-identical
    if (argc > 1) {
        dbpf::Package pkg;
        CHECK(pkg.Open(argv[1]));
        std::vector<std::uint8_t> file;
        {
            FILE* f = std::fopen(argv[1], "rb");
            std::fseek(f, 0, SEEK_END);
            file.resize(static_cast<std::size_t>(std::ftell(f)));
            std::fseek(f, 0, SEEK_SET);
            CHECK(std::fread(file.data(), 1, file.size(), f) == file.size());
            std::fclose(f);
        }
        int checked = 0, identical = 0;
        for (const auto& e : pkg.Entries()) {
            if (!e.uncompressedSize) continue;
            std::vector<std::uint8_t> plain;
            CHECK(pkg.Read(e, plain));
            CHECK(plain.size() == e.uncompressedSize);
            std::vector<std::uint8_t> src(plain.size() + 2, 0);
            std::copy(plain.begin(), plain.end(), src.begin());
            std::vector<std::uint8_t> comp(c.GetMaxLengthRequiredForCompressedData(static_cast<std::uint32_t>(plain.size())));
            std::uint32_t clen = 0;
            c.EnableQuickCompression(plain.size() > 250000);  // as cGZDBSegmentPackedFile::CompressData
            CHECK(c.CompressData(src.data(), static_cast<std::uint32_t>(plain.size()), comp.data(), clen));
            std::vector<std::uint8_t> back(plain.size() + 4);
            std::uint32_t blen = static_cast<std::uint32_t>(back.size());
            CHECK(c.DecompressData(comp.data(), clen, back.data(), blen));
            CHECK(blen == plain.size() && !std::memcmp(back.data(), plain.data(), plain.size()));
            // is our encoder's output the very bytes stored in the save?
            if (clen == e.size && !std::memcmp(comp.data(), file.data() + e.offset, clen)) ++identical;
            if (++checked >= 60) break;
        }
        std::printf("real compressed records: %d, byte-identical re-encodings: %d\n", checked, identical);
    }
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("rz_fastcompression: all tests passed");
    return 0;
}

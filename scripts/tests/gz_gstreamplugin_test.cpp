#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gz/GStreamPlugin.h"
#include "gz/MiscFunctions.h"
#include "s3d/IndexGeneration.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
struct MemFile final : cIGZFile {
    std::string data; std::uint32_t pos = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool Open(int, int, int) override { return true; }
    bool IsOpen() const override { return true; }
    bool Close() override { return true; }
    int Position() override { return int(pos); }
    std::uint32_t Length() override { return std::uint32_t(data.size()); }
    bool SetLength(std::uint32_t) override { return true; }
    void SeekToBegin() override { pos = 0; }
    void SeekToEnd() override { pos = std::uint32_t(data.size()); }
    void SeekToRelativePosition(int o) override { pos += o; }
    void SeekToPosition(std::uint32_t p) override { pos = p; }
    std::uint32_t Seek(int offset, int origin) override {
        long base = origin == 0 ? 0 : origin == 1 ? long(pos) : long(data.size());
        long target = base + offset;
        if (target < 0 || target > long(data.size())) return 0xffffffffu;
        pos = std::uint32_t(target); return pos;
    }
    bool Read(void*, std::uint32_t) override { return true; }
    bool ReadWithCount(void* d, std::uint32_t& n) override {
        if (pos + n > data.size()) n = std::uint32_t(data.size()) - pos;
        std::memcpy(d, data.data() + pos, n); pos += n; return true; }
    bool Write(const void*, std::uint32_t) override { return true; }
    bool WriteWithCount(const void* s, std::uint32_t& n) override {
        data.replace(pos, std::min<std::size_t>(n, data.size() - pos), static_cast<const char*>(s), n); pos += n; return true; }
    bool Flush() override { return true; }
    bool Remove() override { return true; }
    bool Rename(const cIGZString&) override { return true; }
    bool Exists() override { return true; }
    bool Copy(const cIGZString&, bool) override { return true; }
    bool Copy(const cIGZFile&, bool) override { return true; }
    void GetPath(cIGZString&) const override {}
    const cIGZString* GetPath() const override { return nullptr; }
    const char* GetPathCStr() const override { return ""; }
    void SetPath(const cIGZString&) override {}
    void SetPath(const char*) override {}
    bool GetOpenModes(int&, int&, int&) const override { return true; }
    void GetBuffering(std::uint32_t&, std::uint32_t&) const override {}
    bool SetBuffering(std::uint32_t, std::uint32_t) override { return true; }
};
}  // namespace

int main() {
    char* block = static_cast<char*>(_galloc(16));
    CHECK(block && _gfree(block) && _gfree(nullptr));
    MemFile f; f.data = "hello world";
    char buf[16] = {};
    CHECK(_glen(&f) == 11 && _gtell(&f) == 0);
    CHECK(_gread(&f, buf, 5) == 5 && std::string(buf, 5) == "hello" && _gtell(&f) == 5);
    CHECK(_gread(&f, buf, 100) == 6 && _gread(&f, buf, -1) == 0);
    CHECK(_gseek(&f, 6) && _gtell(&f) == 6 && !_gseek(&f, 99));
    CHECK(_gwrite(&f, "WORLD", 5) == 5 && f.data == "hello WORLD" && _gwrite(&f, "x", -3) == 0);

    std::uint8_t* about = NULL_about();
    CHECK(about[0] == 'L' && about[1] == 'L' && about[2] == 'U' && about[3] == 'N' && about[4] == 0 && about[5] == 1);
    CHECK(std::memcmp(about + 0x7c, "Colin Andrews", 14) == 0 && std::memcmp(about + 0xa4, "Null", 5) == 0);
    CHECK(std::memcmp(about + 0xbc, "Not Gimex", 10) == 0 && std::memcmp(about + 0xac, "Not", 4) == 0 && about[0x9c] == '0' && about[0xff] == 0);
    CHECK(_gfree(about));
    CHECK(NULL_is(nullptr) == 0 && NULL_open(nullptr, nullptr, "", false) == 0 && NULL_info(nullptr, 0) == 0 &&
          NULL_read(nullptr, nullptr, nullptr, 0) == 0 && NULL_close(nullptr) == 0 && NULL_wopen(nullptr, nullptr, "", 0) == 0 &&
          NULL_write(nullptr, nullptr, nullptr, 0) == 0 && NULL_wclose(nullptr) == 0);

    CHECK(stricmp("Hello", "hELLO") == 0 && stricmp("abc", "abd") == -1 && stricmp("abd", "abc") == 1 && stricmp("", "a") == -1 && stricmp("a", "") == 1);

    std::uint16_t strip[] = {10, 11, 12, 13, 14, 15};
    std::uint16_t out[12] = {};
    std::uint32_t lo = 0, hi = 0;
    GenerateIndexedTriListForIndexedTriangleStrip<std::uint16_t>(6, strip, out, &lo, &hi);
    const std::uint16_t want[12] = {10, 11, 12, 12, 11, 13, 12, 13, 14, 14, 13, 15};
    // The original wrote one triangle per iteration with the same alternating pattern; check the first four triangles.
    for (int i = 0; i < 12; ++i) CHECK(out[i] == want[i]);
    CHECK(lo == 10 && hi == 15);
    std::uint8_t fan[] = {5, 6, 7, 8, 9};
    std::uint16_t fout[9] = {};
    GenerateIndexedTriListForIndexedTriangleFan<std::uint8_t>(5, fan, fout, &lo, &hi);
    const std::uint16_t fwant[9] = {5, 6, 7, 5, 7, 8, 5, 8, 9};
    for (int i = 0; i < 9; ++i) CHECK(fout[i] == fwant[i]);
    CHECK(lo == 5 && hi == 9);
    std::uint32_t wide[] = {900, 4, 70000 - 65536 + 100, 3};
    std::uint16_t wout[6] = {};
    GenerateIndexedTriListForIndexedTriangleStrip<std::uint32_t>(4, wide, wout, &lo, &hi);
    CHECK(lo == 3 && hi == 4564 && wout[0] == 900 && wout[2] == wide[2]);
    GenerateIndexedTriListForIndexedTriangleStrip<std::uint16_t>(2, strip, out, &lo, &hi);
    CHECK(lo == 10 && hi == 11);
    std::printf(failures ? "FAILED\n" : "gz_gstreamplugin ok\n");
    return failures != 0;
}

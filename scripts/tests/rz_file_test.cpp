#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/stat.h>

#include "rz/cRZFile.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    char tmpl[] = "/tmp/rzfile_testXXXXXX";
    char* dir = mkdtemp(tmpl);
    std::string base = std::string(dir) + "/a.bin";

    // Splitpath / helpers
    {
        char d[1], di[256], n[256], e[256];
        Splitpath("/x/y/file.tar.gz", d, di, n, e);
        CHECK(!strcmp(di, "/x/y/") && !strcmp(n, "file.tar") && !strcmp(e, ".gz"));
        Splitpath("noext", d, di, n, e);
        CHECK(!*di && !strcmp(n, "noext") && !*e);
        Splitpath("/a.d/b", d, di, n, e);  // dot inside a directory is not an extension
        CHECK(!strcmp(n, "b") && !*e);
        cRZString p("a\\b\\c.txt"), q("a\\b\\c.txt");
        SC4AdjustPathSeparatorsForPlatform(p, q);
        CHECK(!strcmp(q.ToChar(), "a/b/c.txt"));
        cRZFile f("C:\\x\\y.dat");
        CHECK(!strcmp(f.GetPathCStr(), "C:/x/y.dat"));
        cRZString o;
        CHECK(cRZFile::GetPathName(cRZString("/q/r/s.sc4"), o, true) && !strcmp(o.ToChar(), "s.sc4"));
        CHECK(cRZFile::GetPathName(cRZString("/q/r/s.sc4"), o, false) && !strcmp(o.ToChar(), "s"));
        CHECK(cRZFile::GetPathExtension(cRZString("/q/r/s.sc4"), o) && !strcmp(o.ToChar(), ".sc4"));
        CHECK(!cRZFile::GetPathDirectory(cRZString("/q/r/s.sc4"), o) && o.Strlen() == 0);
    }

    // Buffered write / read-back across buffer boundaries
    {
        cRZFile f(base.c_str());
        CHECK(!f.Exists());
        CHECK(f.Open(3, 1, 1));
        CHECK(f.SetBuffering(16, 16));
        std::string data;
        for (int i = 0; i < 1000; ++i) data.push_back(char('a' + i % 26));
        CHECK(f.Write(data.data(), 1000));
        CHECK(f.Position() == 1000);
        CHECK(f.Length() == 1000);
        CHECK(f.Flush());
        f.SeekToBegin();
        char buf[1000];
        CHECK(f.Read(buf, 1000) && memcmp(buf, data.data(), 1000) == 0);
        f.SeekToPosition(500);
        CHECK(f.Read(buf, 10) && memcmp(buf, data.data() + 500, 10) == 0);
        f.SeekToRelativePosition(5);
        CHECK(f.Position() == 515);
        CHECK(f.Read(buf, 4) && memcmp(buf, data.data() + 515, 4) == 0);
        // short read at EOF: ReadWithCount reports bytes, Read fails
        f.SeekToPosition(995);
        std::uint32_t n = 20;
        CHECK(f.ReadWithCount(buf, n) && n == 5);
        f.SeekToPosition(995);
        CHECK(!f.Read(buf, 20));
        CHECK(f.Seek(0, 7) == static_cast<std::uint32_t>(f.Position()));  // unknown origin: position
        // overwrite in the middle after reading
        f.SeekToPosition(100);
        CHECK(f.Read(buf, 3));
        CHECK(f.Write("ZZZ", 3));
        f.Flush();
        char three[3];
        // a write after a read goes to the logical position (103)
        f.SeekToPosition(103);
        CHECK(f.Read(three, 3) && memcmp(three, "ZZZ", 3) == 0);
        CHECK(f.SetLength(10) && f.Length() == 10);
        int a, c, s; CHECK(f.GetOpenModes(a, c, s) && a == 3 && c == 1 && s == 1);
        CHECK(f.Close() && !f.IsOpen());
        CHECK(!f.GetOpenModes(a, c, s));
        CHECK(cRZFile::GetLength(cRZString(base.c_str())) == 10u);
    }

    // Unbuffered mode
    {
        cRZFile f(base.c_str());
        CHECK(f.Open(3, 3, 1));
        CHECK(f.SetBuffering(0, 0));
        std::uint32_t r, w; f.GetBuffering(r, w);
        CHECK(r == 0 && w == 0);
        f.SeekToEnd();
        CHECK(f.Write("12345", 5));
        f.SeekToPosition(10);
        char b[5];
        CHECK(f.Read(b, 5) && !memcmp(b, "12345", 5));
        f.SetBuffering(2, 3);  // low bit is dropped, 1..3 round up to 4
        f.GetBuffering(r, w);
        CHECK(r == 4 && w == 4);
        f.SetBuffering(1, 0);  // 1 & ~1 == 0: buffering off
        f.GetBuffering(r, w);
        CHECK(r == 0 && w == 0);
        f.SetBuffering(0xffffffffu, 1u << 30);
        f.GetBuffering(r, w);
        CHECK(r == 0x200 && w == 0x1f4000);
    }

    // Open modes and error codes
    {
        cRZFile f((std::string(dir) + "/missing").c_str());
        CHECK(!f.Open(1, 2, 1) && f.LastError() == 2);
    }

    // Static helpers
    {
        cRZString p(base.c_str()), cp((std::string(dir) + "/b.bin").c_str());
        CHECK(cRZFile::FileExists(p));
        CHECK(cRZFile::Copy(p, cp, true));
        CHECK(cRZFile::GetLength(cp) == cRZFile::GetLength(p));
        CHECK(!cRZFile::Copy(p, cp, false));                       // source exists -> fails (sic)
        CHECK(cRZFile::Copy(cRZString("/x/.DS_Store"), cp, true));  // skipped, reports success
        CHECK(cRZFile::GetAttribute(p, cRZFile::kAttrReadable | cRZFile::kAttrWritable | cRZFile::kAttrFile));
        CHECK(!cRZFile::GetAttribute(p, cRZFile::kAttrDirectory));
        CHECK(cRZFile::GetAttribute(cRZString(dir), cRZFile::kAttrDirectory));
        CHECK(cRZFile::GetTime(p, cRZFile::kTimeModified) > 1000000000u);
        CHECK(cRZFile::IsFileWritable(p));
        CHECK(cRZFile::Rename(cp, cRZString((std::string(dir) + "/c.bin").c_str())));
        cRZFile moved((std::string(dir) + "/c.bin").c_str());
        CHECK(moved.Exists());
        CHECK(moved.Rename(cRZString((std::string(dir) + "/d.bin").c_str())));
        CHECK(!strcmp(moved.GetPathCStr(), (std::string(dir) + "/d.bin").c_str()));
        CHECK(moved.Remove() && !moved.Exists());
        cRZString tmpPrefix(""), ext(".dat");
        CHECK(cRZFile::MakeTempPathName(tmpPrefix, &ext));
        CHECK(tmpPrefix.FindChars("RZ_", 0, true) != size_t(-1) && cRZFile::FileExists(tmpPrefix));
        CHECK(cRZFile::Remove(tmpPrefix));
        cRZString dd((std::string(dir) + "/").c_str());
        cRZString pre("t_");
        CHECK(cRZFile::MakeTempFileName(dd, pre, nullptr) && cRZFile::FileExists(pre));
        CHECK(pre.FindChars(".tmp", 0, true) != size_t(-1));
        cRZFile::Remove(pre);
        CHECK(cRZFile::Remove(p));
        // refcount
        auto* h = new cRZFile();
        h->AddRef();
        void* q = nullptr;
        CHECK(h->QueryInterface(kGZIID_cIGZFile, &q) && q);
        CHECK(!h->QueryInterface(0x12345, &q));
        h->Release();
        CHECK(h->Release() == 0);
    }
    rmdir(dir);
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::puts("rz_file: all tests passed");
    return 0;
}

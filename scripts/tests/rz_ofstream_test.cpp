#include <cstdio>
#include <fstream>
#include <sstream>

#include "rz/cRZOFStream.h"
#include "rz/cRZString.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static std::string Slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

int main() {
    const std::string dir = std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp";
    const std::string textPath = dir + "/rz_ofstream_text.txt", binPath = dir + "/rz_ofstream_bin.dat";
    {
        cRZOFStream text(textPath.c_str(), false, true, false, false);
        CHECK(!text.GetError());
        text.SetSint8(-5); text.SetUint8(200); text.SetSint16(-300); text.SetUint16(65000);
        text.SetSint32(-70000); text.SetUint32(4000000000u); text.SetSint64(-5000000000ll);
        text.SetUint64(18000000000000000000ull); text.SetFloat32(1.5f); text.SetFloat64(2.25);
        text.SetGZStr(cRZString("|end"));
        text.SetRZCharStr("!");
        text.Flush();
    }
    CHECK(Slurp(textPath) == "-5200-30065000-700004000000000-500000000018000000000000000000" "1.52.25|end!");
    {
        cRZOFStream bin(cRZString(binPath.c_str()), true, false, true, false);   // big-endian, flush every write
        bin.SetUint8(1); bin.SetUint16(0x0203); bin.SetUint32(0x04050607); bin.SetSint8(-1);
        bin.SetRZCharStr("ab");
        bin.SetVoid("cd", 2);
    }
    const std::string raw = Slurp(binPath);
    CHECK(raw == std::string("\x01\x02\x03\x04\x05\x06\x07\xff" "abcd", 12));
    {
        cRZOFStream little(binPath.c_str(), true, true, false, false);
        little.SetUint16(0x0203);
    }
    CHECK(Slurp(binPath) == std::string("\x03\x02", 2));
    {
        cRZOFStream append(binPath.c_str(), true, true, false, true);   // append keeps the old content
        append.SetUint8(9);
    }
    CHECK(Slurp(binPath) == std::string("\x03\x02\x09", 3));
    cRZOFStream bad("/nonexistent-dir/x.bin", true, true, false, false);
    CHECK(bad.GetError() && !bad.SetUint8(1));
    SetRZTrueNameFn([](const std::string& p) { return p + ".named"; });
    {
        cRZOFStream named(binPath.c_str(), true, true, false, false);
        named.SetUint8(7);
    }
    CHECK(Slurp(binPath + ".named") == "\x07");
    SetRZTrueNameFn(nullptr);
    std::remove(textPath.c_str());
    std::remove(binPath.c_str());
    std::remove((binPath + ".named").c_str());
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}

// cRZVersion over a scripted platform.
#include <cstdio>
#include <cstring>

#include "rz/cRZVersion.h"

static int g_failed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_failed; } } while (0)

struct FakePlatform : IRZVersionPlatform {
    bool haveBundle = true, haveMemory = true, haveUser = true;
    bool BundleName(std::string& out) override { if (!haveBundle) return false; out = "SimCity 4"; return true; }
    bool BundleShortVersion(std::string& out) override { if (!haveBundle) return false; out = "1.2.1"; return true; }
    bool AppPath(std::string& out) override { out = "/Applications/SimCity 4.app"; return true; }
    std::uint64_t CPUFrequencyMax() override { return 2'800'000'000ull; }
    bool MemoryMB(std::uint64_t& physical, std::uint32_t& available) override { if (!haveMemory) return false; physical = 8191; available = 1001; return true; }
    bool UserName(std::string& out) override { if (!haveUser) return false; out = "mayor"; return true; }
    bool ComputerName(std::string& out) override { if (!haveUser) return false; out = "sim-mac"; return true; }
};

int main() {
    FakePlatform fake;
    SetRZVersionPlatform(&fake);
    cRZVersion version;
    cRZString s("junk");
    CHECK(version.GetModuleName(s) && std::strcmp(s.ToChar(), "SimCity 4") == 0);
    CHECK(version.GetModuleVersion(s) && std::strcmp(s.ToChar(), "1.2.1") == 0);
    CHECK(version.GetModulePath(s) && std::strcmp(s.ToChar(), "/Applications/SimCity 4.app") == 0);
    CHECK(version.GetSystemVersion(s) && std::strcmp(s.ToChar(), "Windows NT 5.1") == 0);
    CHECK(version.GetModuleVersion(std::uint32_t(3)) == 0xffffffffu);
    CHECK(version.GetCPUSpeed() == 2800 && version.GetCPUFlags() == 0 && !version.GetCPUName(s));
    std::uint32_t total = 0, available = 0;
    CHECK(version.GetSystemMemory(total, available) && total == 8192 && available == 1004);   // rounded up to a multiple of 4
    fake.haveMemory = false;
    CHECK(!version.GetSystemMemory(total, available) && total == 0x8000000 && available == 0x1000000);
    CHECK(version.GetUserNameA(s, nullptr) && std::strcmp(s.ToChar(), "mayor") == 0);
    CHECK(version.GetHostName(s, nullptr) && std::strcmp(s.ToChar(), "sim-mac") == 0);
    fake.haveBundle = false; fake.haveUser = false;
    CHECK(!version.GetModuleName(s) && std::strcmp(s.ToChar(), "") == 0);
    CHECK(!version.GetModuleVersion(s) && s.Strlen() == 0);
    CHECK(!version.GetUserNameA(s, nullptr) && std::strcmp(s.ToChar(), "?") == 0);
    cRZString fallback("nobody");
    CHECK(!version.GetUserNameA(s, &fallback) && std::strcmp(s.ToChar(), "nobody") == 0);
    CHECK(!version.GetHostName(s, nullptr) && std::strcmp(s.ToChar(), "localhost") == 0);
    SetRZVersionPlatform(nullptr);
    CHECK(GetRZVersionPlatform() != &fake);
    std::printf(g_failed ? "FAILED (%d)\n" : "OK\n", g_failed);
    return g_failed ? 1 : 0;
}

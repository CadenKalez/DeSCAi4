#include "rz/cRZVersion.h"

#include <cstdlib>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif

namespace {

class DefaultPlatform final : public IRZVersionPlatform {
public:
    bool BundleName(std::string&) override { return false; }
    bool BundleShortVersion(std::string&) override { return false; }
    bool AppPath(std::string& out) override {
        const char* path = std::getenv("PWD");
        if (path == nullptr) return false;
        out = path;
        return true;
    }
    std::uint64_t CPUFrequencyMax() override {
#ifdef __APPLE__
        std::uint64_t hz = 0;
        std::size_t size = sizeof(hz);
        sysctlbyname("hw.cpufrequency_max", &hz, &size, nullptr, 0);
        return hz;
#else
        return 0;
#endif
    }
    bool MemoryMB(std::uint64_t& physical, std::uint32_t& available) override {
#ifdef __APPLE__
        const mach_port_t host = mach_host_self();
        vm_size_t pageSize = 0;
        host_page_size(host, &pageSize);
        vm_statistics_data_t stats{};
        mach_msg_type_number_t statCount = HOST_VM_INFO_COUNT;
        host_statistics(host, HOST_VM_INFO, reinterpret_cast<host_info_t>(&stats), &statCount);
        host_basic_info_data_t info{};
        mach_msg_type_number_t infoCount = HOST_BASIC_INFO_COUNT;
        if (host_info(host, HOST_BASIC_INFO, reinterpret_cast<host_info_t>(&info), &infoCount) != KERN_SUCCESS) return false;
        physical = info.max_mem >> 20;
        available = static_cast<std::uint32_t>((static_cast<std::uint64_t>(stats.free_count) * pageSize) >> 20)
                  + static_cast<std::uint32_t>((static_cast<std::uint64_t>(stats.inactive_count) * pageSize) >> 20);
        return true;
#else
        (void)physical; (void)available;
        return false;
#endif
    }
    bool UserName(std::string& out) override {
        const char* name = std::getenv("USER");
        if (name == nullptr || *name == '\0') return false;
        out = name;
        return true;
    }
    bool ComputerName(std::string& out) override {
        char buffer[256];
        if (gethostname(buffer, sizeof(buffer)) != 0) return false;
        buffer[sizeof(buffer) - 1] = '\0';
        out = buffer;
        return true;
    }
};

DefaultPlatform g_default;
IRZVersionPlatform* g_platform = &g_default;

}  // namespace

IRZVersionPlatform* GetRZVersionPlatform() { return g_platform; }
void SetRZVersionPlatform(IRZVersionPlatform* platform) { g_platform = platform ? platform : &g_default; }

bool cRZVersion::GetModulePath(cRZString& out) {   // 0x100041db2
    std::string path;
    if (!GetRZVersionPlatform()->AppPath(path)) return false;
    out.AssignFromCString(path.c_str());
    return true;
}

bool cRZVersion::GetModuleName(cRZString& out) {   // 0x100041dc6
    std::string name;
    if (!GetRZVersionPlatform()->BundleName(name)) {
        out.str().erase();
        return false;
    }
    out.str() = name;
    return true;
}

bool cRZVersion::GetModuleVersion(cRZString& out) {   // 0x100041f62
    std::string version;
    if (!GetRZVersionPlatform()->BundleShortVersion(version)) {
        out.str().erase();
        return false;
    }
    out.str() = version;
    return true;
}

bool cRZVersion::GetSystemVersion(cRZString& out) {   // 0x100042122
    out.AssignFromCString("Windows NT 5.1");
    return true;
}

std::uint32_t cRZVersion::GetModuleVersion(std::uint32_t) { return 0xffffffffu; }   // 0x100042142

std::uint32_t cRZVersion::GetCPUSpeed() {   // 0x10004214e
    return static_cast<std::uint32_t>(GetRZVersionPlatform()->CPUFrequencyMax() / 1000000);
}

bool cRZVersion::GetSystemMemory(std::uint32_t& totalMB, std::uint32_t& availableMB) {   // 0x1000421ac
    std::uint64_t physical = 0;
    std::uint32_t available = 0;
    if (!GetRZVersionPlatform()->MemoryMB(physical, available)) {
        availableMB = 0x1000000;
        totalMB = 0x8000000;
        return false;
    }
    totalMB = ((static_cast<std::uint32_t>(physical) - 1U) | 3U) + 1U;
    availableMB = ((available - 1U) | 3U) + 1U;
    return true;
}

bool cRZVersion::GetUserNameA(cRZString& out, const cRZString* fallback) {   // 0x10004227a
    std::string name;
    if (!GetRZVersionPlatform()->UserName(name)) {
        if (fallback == nullptr) out.str() = "?";
        else out.str() = fallback->str();
        return false;
    }
    out.str() = name;
    return true;
}

bool cRZVersion::GetHostName(cRZString& out, const cRZString* fallback) {   // 0x10004244e
    std::string name;
    if (!GetRZVersionPlatform()->ComputerName(name)) {
        if (fallback == nullptr) out.str() = "localhost";
        else out.str() = fallback->str();
        return false;
    }
    out.str() = name;
    return true;
}

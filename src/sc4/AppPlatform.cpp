#include "sc4/AppPlatform.h"

#include <cstdlib>
#include <filesystem>
#include <utility>

#include "sc4/ServiceLocator.h"

namespace {
std::function<bool(std::string&)> g_documentsFolder = [](std::string& out) {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return false;
    out = std::string(home) + "/Documents";
    return true;
};
}  // namespace

void SetDocumentsFolderProvider(std::function<bool(std::string&)> provider) { g_documentsFolder = std::move(provider); }

void SC4SetupFPU() {}
void SC4DisableOSMessageBoxAlerts() {}

bool SC4GetInstalledLanguageAndCountryIDs(unsigned int& language, unsigned int& country) {
    language = 0;
    country = 0;
    return false;
}

int SC4SetCPUUsageCount(int) { return 0; }

void SC4GetUserDataDirectory(cIGZString& out, bool createIfMissing) {
    SL::DataDirectory(out);
    std::string documents;
    if (g_documentsFolder && g_documentsFolder(documents)) {
        out.AssignFromChars(documents.c_str(), static_cast<std::uint32_t>(documents.size()));
        out.AppendChars("/", 1);
    }
    if (out.Strlen() == 0) return;
    out.AppendChars("SimCity 4", 9);
    out.AppendChars("/", 1);
    if (!createIfMissing) return;
    std::error_code error;
    if (std::filesystem::is_directory(out.ToChar(), error)) return;
    if (!std::filesystem::create_directory(out.ToChar(), error)) SL::DataDirectory(out);
}

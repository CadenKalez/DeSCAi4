#include "render/MacPreferences.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace sgl {

namespace {
IPreferencesStore* g_store = nullptr;

}  // namespace

IPreferencesStore* PreferencesStore() { return g_store ? g_store : DefaultPreferencesStore(); }
void SetPreferencesStore(IPreferencesStore* store) { g_store = store; }

void MacPreferences::GetString(const char* key, char* out, int capacity, const char* defaultValue) {
    std::string value;
    if (PreferencesStore()->CopyString(key, value) && static_cast<int>(value.size()) < capacity) {
        std::memcpy(out, value.c_str(), value.size() + 1);
        return;
    }
    if (!defaultValue) {
        *out = '\0';
        return;
    }
    std::strcpy(out, defaultValue);
}

void MacPreferences::PutString(const char* key, const char* value) { PreferencesStore()->SetString(key, value); }

int MacPreferences::GetInteger(const char* key, int defaultValue) {
    char buf[0x100];
    GetString(key, buf, 0x100, nullptr);
    if (buf[0] != '\0') defaultValue = std::atoi(buf);
    return defaultValue;
}

void MacPreferences::PutInteger(const char* key, int value) {
    char buf[0x100];
    std::snprintf(buf, sizeof buf, "%d", value);
    PutString(key, buf);
}

float MacPreferences::GetFloat(const char* key, float defaultValue) {
    char buf[0x100];
    GetString(key, buf, 0x100, nullptr);
    if (buf[0] != '\0') defaultValue = static_cast<float>(std::atof(buf));
    return defaultValue;
}

bool MacPreferences::GetBoolean(const char* key, bool defaultValue) { return GetInteger(key, defaultValue ? 1 : 0) != 0; }

void MacPreferences::PutBoolean(const char* key, bool value) {
    char buf[0x100];
    std::snprintf(buf, sizeof buf, "%d", value ? 1 : 0);
    PutString(key, buf);
}

namespace {
std::string Sub(const char* key, const char* suffix) { return std::string(key) + suffix; }
}  // namespace

void MacPreferences::GetRect(const char* key, CGRect& out) {
    out.x = GetFloat(Sub(key, ".origin.x").c_str(), 0.0f);
    out.y = GetFloat(Sub(key, ".origin.y").c_str(), 0.0f);
    out.width = GetFloat(Sub(key, ".size.width").c_str(), 0.0f);
    out.height = GetFloat(Sub(key, ".size.height").c_str(), 0.0f);
}

void MacPreferences::PutRect(const char* key, const CGRect& rect) {
    char buf[0x100];
    std::snprintf(buf, sizeof buf, "%f", static_cast<double>(static_cast<float>(rect.x)));
    PutString(Sub(key, ".origin.x").c_str(), buf);
    std::snprintf(buf, sizeof buf, "%f", static_cast<double>(static_cast<float>(rect.y)));
    PutString(Sub(key, ".origin.y").c_str(), buf);
    std::snprintf(buf, sizeof buf, "%f", static_cast<double>(static_cast<float>(rect.width)));
    PutString(Sub(key, ".size.width").c_str(), buf);
    std::snprintf(buf, sizeof buf, "%f", static_cast<double>(static_cast<float>(rect.height)));
    PutString(Sub(key, ".size.height").c_str(), buf);
}

void MacPreferences::Synchronize() { PreferencesStore()->Synchronize(); }

}  // namespace sgl

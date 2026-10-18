#include "sc4/StringDetokenizer.h"

#include <strings.h>

#include <algorithm>
#include <string>

#include "gz/cIGZPersistResourceManager.h"
#include "rz/cRZSysServPtr.h"

namespace sc4 {

namespace {
IDetokenizerHost* g_host = nullptr;
constexpr std::uint32_t kPersistResourceManagerService = 0x056b906e, kPersistResourceManagerIid = 0x656b8efc;

std::string Text(const cIGZString& s) { return std::string(s.Data(), s.Strlen()); }
}  // namespace

IDetokenizerHost* DetokenizerHost() { return g_host; }
void SetDetokenizerHost(IDetokenizerHost* host) { g_host = host; }

bool cSCStringDetokenizer::CaseInsensitiveStringCompare::operator()(const cRZString& a, const cRZString& b) const {
    const std::uint32_t shorter = std::min(a.Strlen(), b.Strlen());
    const int result = strncasecmp(a.Data(), b.Data(), shorter);
    return result != 0 ? result < 0 : a.Strlen() < b.Strlen();
}

cSCStringDetokenizer::~cSCStringDetokenizer() = default;

bool cSCStringDetokenizer::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cSCStringDetokenizer) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSCStringDetokenizer::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

void cSCStringDetokenizer::SetTokenReplacementValue(const cIGZString& token, const cIGZString& value) {
    values_[cRZString(token)] = cRZString(value);
}

void cSCStringDetokenizer::SetTokenReplacementMethod(const cIGZString& token, DetokenizeMethod method, void* context) {
    methods_[cRZString(token)] = tMethodDef{method, context};
}

void cSCStringDetokenizer::AddUnknownTokenReplacementMethod(DetokenizeMethod method, void* context, bool add) {
    const tMethodDef def{method, context};
    if (add) unknownMethods_.insert(def);     // an already registered function keeps its first context
    else unknownMethods_.erase(def);
}

bool cSCStringDetokenizer::Detokenize(const cGZPersistResourceKey& key, cIGZString& out) {
    cRZSysServPtr<cIGZPersistResourceManager, kPersistResourceManagerService, kPersistResourceManagerIid> manager;
    cIGZString* resource = nullptr;
    if (!manager || !manager->GetResource(key, kGZIID_cIGZString, reinterpret_cast<void**>(&resource), 0, nullptr)) return false;
    const bool result = Detokenize(*resource, out);
    if (resource) resource->Release();
    return result;
}

void cSCStringDetokenizer::MakeHTMLClean(cIGZString& text) {
    const std::string source = Text(text);
    cRZString cleaned;
    std::string out;
    for (const char c : source) {
        switch (c) {
            case '"': out += "&quot;"; break;
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            default: out.push_back(c);
        }
    }
    cleaned.AssignFromChars(out.data(), static_cast<std::uint32_t>(out.size()));
    text.AssignFromIGZString(cleaned);
}

bool cSCStringDetokenizer::ConvertToken(const cRZString& token, cRZString& out) {
    bool converted = false;
    if (auto value = values_.find(token); value != values_.end()) {
        out.AssignFromIGZString(value->second);
        converted = true;
    } else {
        auto method = methods_.find(token);
        if (method != methods_.end() && method->second.method(token, out, method->second.context)) {
            converted = true;
        } else {
            for (const tMethodDef& unknown : unknownMethods_)
                if (unknown.method(token, out, unknown.context)) {
                    converted = true;
                    break;
                }
        }
    }
    if (!converted) out.AssignFromIGZString(token);
    if (htmlClean_) MakeHTMLClean(out);
    return converted;
}

bool cSCStringDetokenizer::PerformDetokenize(const cIGZString& in, cIGZString& out) {
    std::string text = Text(in);
    std::string result;
    bool ok = true;
    const std::size_t opener = text.find('#');
    if (opener == std::string::npos) {
        if (&in != &out) out.Copy(in);
        return true;
    }
    // "##" -> a NUL placeholder for a literal '#'
    for (std::size_t pair; (pair = text.find("##")) != std::string::npos;) {
        text.erase(pair, 1);
        text[pair] = '\0';
    }
    std::size_t open = opener;      // the position found before the escapes were folded (kept: it can be stale)
    std::size_t consumed = 0;
    if (open < text.size()) {
        for (;;) {
            result.append(text, consumed, open - consumed);
            consumed = open + 1;
            const std::size_t close = text.find('#', consumed);
            if (close >= text.size()) {
                result.append(text, open, std::string::npos);     // an unclosed marker stays as it is
                consumed = text.size();
                break;
            }
            cRZString token(text.substr(consumed, close - consumed).c_str());
            token.Trim();
            cRZString replacement;
            if (!ConvertToken(token, replacement)) ok = false;
            result.append(replacement.Data(), replacement.Strlen());
            consumed = close + 1;
            open = text.find('#', consumed);
            if (!(open < text.size())) break;
        }
    }
    if (consumed < text.size()) result.append(text, consumed, std::string::npos);
    for (char& c : result)
        if (c == '\0') c = '#';
    cRZString finished;
    finished.AssignFromChars(result.data(), static_cast<std::uint32_t>(result.size()));
    out.Copy(finished);
    return ok;
}

// ------------------------------------------------------------------------------------------------------ defaults

bool cSCStringDetokenizerDefault::Init() {
    city_ = cRZString("City");
    SetTokenReplacementMethod(city_, DefaultReplacementFunctionStatic, this);
    mayor_ = cRZString("Mayor");
    SetTokenReplacementMethod(mayor_, DefaultReplacementFunctionStatic, this);
    year_ = cRZString("Year");
    SetTokenReplacementMethod(year_, DefaultReplacementFunctionStatic, this);
    population_ = cRZString("Population");
    SetTokenReplacementMethod(population_, DefaultReplacementFunctionStatic, this);
    region_ = cRZString("Region");
    SetTokenReplacementMethod(region_, DefaultReplacementFunctionStatic, this);
    return true;
}

bool cSCStringDetokenizerDefault::DefaultReplacementFunctionStatic(const cIGZString& token, cIGZString& out, void* self) {
    return static_cast<cSCStringDetokenizerDefault*>(self)->DefaultReplacementFunction(token, out);
}

bool cSCStringDetokenizerDefault::DefaultReplacementFunction(const cIGZString& token, cIGZString& out) {
    IDetokenizerHost* host = g_host;
    if (city_.IsEqualToStringPointer(&token, false)) return host && host->CityName(out);
    if (mayor_.IsEqualToStringPointer(&token, false)) return host && host->MayorName(out);
    if (year_.IsEqualToStringPointer(&token, false)) {
        int year = 0;
        if (!host || !host->GetYear(year)) return false;
        host->FormatNumber(year, out);
        return true;
    }
    if (population_.IsEqualToStringPointer(&token, false)) {
        int population = 0;
        if (!host || !host->GetPopulation(population)) return false;
        host->FormatNumber(population, out);
        return true;
    }
    if (region_.IsEqualToStringPointer(&token, false)) return host && host->GetRegionName(out);
    return false;
}

}  // namespace sc4

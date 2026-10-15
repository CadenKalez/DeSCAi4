#include "html/HTMLShared.h"

#include <cstdlib>
#include <cstring>

#include "rz/RZConvert.h"

namespace html {

namespace {
struct NamedColor { const char* name; const char* hex; };
constexpr NamedColor kNamedColors[] = {
    {"BLACK", "000000"},   {"SILVER", "C0C0C0"}, {"GRAY", "808080"},  {"WHITE", "FFFFFF"},
    {"MAROON", "800000"},  {"RED", "FF0000"},    {"PURPLE", "800080"}, {"FUCHSIA", "FF00FF"},
    {"GREEN", "008000"},   {"LIME", "00FF00"},   {"OLIVE", "808000"}, {"YELLOW", "FFFF00"},
    {"NAVY", "000080"},    {"BLUE", "0000FF"},   {"TEAL", "008080"},  {"AQUA", "00FFFF"},
};

bool IsHexDigit(char c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'); }
}  // namespace

void cHTMLShared::SetColor(const cRZString& text, Color& color) {
    std::string value = text.str();
    if (!value.empty() && value[0] != '#') {
        for (char& c : value)
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    for (const NamedColor& named : kNamedColors) {
        if (value == named.name) {
            value = named.hex;
            break;
        }
    }
    if (!value.empty() && value[0] == '#') value.erase(0, 1);
    if (value.size() != 6) return;
    for (char c : value)
        if (!IsHexDigit(c)) return;
    color = static_cast<Color>(std::strtoul(value.c_str(), nullptr, 16)) | 0xff000000u;
}

void cHTMLShared::SetLength(const cRZString& text, tLength& length) {
    length.unit = tLength::kPixels;
    const std::string& s = text.str();
    if (!s.empty() && s.back() == '%') {
        length.unit = tLength::kPercent;
        length.value = RZ::ConvertToUint(s.substr(0, s.size() - 1).c_str());
    } else {
        length.value = RZ::ConvertToUint(s.c_str());
    }
}

void cHTMLShared::SetPixels(const cRZString& text, std::uint32_t& pixels) { pixels = RZ::ConvertToUint(text.ToChar()); }

bool cHTMLShared::StringLeftEquals(const cRZString& text, std::uint32_t count, const char* literal) {
    if (text.str().size() < count) return false;
    const char* p = text.str().c_str();
    const char* end = p + count;
    while (p < end && *literal != '\0' && *p == *literal) {
        ++p;
        ++literal;
    }
    return p == end;
}

bool cHTMLShared::StringLeftEquals(const cRZString& text, char c) { return !text.str().empty() && text.str()[0] == c; }

}  // namespace html

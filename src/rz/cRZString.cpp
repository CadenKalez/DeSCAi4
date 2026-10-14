#include "rz/cRZString.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr std::uint32_t kUseStrlen = 0xFFFFFFFFu;

inline char FoldLower(char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }
inline char FoldUpper(char c) noexcept { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 32) : c; }
inline bool IsTrimChar(char c) noexcept { return c == ' ' || c == '\t'; }  // mask 0x100000200

// Allocation-free equivalent of "lowercase both, then memcmp" over `n` bytes.
int CompareFolded(const char* a, const char* b, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        const auto ca = static_cast<unsigned char>(FoldLower(a[i]));
        const auto cb = static_cast<unsigned char>(FoldLower(b[i]));
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    return 0;
}

std::string Lowered(const char* s, std::size_t n) {
    std::string out(s, n);
    for (char& c : out) c = FoldLower(c);
    return out;
}

}  // namespace

// cRZString::QueryInterface(unsigned int, void**) @ 0x10001651e
bool cRZString::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZString) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

// cRZString::Release() @ 0x10001654e
std::uint32_t cRZString::Release() {
    if (refCount_ > 1) {
        return --refCount_;
    }
    delete this;
    return 0;
}

// cRZString::FromChar(char const*) @ 0x1000170b6 [AssignFromCString], FromChar(char const*, unsigned int) @ 0x1000170fe [AssignFromChars]
void cRZString::AssignFromCString(const char* s) {
    if (s == nullptr) str_.clear(); else str_.assign(s);
}
void cRZString::AssignFromChars(const char* s, std::uint32_t length) {
    if (s == nullptr) str_.clear(); else str_.assign(s, length);
}

// cRZString::IsEqual(...) @ 0x100017174 / 0x1000173ba / 0x1000173cc
bool cRZString::IsEqualToStringRef(const cIGZString& other, bool caseSensitive) const {
    const std::uint32_t n = other.Strlen();
    if (str_.size() != n) return false;
    return caseSensitive ? std::strncmp(str_.data(), other.Data(), n) == 0
                         : CompareFolded(str_.data(), other.Data(), n) == 0;
}
bool cRZString::IsEqualToChars(const char* s, std::uint32_t length, bool caseSensitive) const {
    const std::size_t n = (length == kUseStrlen) ? std::strlen(s) : length;
    if (str_.size() != n) return false;
    return caseSensitive ? std::strncmp(str_.data(), s, n) == 0 : CompareFolded(str_.data(), s, n) == 0;
}

// cRZString::CompareTo(...) @ 0x1000175d4 / 0x100017824
int cRZString::CompareToString(const cIGZString& other, bool caseSensitive) const {
    return CompareToChars(other.Data(), other.Strlen(), caseSensitive);
}
int cRZString::CompareToChars(const char* s, std::uint32_t length, bool caseSensitive) const {
    const std::uint32_t otherLen = (length == kUseStrlen) ? static_cast<std::uint32_t>(std::strlen(s)) : length;
    const auto myLen = static_cast<std::uint32_t>(str_.size());
    const std::size_t common = myLen < otherLen ? myLen : otherLen;
    int r = caseSensitive ? std::strncmp(str_.data(), s, common) : CompareFolded(str_.data(), s, common);
    if (r == 0) {
        r = static_cast<int>(myLen - otherLen);  // common prefix equal: shorter sorts first
    }
    return r;
}

// cRZString::Find(...) @ 0x100017be2 / 0x100017f0a
std::size_t cRZString::FindChars(const char* s, std::size_t pos, bool caseSensitive) const {
    if (caseSensitive) return str_.find(s, pos);
    const std::string hay = Lowered(str_.data(), str_.size());
    const std::string needle = Lowered(s, std::strlen(s));
    return hay.find(needle, pos);
}

// cRZString::RFind(...) @ 0x100018230 / 0x1000185f6
std::size_t cRZString::ReverseFindChars(const char* s, std::size_t pos, bool caseSensitive) const {
    if (caseSensitive) return str_.rfind(s, pos);
    const std::string hay = Lowered(str_.data(), str_.size());
    const std::string needle = Lowered(s, std::strlen(s));
    return hay.rfind(needle, pos);
}

// cRZString::Sprintf(char const*, ...) @ 0x1000189c2 — see header note on glyph widths.
cIGZString* cRZString::Sprintf(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    va_list copy;
    va_copy(copy, ap);
    const int needed = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (needed < 0) {
        str_.clear();
    } else {
        str_.resize(static_cast<std::size_t>(needed));
        // writes needed+1 bytes including the terminator, which std::string guarantees room for
        std::vsnprintf(str_.data(), str_.size() + 1, format, ap);
    }
    va_end(ap);
    return this;
}

// cRZString::SplitToken(char const*) @ 0x1000165ac
cRZString cRZString::SplitToken(const char* delimiters) {
    if (delimiters == nullptr || delimiters[0] == '\0') {
        return cRZString(*this, 0, std::string::npos);
    }
    const std::size_t delimLen = std::strlen(delimiters);
    const std::size_t pos = str_.find(delimiters, 0, delimLen);
    if (pos == std::string::npos) {
        return cRZString();
    }
    cRZString token(*this, 0, pos);
    str_.erase(0, pos + delimLen);
    return token;
}

// cRZString::SplitTokenDelimited(char) @ 0x100016782
bool cRZString::DiscardThroughDelimiter(char delimiter) {
    const std::size_t pos = str_.find(delimiter);
    if (pos == std::string::npos) {
        str_.clear();
        return false;
    }
    str_.erase(0, pos + 1);
    return true;
}

// cRZString::SplitTokenDelimited(char, cRZString&) @ 0x1000167d2
bool cRZString::SplitTokenAtDelimiter(char delimiter, cRZString& token) {
    const std::size_t pos = str_.find(delimiter);
    if (pos == std::string::npos) {
        token.str_.clear();
        str_.swap(token.str_);  // whole remainder becomes the token
        return false;
    }
    token.str_.assign(str_, 0, pos);
    str_.erase(0, pos + 1);
    return true;
}

// cRZString::SplitTokenSeparated(char, cRZString&) @ 0x1000168a8
bool cRZString::SplitTokenSeparated(char delimiter, cRZString& token) {
    for (;;) {
        const std::size_t pos = str_.find(delimiter);
        if (pos == std::string::npos) break;
        std::size_t end = pos;
        while (end < str_.size() && str_[end] == delimiter) ++end;  // swallow the whole run
        if (pos != 0) {
            token.str_.assign(str_, 0, pos);
            str_.erase(0, end);
            return true;
        }
        str_.erase(0, end);  // leading delimiters carry no token
    }
    if (str_.empty()) return false;
    token.str_.clear();
    token.str_.swap(str_);
    return true;
}

// cRZString::Strncpy(char const*, unsigned long) @ 0x100016a1c
cRZString& cRZString::Strncpy(const char* s, std::size_t maxLength) {
    const std::size_t len = std::strlen(s);
    str_.assign(s, len < maxLength ? len : maxLength);
    return *this;
}

// cRZString::Mid(unsigned long, unsigned long) const @ 0x100016ae2
cRZString cRZString::Mid(std::size_t pos, std::size_t count) const {
    if (pos >= str_.size()) return cRZString();
    const std::size_t avail = str_.size() - pos;
    return cRZString(*this, pos, count < avail ? count : avail);
}

// cRZString::Right(unsigned long) const @ 0x100016b5c — a request longer than the string
// yields the whole string.
cRZString cRZString::Right(std::size_t count) const {
    if (str_.size() < count) return cRZString(*this, 0, std::string::npos);
    return Mid(str_.size() - count, count);
}

// cRZString::BeginsWith / EndsWith @ 0x100016bd4 / 0x100016c2a
bool cRZString::BeginsWith(const char* s, std::size_t length) const {
    return str_.size() >= length && std::memcmp(str_.data(), s, length) == 0;
}
bool cRZString::EndsWith(const char* s, std::size_t length) const {
    return str_.size() >= length && std::memcmp(str_.data() + str_.size() - length, s, length) == 0;
}

// cRZString::MakeLower() / MakeUpper() @ 0x100016c8e / 0x100016e16 — ASCII only, see header.
void cRZString::MakeLower() { for (char& c : str_) c = FoldLower(c); }
void cRZString::MakeUpper() { for (char& c : str_) c = FoldUpper(c); }

// cRZString::LTrim() / RTrim() @ 0x100016f9e / 0x100017008
void cRZString::LTrim() {
    std::size_t n = 0;
    while (n < str_.size() && IsTrimChar(str_[n])) ++n;
    str_.erase(0, n);
}
void cRZString::RTrim() {
    std::size_t end = str_.size();
    while (end > 0 && IsTrimChar(str_[end - 1])) --end;
    str_.erase(end);
}

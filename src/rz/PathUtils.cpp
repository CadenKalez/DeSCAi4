#include "rz/PathUtils.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "rz/TextEncoding.h"

namespace {
constexpr const char* kReservedNames[23] = {
    "con", "prn", "aux", "clock$", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
    "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};

bool IsInvalidNameChar(unsigned char c) { return c == '"' || c == '*' || c == '/' || c == '?' || c == '|'; }
}  // namespace

bool IsFullPath(const cRZString& path) {
    const char* s = path.ToChar();
    return s[0] == '/' && std::strstr(s, "/.") == nullptr;
}

bool Fullpath(std::string& out, const char* path, const char* base) {
    if (!path) return false;
    char buf[0x220];
    std::size_t length = 0;
    if (*base) {
        for (; base[length]; ++length) {
            if (length > 0x1ff) return false;
            buf[length] = base[length];
        }
        if (buf[length - 1] != '/') buf[length++] = '/';
    }
    const std::size_t pathLength = std::strlen(path);
    if (length + pathLength > 0x200) return false;
    std::memcpy(buf + length, path, pathLength + 1);
    const long total = static_cast<long>(length + pathLength);
    const bool trailingSlash = total > 0 && buf[total - 1] == '/';

    auto at = [&](long i) -> char { return i < 0 ? '\0' : buf[i]; };
    const char* read = buf;
    long w = 0;
    for (;;) {
        const char c = *read;
        char* dest = buf + w;
        if (c == '/' || c == '\0') {
            long next = w;
            if (w > 1) {
                next = w - 2;
                if (!(at(w - 1) == '.' && at(w - 2) == '/')) {
                    if (w < 3) {
                        next = 2;
                    } else {
                        next = w;
                        if (at(w - 1) == '.' && at(w - 2) == '.' && at(w - 3) == '/') {
                            if (at(w - 4) == '\0') return false;
                            long k = w - 3;
                            if (w > 3) {
                                long i = w;
                                for (;;) {
                                    k = i;
                                    if (at(k - 4) == '/') break;
                                    i = k - 1;
                                    if (!(1 < k - 3)) break;
                                }
                                k = k - 4;
                            }
                            next = k;
                            if (k == 0) next = 0;      // climbing to the root: the original wrote before the buffer
                        }
                    }
                }
            }
            if (c == '\0') {
                w = next;
                break;
            }
            dest = buf + next;
            w = next;
        }
        ++read;
        ++w;
        *dest = c;
    }
    w -= (at(w - 1) == '/');
    if (at(w - 1) == '\0') buf[w++] = '/';
    buf[w] = '\0';
    out.assign(buf);
    if (trailingSlash) out.push_back('/');
    return out.size() < 0x200;
}

bool Fullpath(cRZString& out, const cRZString& path, const cRZString* base) {
    std::string result;
    if (!Fullpath(result, path.ToChar(), base ? base->ToChar() : "")) return false;
    out.AssignFromCString(result.c_str());
    return true;
}

bool ResolvePath(cRZString& out, const cRZString& path, const cRZString* base) {
    if (IsFullPath(path)) {
        out.AssignFromCString(path.ToChar());
        return true;
    }
    return Fullpath(out, path, base);
}

bool IsFileNameValid(const cRZString& name, const cRZString* fileSystem) {
    const std::string text(name.ToChar());
    if (text.size() > 0x100) return true;
    for (unsigned char c : text)
        if (IsInvalidNameChar(c)) return false;
    const std::string fs = fileSystem ? fileSystem->ToChar() : "";
    if (fs == "FAT" || fs == "HPFS" || fs == "NTFS") {
        std::string lower = text;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        const std::size_t dot = lower.find('.');
        if (dot != std::string::npos) lower.erase(dot);
        for (const char* reserved : kReservedNames)
            if (lower == reserved) return false;
    }
    for (unsigned char c : text)
        if (c != ' ' && c != '.') return true;
    return false;
}

bool MakeFileNameValid(const cRZString& name, cRZString& out, const cRZString*) {
    std::string result;
    for (unsigned char c : std::string(name.ToChar()))
        if (!IsInvalidNameChar(c)) result.push_back(static_cast<char>(c));
    if (result.size() > 0x100) {
        result.resize(0x100);
        while (!result.empty() &&
               !IsStringValid(result.data(), static_cast<unsigned>(result.size()), -2))
            result.pop_back();
    }
    out.AssignFromCString(result.c_str());
    return true;
}

bool MatchPattern(const char* text, const char* pattern) {
    for (;;) {
        char c = *pattern++;
        for (;;) {
            if (c == '*' && *pattern == '\0') return true;
            if (*text == '\0') return c == '\0';
            if (c != '*') break;
            if (MatchPattern(text, pattern)) return true;
            ++text;
        }
        if (c != '?' && *text != c) return false;
        ++text;
    }
}

bool WildcardMatch(const cRZString& text, const cRZString& pattern, bool caseSensitive) {
    std::string a(text.ToChar()), b(pattern.ToChar());
    if (!caseSensitive) {
        std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return std::tolower(c); });
        std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return std::tolower(c); });
    }
    return MatchPattern(a.c_str(), b.c_str());
}

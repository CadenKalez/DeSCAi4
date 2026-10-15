#include "rz/RZStringUtils.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "rz/RZConvert.h"

char* RZStruprAImp(char* text) {
    for (char* p = text; *p; ++p) *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));
    return text;
}

char* RZStrlwrAImp(char* text) {
    for (char* p = text; *p; ++p) *p = static_cast<char>(std::tolower(static_cast<unsigned char>(*p)));
    return text;
}

int RZToupperWImp(int ch) { return ch > 0x7f ? ch : ch - 0x20; }
int RZTolowerWImp(int ch) { return ch > 0x7f ? ch : ch + 0x20; }

double RZAtofA(const char* text) {
    char* end;
    return std::strtod(text, &end);
}

double RZStrtodEnglishA(const char* text, char** end) {
    const char* p = text;
    while (std::isspace(static_cast<unsigned char>(*p))) ++p;
    char sign = '+';
    if (*p == '+' || *p == '-') sign = *p++;
    double value = 0.0;
    const char* last = p;
    while (static_cast<unsigned char>(*p - '0') < 10) {
        value = static_cast<double>(*p - '0') + value * 10.0;
        ++p;
    }
    last = p;
    if (*p == '.') {
        ++p;
        double scale = 1.0;
        last = p;
        while (static_cast<unsigned char>(*p - '0') < 10) {
            scale *= 0.1;
            value += static_cast<double>(*p - '0') * scale;
            ++p;
        }
        last = p;
    }
    if ((static_cast<unsigned char>(*p) | 0x20) == 'e') {
        const char* q = p + 1;
        char expSign = '+';
        if (*q == '-' || *q == '+') expSign = *q++;
        double exponent = 0.0;
        if (static_cast<unsigned char>(*q - '0') < 10) {
            int digits = 0;
            while (static_cast<unsigned char>(*q - '0') < 10) {
                digits = (*q - '0') + digits * 10;
                ++q;
            }
            exponent = static_cast<double>(digits);
            last = q;
        } else {
            last = q;
        }
        double scale = std::pow(10.0, exponent);
        if (expSign == '-') scale = 1.0 / scale;
        value *= scale;
    }
    if (end) *end = const_cast<char*>(last);
    return sign == '-' ? -value : value;
}

double RZAtofEnglishA(const char* text) {
    char* end;
    return RZStrtodEnglishA(text, &end);
}

bool RZFtoaEnglishA(double value, char* out, int size, int maxFraction) {
    if (size < 1) return false;
    int decpt = 0, sign = 0;
    const char* digits = fcvt(value, 64, &decpt, &sign);
    int n = 0;
    auto fail = [&](int index) {
        index = index - 1;
        if (index < 0) index = 0;
        out[index] = '\0';
        return false;
    };
    if (sign) out[n++] = '-';
    if (std::fabs(value) < 1.0) {
        if (size <= n) return fail(n);
        out[n++] = '0';
    }
    int used = 0;
    if (decpt >= 1) {
        for (; used < decpt && digits[used] != '\0'; ++used) {
            if (size <= n) return fail(n);
            out[n++] = digits[used];
        }
    }
    // position just after the last non-zero fractional digit
    int lastSignificant = used;
    for (int i = used; digits[i] != '\0'; ++i)
        if (digits[i] != '0') lastSignificant = i + 1;
    if (digits[used] != '\0' && maxFraction > 0 && used < lastSignificant) {
        if (size <= n) return fail(n);
        out[n++] = '.';
        if (decpt < 0) {
            for (int zeros = 0; zeros < -decpt; ++zeros) {
                if (size <= n) return fail(n);
                out[n++] = '0';
            }
        }
        int written = 0;
        while (digits[used + written] != '\0') {
            if (size <= n) {
                // truncated: terminate and strip trailing zeros
                int end = n - 1;
                if (end < 0) end = 0;
                out[end] = '\0';
                while (end > 1 && out[end - 1] == '0') out[--end] = '\0';
                return false;
            }
            out[n++] = digits[used + written];
            ++written;
            if (lastSignificant <= used + written || maxFraction <= written) break;
        }
    }
    if (n < size) {
        out[n] = '\0';
        return true;
    }
    return fail(n);
}

std::int64_t RZAtoi64A(const char* text) {
    const char* p = text;
    while (std::isspace(static_cast<unsigned char>(*p))) ++p;
    char sign = '+';
    if (*p == '+' || *p == '-') sign = *p++;
    std::int64_t value = 0;
    while (static_cast<unsigned char>(*p - '0') < 10) value = (*p++ - '0') + value * 10;
    return sign == '-' ? -value : value;
}

std::uint32_t RZAtoUint32HexA(const char* text) { return RZ::ConvertHexToUint(text); }
std::uint64_t RZAtoUint64HexA(const char* text) { return RZ::ConvertHexToUint64(text); }
std::int64_t RZAtoLongHexA(const char* text) { return static_cast<std::int64_t>(RZ::ConvertHexToUint64(text)); }

char* RZLtoaAImp(long value, char* out, int radix) {
    char* cursor = out;
    if (static_cast<unsigned>(radix - 2) < 15) {
        int count = 0;
        if (value < 0) {
            if (radix == 10) { *cursor++ = '-'; }
            value = -value;
        }
        char reversed[104];
        unsigned long v = static_cast<unsigned long>(value);
        const unsigned long r = static_cast<unsigned long>(radix);
        do {
            const char d = static_cast<char>(v % r);
            reversed[count++] = d + '0' > '9' ? d + 'W' : d + '0';
            v /= r;
        } while (v != 0);
        while (count > 0) *cursor++ = reversed[--count];
    }
    *cursor = '\0';
    return out;
}

void RZItoaAImp(int value, char* out, int radix) { RZLtoaAImp(static_cast<long>(value), out, radix); }

void ConvertBinaryDataToASCIIArray(const std::uint8_t* data, unsigned size, char* out) {
    for (unsigned i = 0; i < size; ++i) {
        std::uint8_t hi = static_cast<std::uint8_t>(data[i] >> 4 | 0x30);
        if (hi > 0x39) hi += 7;
        std::uint8_t lo = static_cast<std::uint8_t>((data[i] & 0xf) | 0x30);
        if (lo > 0x39) lo += 7;
        *out++ = static_cast<char>(hi);
        *out++ = static_cast<char>(lo);
    }
    *out = '0';                      // (sic) the terminator is the character '0'
}

bool ConvertASCIIArrayToBinaryData(const char* text, unsigned size, std::uint8_t* out) {
    bool ok = true;
    const char* end = text + size;
    while (text < end) {
        unsigned byte = 0;
        for (int nibble = 0; nibble < 2; ++nibble) {
            char c = text[nibble];
            if (c < '0') { c = '0'; ok = false; }
            else if (c < 'G') {
                if (static_cast<unsigned char>(c - 0x3a) < 7) { c = '0'; ok = false; }
                else if (c > '@') c = static_cast<char>(c - 7);
            } else {
                if (static_cast<unsigned char>(c + 0x9f) > 5) { c = '0'; ok = false; }
                else c = static_cast<char>(c - 0x27);
            }
            byte += static_cast<unsigned>(c - 0x30) << (nibble == 0 ? 4 : 0);
        }
        *out++ = static_cast<std::uint8_t>(byte);
        text += 2;
    }
    return ok;
}

void memset8(void* destination, std::uint8_t value, unsigned count) { std::memset(destination, value, count); }
void memset16(void* destination, std::uint16_t value, unsigned count) {
    auto* p = static_cast<std::uint16_t*>(destination);
    for (unsigned i = 0; i < count; ++i) p[i] = value;
}
void memset32(void* destination, std::uint32_t value, unsigned count) {
    auto* p = static_cast<std::uint32_t*>(destination);
    for (unsigned i = 0; i < count; ++i) p[i] = value;
}

bool RZParseDelimitedText(const char* text, unsigned length, char delimiter, cRZString* out, unsigned& count) {
    const char* end = text + length;
    const char* p = text;
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    if (p == end) {
        count = 0;
        return true;
    }
    const std::string s(p, static_cast<std::size_t>(end - p));
    const unsigned capacity = count;
    count = 0;
    const unsigned len = static_cast<unsigned>(s.size());
    auto finish = [&](unsigned begin, unsigned stop) {
        std::string token = s.substr(begin, stop - begin);
        if (delimiter != ' ') {
            while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) token.pop_back();
        }
        if (!token.empty() && token.front() == '"' && token.back() == '"') {
            token.pop_back();
            if (!token.empty()) token.erase(0, 1);
        }
        out[count].AssignFromCString(token.c_str());
    };
    unsigned start = 0, i = 0;
    unsigned quotes = 0;
    while (i < len && count < capacity) {
        const char c = s[i];
        const bool isDelimiter = delimiter == ' ' ? (c == ' ' || c == '\t') : c == delimiter;
        if (isDelimiter) {
            if ((quotes & 1) == 0) {
                finish(start, i);
                start = i + 1;
                while (start < len && (s[start] == ' ' || s[start] == '\t')) ++start;
                i = start;
                ++count;
                quotes = 0;
                continue;
            }
        } else if (c == '"') {
            ++quotes;
        }
        ++i;
    }
    if (count < capacity && i > start) {
        finish(start, i);
        ++count;
        start = i + 1;
    }
    return i <= start && !(i < len);
}

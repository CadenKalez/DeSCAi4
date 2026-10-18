#include "gz/MiscFunctions.h"

#include "rz/CharLength.h"
#include "rz/RZStringUtils.h"
#include "rz/TextEncoding.h"

bool HTMLIsSpace(const char* text, int length) {
    if (length <= 0) return false;
    if (GetCurrentFrameworkCodePage() == 8) {
        const unsigned b = static_cast<unsigned char>(*text) - 9u;
        if (b < 0x18) return ((0x80001bu >> (b & 0x1f)) & 1) != 0;
        return false;
    }
    const unsigned charLength = static_cast<unsigned>(GetCharLength(text, static_cast<std::uint32_t>(length), -2));
    std::uint16_t ch = 0;
    unsigned capacity = 1;
    ConvertStringEncoding(text, charLength, -2, &ch, capacity, 0x10);
    if (capacity != 0) {
        if (ch < 0x21 && ((0x100003600ull >> (ch & 0x3f)) & 1) != 0) return true;
        if (ch == 0x200b) return true;
    }
    return false;
}

bool HTMLIsSpace(const cRZString& text, int index) {
    return HTMLIsSpace(text.ToChar() + index, static_cast<int>(text.Strlen()) - index);
}

int HTMLAdvance(const cRZString& text, int index) {
    return GetCharLength(text.ToChar() + index, text.Strlen() - static_cast<unsigned>(index), -2) + index;
}

int HTMLBackspace(const cRZString& text, int index) {
    return index - static_cast<int>(GetPreviousCharLength(text.ToChar() + index, static_cast<unsigned>(index), -2));
}

std::uint32_t gCombineGUIDs(int a, int b) { return static_cast<std::uint32_t>(a ^ b); }

namespace {
bool IsHex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
}  // namespace

bool gConvertURLFromEndodedForm(const cRZString& in, cRZString& out) {
    std::string result;
    const char* s = in.ToChar();
    const int length = static_cast<int>(in.Strlen());
    for (int i = 0; i < length; ++i) {
        if (s[i] == '%' && i + 2 < length && IsHex(s[i + 1]) && IsHex(s[i + 2])) {
            const char digits[3] = {s[i + 1], s[i + 2], 0};
            result.push_back(static_cast<char>(RZAtoUint32HexA(digits)));
            i += 2;
        } else {
            result.push_back(s[i]);
        }
    }
    out.AssignFromChars(result.data(), static_cast<std::uint32_t>(result.size()));
    return true;
}

void RemoveUnnecessaryFloatingPointDigits(char* s, int length) {
    if (length <= 0) return;
    int dot = -1, exponent = -1;
    for (int i = 0; i < length; ++i) {
        if (s[i] == '.') dot = i;
        if ((s[i] | 0x20) == 'e') exponent = i;
    }
    const int first = dot >= 0 ? dot + 1 : 0;      // first fraction digit
    int end = exponent < 0 ? length : exponent;     // one past the mantissa
    if (first < end) {
        long at = end;
        while (s[at - 1] == '0') {
            const long victim = at - 1;
            for (long k = victim; k < length; ++k) s[k] = s[k + 1];   // also moves the terminator
            --length;
            at = victim;
            if (!(first < victim)) break;
        }
    }
    if (dot >= 0 && dot == length - 1) {
        s[dot] = '\0';
        length = dot;
    }
    int k = 0;
    if (length > 0) {
        while (k < length && (s[k] == '0' || s[k] == '.')) ++k;
    }
    if (k == length) *s = '0';
}

namespace {
int OutCode(int x, int y, int minX, int minY, int maxX, int maxY) {
    int code = (x < minX) ? 4 : 0;
    if (x > maxX) code += 8;
    code |= (y < minY) ? 1 : 0;
    if (y > maxY) code |= 2;
    return code;
}
}  // namespace

bool ClipLineToRect(int& x0, int& y0, int& x1, int& y1, int minX, int minY, int maxX, int maxY) {
    int code0 = OutCode(x0, y0, minX, minY, maxX, maxY);
    int code1 = OutCode(x1, y1, minX, minY, maxX, maxY);
    while (code0 != 0 || code1 != 0) {
        if ((code0 & code1) != 0) return false;
        const int code = code0 != 0 ? code0 : code1;
        int x = 0, y = 0;
        if (code & 1) {
            y = minY;
            x = ((minY - y0) * (x1 - x0)) / (y1 - y0) + x0;
        } else if (code & 2) {
            y = maxY;
            x = ((maxY - y0) * (x1 - x0)) / (y1 - y0) + x0;
        } else if (code & 8) {
            x = maxX;
            y = ((maxX - x0) * (y1 - y0)) / (x1 - x0) + y0;
        } else if (code & 4) {
            x = minX;
            y = ((minX - x0) * (y1 - y0)) / (x1 - x0) + y0;
        }
        if (code == code0) {
            x0 = x;
            y0 = y;
            code0 = OutCode(x0, y0, minX, minY, maxX, maxY);
        } else {
            x1 = x;
            y1 = y;
            code1 = OutCode(x1, y1, minX, minY, maxX, maxY);
        }
    }
    return true;
}

void HillToTerrainRect(int rotation, int width, int depth, const SC4RectInt& in, SC4RectInt& out) {
    switch (rotation) {
        case 0:
            out.left = in.top; out.right = in.bottom; out.top = in.left; out.bottom = in.right;
            break;
        case 1:
            out.left = (width - 1) - in.right; out.right = (width - 1) - in.left; out.top = in.top; out.bottom = in.bottom;
            break;
        case 2:
            out.left = (width - 1) - in.bottom; out.right = (width - 1) - in.top;
            out.top = (depth - 1) - in.right; out.bottom = (depth - 1) - in.left;
            break;
        case 3:
            out.left = in.left; out.right = in.right; out.top = (depth - 1) - in.bottom; out.bottom = (depth - 1) - in.top;
            break;
        default:
            break;
    }
}

void FastAddOffset(std::uint16_t* values, std::uint32_t count, int offset) {
    const std::uint16_t add = static_cast<std::uint16_t>(offset);
    for (std::uint32_t i = 0; i < count; ++i) values[i] = static_cast<std::uint16_t>(values[i] + add);
}

float SafeGetAltitude(ISTETerrainAltitude* terrain, float x, float z) {
    const float cellX = terrain->GetCellSize();
    const int width = terrain->GetWidthInCells();
    const float cellZ = terrain->GetCellSize();
    const int depth = terrain->GetDepthInCells();
    float cx = 0.0f;
    if (0.0f <= x) cx = x;
    float cz = 0.0f;
    if (0.0f <= z) cz = z;
    const float maxX = static_cast<float>(width) * cellX;
    const float maxZ = static_cast<float>(depth) * cellZ;
    if (maxX <= cx) cx = maxX;
    if (maxZ <= cz) cz = maxZ;
    return terrain->GetAltitude(cx, cz);
}

int stricmp(const char* a, const char* b) {
    auto lower = [](const char* p) -> signed char {
        const int c = static_cast<unsigned char>(*p);
        return static_cast<signed char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    };
    signed char ca = lower(a), cb = lower(b);
    while (cb <= ca) {
        ++a;
        ++b;
        if (cb < ca) return 1;
        if (ca == 0) return 0;
        ca = lower(a);
        cb = lower(b);
    }
    return -1;
}

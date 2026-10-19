#include "rz/cRZTextFile.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr std::uint32_t kChunk = 0x80;

void AppendUtf8(std::string& out, std::uint32_t c) {
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800) { out += static_cast<char>(0xc0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3f)); }
    else { out += static_cast<char>(0xe0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 0x3f)); out += static_cast<char>(0x80 | (c & 0x3f)); }
}
}  // namespace

bool cRZTextFile::Open(int access, int creation, int shareFlags) {
    if (!cRZFile::Open(access, creation, shareFlags)) return false;
    if (Length() > 1) {
        std::uint16_t bom = 0;
        if (Read(&bom, 2)) {
            if (bom == 0xfffe || bom == 0xfeff) {
                textMode_ = 2;
                hasByteOrderMark_ = true;
            } else {
                SeekToPosition(0);
            }
        }
    }
    return true;
}

bool cRZTextFile::ReadLine(cIGZString& out) {
    const bool wasOpen = IsOpen();
    out.Resize(0);
    if (!wasOpen && !Open(1, 2, 1)) return false;
    int position = Position();
    const int size = static_cast<int>(Length());
    int remaining = size - position;
    if (remaining == 0 || position > size) return false;

    const std::uint32_t unit = textMode_ == 2 ? 2 : 1;
    std::string line;
    bool any = false, finished = false;
    std::uint8_t buffer[kChunk];
    while (remaining != 0 && !finished) {
        const int chunk = std::min<int>(remaining, kChunk);
        if (!Read(buffer, static_cast<std::uint32_t>(chunk))) break;
        int used = 0;
        for (; used < chunk; used += static_cast<int>(unit)) {
            const std::uint32_t ch = unit == 2 ? static_cast<std::uint32_t>(buffer[used] | (buffer[used + 1] << 8)) : buffer[used];
            if (ch != 0x0d && ch != 0x0a) continue;
            SeekToPosition(static_cast<std::uint32_t>(position + static_cast<int>(unit) + used));
            std::uint16_t next = 0;
            if (Read(&next, unit)) {
                const std::uint32_t n = unit == 2 ? next : (next & 0xff);
                if ((n != 0x0d && n != 0x0a) || n == ch) SeekToRelativePosition(-static_cast<int>(unit));
            }
            finished = true;
            break;
        }
        if (unit == 2) {
            for (int i = 0; i + 2 <= used; i += 2) AppendUtf8(line, static_cast<std::uint32_t>(buffer[i] | (buffer[i + 1] << 8)));
        } else {
            line.append(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(used));   // the code page is UTF-8 on macOS
        }
        any = true;
        if (finished) break;
        remaining -= chunk;
        position = Position();
    }
    out.AssignFromChars(line.data(), static_cast<std::uint32_t>(line.size()));
    return any;
}

bool cRZTextFile::WriteLine(const void* data, std::uint32_t bytes) {
    if (!Write(data, bytes)) return false;
    static const char narrow[] = "\n";
    static const char wide[] = {'\n', 0};
    return textMode_ == 2 ? Write(wide, 2) : Write(narrow, 1);
}

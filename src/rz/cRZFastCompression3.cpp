#include "rz/cRZFastCompression3.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

constexpr std::uint32_t kHashSize = 0x10000;
constexpr std::uint32_t kChainSize = 0x20000;
constexpr std::int64_t kWindow = 0x1ffff;
constexpr std::uint32_t kMaxMatch = 0x404;

// RefPack encoder. Reads past `length` yield zero (the original callers pad their buffers with two
// zero bytes). Returns the number of bytes written.
std::size_t RefPackEncode(const std::uint8_t* src, std::int64_t length, std::uint8_t* dst, bool quick) {
    std::vector<std::int32_t> head(kHashSize, -1);
    std::vector<std::int32_t> chain(kChainSize, -1);
    std::uint8_t* out = dst;
    if (length <= 0) {
        *out++ = 0xfc;
        return 1;
    }
    auto at = [&](std::int64_t i) -> std::uint32_t { return i < length ? src[i] : 0u; };

    std::int64_t cur = 0, literalStart = 0, run = 0, remaining = length;
    auto flushLiterals = [&]() {
        while (run > 3) {
            std::int64_t n = std::min<std::int64_t>(run & ~std::int64_t(3), 0x70);
            *out++ = static_cast<std::uint8_t>(((n >> 2) | 0xe0) - 1);
            std::memcpy(out, src + literalStart, static_cast<std::size_t>(n));
            out += n;
            literalStart += n;
            run -= n;
        }
    };

    while (remaining > 0) {
        std::uint32_t maxLen = static_cast<std::uint32_t>(std::min<std::int64_t>(remaining, kMaxMatch));
        std::uint32_t hash = (at(cur + 1) << 4) ^ ((at(cur) << 8) | at(cur + 2));
        std::int64_t candidate = head[hash];
        std::int64_t windowStart = cur >= 0x20000 ? cur - kWindow : 0;

        std::uint32_t bestLen = 2, bestCost = 2;
        std::int64_t bestOffset = 0;
        if (candidate >= windowStart) {
            std::uint32_t probe = at(cur + 2);
            while (true) {
                const std::uint8_t* p = src + candidate;
                if (probe == p[bestLen]) {
                    std::uint32_t len = 0;
                    do {
                        if (at(cur + len) != p[len]) break;
                        ++len;
                    } while (len < maxLen);
                    if (bestLen < len) {
                        std::int64_t offset = cur - candidate - 1;
                        std::uint32_t cost;
                        if (offset < 0x400 && len < 0xb) cost = 2;
                        else cost = 4 - (len < 0x44 && offset < 0x4000 ? 1u : 0u);
                        if ((bestLen + 4) - bestCost < (len + 4) - cost) {
                            bestLen = len;
                            bestOffset = offset;
                            bestCost = cost;
                            if (len > 0x403) break;
                        }
                    }
                }
                candidate = chain[static_cast<std::uint32_t>(candidate) & (kChainSize - 1)];
                if (candidate < windowStart) break;
                probe = at(cur + bestLen);
            }
        }

        if (bestCost < bestLen) {
            flushLiterals();
            std::uint32_t r = static_cast<std::uint32_t>(run);
            std::uint32_t off = static_cast<std::uint32_t>(bestOffset);
            if (bestCost == 3) {
                *out++ = static_cast<std::uint8_t>(bestLen + 0x7c);
                *out++ = static_cast<std::uint8_t>((off >> 8) + r * 0x40);
                *out++ = static_cast<std::uint8_t>(off);
            } else if (bestCost == 2) {
                *out++ = static_cast<std::uint8_t>(r + ((off >> 3) & 0xe0) + bestLen * 4 - 0xc);
                *out++ = static_cast<std::uint8_t>(off);
            } else {
                std::uint32_t lenField = bestLen - 5;
                *out++ = static_cast<std::uint8_t>(r + 0xc0 + ((lenField >> 6) & 0xfc) + ((off >> 12) & 0xf0));
                *out++ = static_cast<std::uint8_t>(off >> 8);
                *out++ = static_cast<std::uint8_t>(off);
                *out++ = static_cast<std::uint8_t>(lenField);
            }
            if (run) {
                std::memcpy(out, src + literalStart, static_cast<std::size_t>(run));
                out += run;
            }
            if (quick) {
                chain[static_cast<std::uint32_t>(cur) & (kChainSize - 1)] = head[hash];
                head[hash] = static_cast<std::int32_t>(cur);
                cur += bestLen;
            } else {
                for (std::uint32_t i = 0; i < bestLen; ++i, ++cur) {
                    std::uint32_t h = (at(cur + 1) << 4) ^ ((at(cur) << 8) | at(cur + 2));
                    std::int32_t previous = head[h];
                    head[h] = static_cast<std::int32_t>(cur);
                    chain[static_cast<std::uint32_t>(cur) & (kChainSize - 1)] = previous;
                }
            }
            remaining -= bestLen;
            run = 0;
            literalStart = cur;
        } else {
            // (the original stores the head it read, not the chain walk result)
            chain[static_cast<std::uint32_t>(cur) & (kChainSize - 1)] = head[hash];
            head[hash] = static_cast<std::int32_t>(cur);
            ++run;
            --remaining;
            ++cur;
        }
    }
    flushLiterals();
    *out++ = static_cast<std::uint8_t>(run + 0xfc);
    if (run) {
        std::memcpy(out, src + literalStart, static_cast<std::size_t>(run));
        out += run;
    }
    return static_cast<std::size_t>(out - dst);
}

// Decodes the command stream starting at `p` (after the header) into `dst` (capacity `cap`).
// Returns false on malformed input.
bool RefPackDecode(const std::uint8_t* p, const std::uint8_t* end, std::uint8_t* dst, std::size_t cap) {
    std::size_t o = 0;
    while (p < end) {
        std::uint32_t c = *p++;
        std::size_t plain = 0, copy = 0, off = 0;
        auto need = [&](std::size_t n) { return static_cast<std::size_t>(end - p) >= n; };
        if (c < 0x80) {
            if (!need(1)) return false;
            std::uint32_t b1 = *p++;
            plain = c & 3;
            copy = ((c >> 2) & 7) + 3;
            off = (((c & 0x60) << 3) | b1) + 1;
        } else if (c < 0xc0) {
            if (!need(2)) return false;
            std::uint32_t b1 = p[0], b2 = p[1];
            p += 2;
            plain = b1 >> 6;
            copy = (c & 0x3f) + 4;
            off = (((b1 & 0x3f) << 8) | b2) + 1;
        } else if (c < 0xe0) {
            if (!need(3)) return false;
            std::uint32_t b1 = p[0], b2 = p[1], b3 = p[2];
            p += 3;
            plain = c & 3;
            copy = (((c & 0x0c) << 6) | b3) + 5;
            off = (((c & 0x10) << 12) | (b1 << 8) | b2) + 1;
        } else if (c < 0xfc) {
            plain = (static_cast<std::size_t>(c & 0x1f) << 2) + 4;
        } else {
            plain = c & 3;
            if (!need(plain) || o + plain > cap) return false;
            std::memcpy(dst + o, p, plain);
            return true;
        }
        if (plain) {
            if (!need(plain) || o + plain > cap) return false;
            std::memcpy(dst + o, p, plain);
            p += plain;
            o += plain;
        }
        if (copy) {
            if (off > o || o + copy > cap) return false;
            for (std::size_t i = 0; i < copy; ++i, ++o) dst[o] = dst[o - off];
        }
    }
    return false;  // no terminator
}

}  // namespace

bool cRZFastCompression3::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZCompression) {
        *out = static_cast<cIGZCompression*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cRZFastCompression3::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cRZFastCompression3::CompressData(const void* source, std::uint32_t length, std::uint8_t* destination,
                                       std::uint32_t& outLength) {
    std::uint8_t* body = destination + 4;
    body[0] = 0x10;
    body[1] = 0xfb;
    body[2] = static_cast<std::uint8_t>(length >> 16);
    body[3] = static_cast<std::uint8_t>(length >> 8);
    body[4] = static_cast<std::uint8_t>(length);
    std::size_t n = RefPackEncode(static_cast<const std::uint8_t*>(source), length, body + 5, quick_);
    std::uint32_t total = static_cast<std::uint32_t>(n + 5 + 4);
    std::memcpy(destination, &total, 4);  // little-endian host
    outLength = total;
    return total != 0;
}

std::uint32_t cRZFastCompression3::GetLengthOfCompressedData(const std::uint8_t* data) {
    std::uint32_t v;
    std::memcpy(&v, data, 4);
    return v;
}

std::uint32_t cRZFastCompression3::GetLengthOfDecompressedData(const std::uint8_t* data) {
    std::uint32_t magic = (std::uint32_t(data[4]) << 8) | data[5];
    const std::uint8_t* size = data + 6 + (magic != 0x10fb ? 3 : 0);
    return (std::uint32_t(size[0]) << 16) | (std::uint32_t(size[1]) << 8) | size[2];
}

bool cRZFastCompression3::ValidateCompressedData(const std::uint8_t* data, std::uint32_t length) {
    if (length <= 6) return false;
    std::uint32_t magic = (std::uint32_t(data[4]) << 8) | data[5];
    if (magic != 0x10fb && magic != 0x11fb) return false;
    return GetLengthOfCompressedData(data) < 0x1000001 && GetLengthOfDecompressedData(data) < 0x1000001;
}

bool cRZFastCompression3::DecompressData(const std::uint8_t* source, std::uint32_t length, void* destination,
                                         std::uint32_t& outLength) {
    // Header after the 4-byte prefix: 2 magic bytes, optional 3-byte compressed size (flag bit 0),
    // 3-byte uncompressed size, then commands.
    if (length < 9) { outLength = 0; return false; }
    const std::uint8_t* h = source + 4;
    const std::uint8_t* sizeField = (h[0] & 1) ? h + 5 : h + 2;
    std::uint32_t expected = (std::uint32_t(sizeField[0]) << 16) | (std::uint32_t(sizeField[1]) << 8) | sizeField[2];
    const std::uint8_t* commands = sizeField + 3;
    const std::uint8_t* end = source + length;
    if (commands > end || expected > outLength) { outLength = 0; return false; }
    bool ok = RefPackDecode(commands, end, static_cast<std::uint8_t*>(destination), expected);
    outLength = ok ? expected : 0;
    return ok;
}

void cRZFastCompression3::GetCompressionVersionString(cIGZString& out) { out.AssignFromCString("0.90"); }

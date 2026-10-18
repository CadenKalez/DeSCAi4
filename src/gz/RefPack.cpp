#include "gz/RefPack.h"

#include <cstdlib>
#include <cstring>

namespace {
constexpr long kWindowMask = 0x1ffff;
constexpr std::size_t kHashEntries = 0x10000;        // 8-byte entries, 0x80000 bytes
constexpr std::size_t kChainEntries = 0x20000;       // 8-byte entries, 0x100000 bytes
}  // namespace

std::uint32_t sizeref(const std::uint8_t* p) {
    const std::uint32_t magic = (static_cast<std::uint32_t>(p[0]) << 8) | p[1];
    const std::uint8_t* size = p + 2 + (magic != 0x10fb ? 3 : 0);
    return (static_cast<std::uint32_t>(size[0]) << 16) | (static_cast<std::uint32_t>(size[1]) << 8) | size[2];
}

std::uint32_t decoderef(const std::uint8_t* packed, std::ptrdiff_t* consumed, std::uint8_t* out) {
    std::uint32_t result = 0;
    const std::uint8_t* src = nullptr;
    if (packed) {
        const std::uint8_t* header = packed + ((packed[0] & 1) ? 5 : 2);
        result = (static_cast<std::uint32_t>(header[0]) << 16) | (static_cast<std::uint32_t>(header[1]) << 8) | header[2];
        src = header + 3;
        auto literals = [&](unsigned count) {
            for (unsigned i = 0; i < count; ++i) *out++ = *src++;
        };
        auto copyBack = [&](std::size_t offset, unsigned length) {
            for (unsigned i = 0; i < length; ++i, ++out) *out = *(out - offset);
        };
        for (;;) {
            const unsigned b0 = *src;
            if (b0 < 0x80) {
                const unsigned b1 = src[1];
                src += 2;
                literals(b0 & 3);
                copyBack(((b0 & 0x60) << 3) + b1 + 1, ((b0 >> 2) & 7) + 3);
            } else if (b0 < 0xc0) {
                const unsigned b1 = src[1], b2 = src[2];
                src += 3;
                literals(b1 >> 6);
                copyBack(((b1 & 0x3f) << 8) + b2 + 1, (b0 & 0x3f) + 4);
            } else if (b0 < 0xe0) {
                const unsigned b1 = src[1], b2 = src[2], b3 = src[3];
                src += 4;
                literals(b0 & 3);
                copyBack(((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1, ((b0 & 0x0c) << 6) + b3 + 5);
            } else {
                const unsigned run = ((b0 & 0x1f) << 2) + 4;
                ++src;
                if (run > 0x70) {
                    literals(b0 & 3);   // end of stream, 0..3 trailing literals
                    break;
                }
                literals(run);
            }
        }
    }
    if (consumed) *consumed = src - packed;
    return result;
}

std::size_t encoderef(const std::uint8_t* in, long size, std::uint8_t* out, const int* quick) {
    const bool fast = quick && *quick != 0;
    // Header: 0x10 0xfb, then the size as 24 bits big endian.
    out[0] = 0x10;
    out[1] = 0xfb;
    out[2] = static_cast<std::uint8_t>(size >> 16);
    out[3] = static_cast<std::uint8_t>(size >> 8);
    out[4] = static_cast<std::uint8_t>(size);
    auto* head = static_cast<long*>(std::malloc(kHashEntries * sizeof(long)));
    auto* chain = static_cast<long*>(std::malloc(kChainEntries * sizeof(long)));
    if (!head || !chain) {
        std::free(head);
        std::free(chain);
        return 5;
    }
    std::memset(head, 0xff, kHashEntries * sizeof(long));
    // The match search peeks a few bytes past the end; those read as zero here.
    auto byteAt = [&](long i) -> std::uint8_t { return i < size ? in[i] : 0; };
    auto hashAt = [&](long i) -> std::size_t {
        return (static_cast<std::size_t>(byteAt(i + 1)) << 4) ^ ((static_cast<std::size_t>(byteAt(i)) << 8) | byteAt(i + 2));
    };

    std::uint8_t* put = out + 5;
    long pending = 0;           // literals not yet written
    long literalStart = 0;      // where they start in the input
    long pos = 0, remaining = size;

    auto flushLiterals = [&]() {
        while (pending > 3) {
            long n = pending & ~3L;
            if (n > 0x70) n = 0x70;
            *put++ = static_cast<std::uint8_t>(((n >> 2) | 0xe0) - 1);
            std::memcpy(put, in + literalStart, static_cast<std::size_t>(n));
            literalStart += n;
            put += n;
            pending -= n;
        }
    };

    while (remaining > 0) {
        const long maxLength = remaining < 0x404 ? remaining : 0x404;
        const std::size_t h = hashAt(pos);
        const long candidate = head[h];
        const long lowest = pos < 0x20000 ? 0 : pos - 0x1ffff;
        long bestLength = 2, bestCost = 2, bestOffset = 0;
        bool matched = false;
        if (lowest <= candidate) {
            long at = candidate;
            std::uint8_t probe = byteAt(pos + 2);
            for (;;) {
                if (probe == byteAt(at + bestLength)) {
                    long length = 0;
                    do {
                        if (byteAt(pos + length) != byteAt(at + length)) break;
                        ++length;
                    } while (length < maxLength);
                    if (bestLength < length) {
                        const long offset = pos - 1 - at;
                        const long cost = (offset < 0x400 && length < 0xb) ? 2 : 4 - ((length < 0x44 && offset < 0x4000) ? 1 : 0);
                        if ((bestLength + 4) - bestCost < (length + 4) - cost) {
                            bestLength = length;
                            bestOffset = offset;
                            bestCost = cost;
                            if (length > 0x403) break;
                        }
                    }
                }
                at = chain[at & kWindowMask];
                if (at < lowest) break;
                probe = byteAt(pos + bestLength);
            }
            matched = bestCost < bestLength;
        }
        if (matched) {
            flushLiterals();
            const std::uint8_t lit = static_cast<std::uint8_t>(pending);
            if (bestCost == 3) {
                put[0] = static_cast<std::uint8_t>(bestLength + 0x7c);
                put[1] = static_cast<std::uint8_t>((bestOffset >> 8) + lit * 0x40);
                put[2] = static_cast<std::uint8_t>(bestOffset);
                put += 3;
            } else if (bestCost == 2) {
                put[0] = static_cast<std::uint8_t>(lit - 0xc + ((bestOffset >> 3) & 0xe0) + bestLength * 4);
                put[1] = static_cast<std::uint8_t>(bestOffset);
                put += 2;
            } else {
                put[0] = static_cast<std::uint8_t>(lit - 0x40 + (((bestLength - 5) >> 6) & 0xfc) + ((bestOffset >> 12) & 0xf0));
                put[1] = static_cast<std::uint8_t>(bestOffset >> 8);
                put[2] = static_cast<std::uint8_t>(bestOffset);
                put[3] = static_cast<std::uint8_t>(bestLength - 5);
                put += 4;
            }
            if (pending != 0) {
                std::memcpy(put, in + literalStart, static_cast<std::size_t>(pending));
                put += pending;
            }
            if (fast) {
                chain[pos & kWindowMask] = head[h];
                head[h] = pos;
                pos += bestLength;
            } else {
                for (long k = 0; k < bestLength; ++k, ++pos) {
                    const std::size_t h2 = hashAt(pos);
                    const long old = head[h2];
                    head[h2] = pos;
                    chain[pos & kWindowMask] = old;
                }
            }
            remaining -= bestLength;
            pending = 0;
            literalStart = pos;
        } else {
            chain[pos & kWindowMask] = candidate;
            head[h] = pos;
            ++pending;
            --remaining;
            ++pos;
        }
    }
    flushLiterals();
    *put++ = static_cast<std::uint8_t>(pending - 4);
    if (pending != 0) {
        std::memcpy(put, in + literalStart, static_cast<std::size_t>(pending));
        put += pending;
    }
    std::free(chain);
    std::free(head);
    return static_cast<std::size_t>(put - (out + 5)) + 5;
}

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "gz/RefPack.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
bool RoundTrip(const std::vector<std::uint8_t>& data, int quick, std::size_t* packedSize = nullptr) {
    std::vector<std::uint8_t> packed(data.size() * 2 + 1024);
    const std::size_t n = encoderef(data.data(), static_cast<long>(data.size()), packed.data(), &quick);
    if (packedSize) *packedSize = n;
    if (n < 6 || packed[0] != 0x10 || packed[1] != 0xfb) return false;
    if (sizeref(packed.data()) != data.size()) return false;
    std::vector<std::uint8_t> out(data.size() + 8, 0xee);
    std::ptrdiff_t used = 0;
    const std::uint32_t size = decoderef(packed.data(), &used, out.data());
    return size == data.size() && static_cast<std::size_t>(used) == n && std::memcmp(out.data(), data.data(), data.size()) == 0 &&
           out[data.size()] == 0xee;
}
}  // namespace

int main() {
    CHECK(RoundTrip({}, 0));
    CHECK(RoundTrip({7}, 0));
    CHECK(RoundTrip({1, 2, 3, 4, 5}, 0));
    std::vector<std::uint8_t> zeros(5000, 0);
    std::size_t packed = 0;
    CHECK(RoundTrip(zeros, 0, &packed) && packed < 100);
    CHECK(RoundTrip(zeros, 1, &packed) && packed < 200);
    std::vector<std::uint8_t> text;
    const char* phrase = "the quick brown fox jumps over the lazy dog; ";
    for (int i = 0; i < 400; ++i) text.insert(text.end(), phrase, phrase + std::strlen(phrase));
    CHECK(RoundTrip(text, 0, &packed) && packed < text.size() / 10);
    CHECK(RoundTrip(text, 1, &packed));
    std::srand(12345);
    std::vector<std::uint8_t> noise(70000);
    for (auto& b : noise) b = static_cast<std::uint8_t>(std::rand());
    CHECK(RoundTrip(noise, 0, &packed));
    std::vector<std::uint8_t> mixed;
    for (int i = 0; i < 300; ++i) {
        for (int j = 0; j < 30 + (i % 7); ++j) mixed.push_back(static_cast<std::uint8_t>(std::rand()));
        mixed.insert(mixed.end(), text.begin() + (i * 13) % 500, text.begin() + (i * 13) % 500 + 40 + i % 90);
    }
    CHECK(RoundTrip(mixed, 0) && RoundTrip(mixed, 1));
    std::vector<std::uint8_t> far(140000);
    for (std::size_t i = 0; i < far.size(); ++i) far[i] = static_cast<std::uint8_t>((i * 2654435761u) >> 13);
    std::copy(far.begin(), far.begin() + 3000, far.begin() + 135000);   // a match from nearly out of the window
    CHECK(RoundTrip(far, 0));

    CHECK(decoderef(nullptr, nullptr, nullptr) == 0);
    std::ptrdiff_t used = 99;
    CHECK(decoderef(nullptr, &used, nullptr) == 0 && used == 0);
    // A header with the flag bit set carries the sizes at byte 5.
    const std::uint8_t flagged[] = {0x11, 0xfb, 0, 0, 9, 0, 0, 3, 0xfd, 'a', 'b', 'c'};   // flagged header: size 3
    CHECK(sizeref(flagged) == 3);
    std::printf(failures ? "FAILED\n" : "gz_refpack ok\n");
    return failures != 0;
}

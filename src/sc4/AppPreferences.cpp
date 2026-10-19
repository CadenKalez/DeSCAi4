#include "sc4/AppPreferences.h"

namespace sc4 {

namespace {
template <class T> void Put(std::uint8_t* base, std::size_t offset, T value) { std::memcpy(base + offset, &value, sizeof value); }
}  // namespace

void SC4Preferences::ResetAudio(std::uint8_t* t) {
    t[0] = 1;
    Put<std::uint64_t>(t, 4, 0x32000000400ull);
    Put<std::uint64_t>(t, 0xc, 1);
    Put<std::uint64_t>(t, 0x14, 0x100000001ull);
    Put<std::uint64_t>(t, 0x1c, 0x100000001ull);
    for (std::size_t o = 0x24; o <= 0x5c; o += 8) Put<std::uint64_t>(t, o, 0);
    t[0x64] = 0;
    Put<std::uint16_t>(t, 0x65, 0x0101);
    std::memset(t + 0x28, 1, 0x1e);
    std::memset(t + 0x46, 1, 0x1e);
}

void SC4Preferences::ResetApp(std::uint8_t* t) {
    t[0] = 0;
    Put<std::uint32_t>(t, 4, 1);
    for (std::size_t o = 8; o <= 0x40; o += 8) Put<std::uint64_t>(t, o, 0);
    Put<std::uint32_t>(t, 0x48, 0x1000101);
    std::memset(t + 0x4c, 0, 0x960);
}

void SC4Preferences::ResetTutorial(std::uint8_t* t) { std::memset(t, 0, 0x800); }

void SC4Preferences::Reset() {
    std::uint8_t* t = bytes_.data();
    Put<std::uint16_t>(t, 0, 0);
    ResetAudio(t + prefs::kAudio);
    Put<std::uint32_t>(t, 0x46c, 0);
    std::memset(t + 0x26c, 0, 0x200);
    std::memset(t + 0x06c, 0, 0x200);
    Put<std::uint32_t>(t, 0x470, 0x42480000);      // 50.0f
    t[0x474] = 0;
    t[0x498] = 0;
    Put<std::uint64_t>(t, 0x490, 0);
    Put<std::uint64_t>(t, 0x488, 0);
    Put<std::uint64_t>(t, 0x480, 0);
    Put<std::uint64_t>(t, 0x478, 0);
    std::memset(t + 0x478, 0, 0x10);
    std::memset(t + 0x488, 0, 0x10);
    t[0x49c] = 1;
    Put<std::uint32_t>(t, 0x4a0, 0);
    ResetApp(t + prefs::kApp);
    Put<std::uint64_t>(t, 0xe50, 0);
    std::memset(t + 0xe58, 0, 100);
    Put<std::uint64_t>(t, 0xefc, 0x8000000080000000ull);
    Put<std::uint16_t>(t, 0xf04, 0x0101);
    std::memset(t + 0xebc, 0, 0x40);
    Put<std::uint32_t>(t, 0xf08, 0x01010101);
    t[0xf0c] = 1;
    Put<std::uint64_t>(t, 0xf0d, 0x101010103030303ull);
    t[0xf15] = 1;
    Put<std::uint16_t>(t, 0xf16, 0x0103);
    t[0xf18] = 1;
    Put<std::uint64_t>(t, 0xf1c, 0x30000000400ull);
    Put<std::uint32_t>(t, 0xf24, 0);
    t[0xf28] = 3;
    Put<std::uint64_t>(t, 0xf2c, 0);
    ResetTutorial(t + prefs::kTutorial);
}

}  // namespace sc4

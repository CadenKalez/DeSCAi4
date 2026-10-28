// GameImage - test tooling, not part of the game: maps the retail x86_64 binary (game-binaries/MacOS/
// "Sim City 4 Deluxe Editionsub") inside an x86_64 process (run under Rosetta on Apple Silicon) so a recovered function
// can be compared with the original machine code itself instead of a decompiler transcription.
//
// The binary is non-PIE with __TEXT at 0x100000000 and __DATA at 0x100a65000, but Rosetta's runtime occupies
// 0x100000000..0x108008000 in a translated process, so the image is mapped at kBase with both segments at their
// original distance: RIP-relative constants, globals, calls and import stubs all still resolve. Absolute 64-bit
// pointers into the image (vtables, pointer tables) do not, so only leaf code is called this way. Nothing is bound or
// initialised: callers bind the import stubs a function reaches (BindStub) and write the globals it reads (At<T>);
// every address argument is the original one (0x1...), translated by Here().
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace machine_oracle {

constexpr std::uint64_t kImageAddress = 0x100000000ull, kBase = 0x300000000ull;
constexpr std::uint64_t kTextAddress = kBase, kTextSize = 0xa65000;
constexpr std::uint64_t kDataAddress = kBase + 0xa65000, kDataSize = 0x135000, kDataFileOffset = 10899456,
                        kDataFileSize = 774144;

// where an original address lives in this process
constexpr std::uint64_t Here(std::uint64_t original) { return original - kImageAddress + kBase; }

inline bool MapGame(const char* path) {
    const int fd = open(path, O_RDONLY);
    if (fd < 0) { std::perror(path); return false; }
    void* text = mmap(reinterpret_cast<void*>(kTextAddress), kTextSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON,
                      -1, 0);
    void* data = mmap(reinterpret_cast<void*>(kDataAddress), kDataSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON,
                      -1, 0);
    if (text != reinterpret_cast<void*>(kTextAddress) || data != reinterpret_cast<void*>(kDataAddress)) {
        std::fprintf(stderr, "game image range is not free (text %p, data %p)\n", text, data);
        close(fd);
        return false;
    }
    const bool ok = pread(fd, text, kTextSize, 0) == static_cast<ssize_t>(kTextSize) &&
                    pread(fd, data, kDataFileSize, kDataFileOffset) == static_cast<ssize_t>(kDataFileSize);
    close(fd);
    return ok && mprotect(text, kTextSize, PROT_READ | PROT_EXEC) == 0;
}

template <typename T> T& At(std::uint64_t address) { return *reinterpret_cast<T*>(Here(address)); }

// An import stub is `jmp qword ptr [rip + disp32]`; point its slot at `function`.
inline void BindStub(std::uint64_t stub, const void* function) {
    const auto* code = reinterpret_cast<const std::uint8_t*>(Here(stub));
    std::int32_t disp;
    std::memcpy(&disp, code + 2, 4);
    At<const void*>(stub + 6 + static_cast<std::int64_t>(disp)) = function;
}

template <typename F> F Function(std::uint64_t address) { return reinterpret_cast<F>(Here(address)); }

}  // namespace machine_oracle

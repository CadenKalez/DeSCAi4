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
#include <signal.h>
#include <sys/ucontext.h>
#include <sys/mman.h>
#include <unistd.h>

namespace machine_oracle {

constexpr std::uint64_t kImageAddress = 0x100000000ull, kBase = 0x200000000000ull;
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

// What the harness is doing, printed if the original code faults (lldb cannot attach to a Rosetta process).
inline const char* gStage = "start";
inline void CrashReport(int sig, siginfo_t* info, void* context) {
    const auto* uc = static_cast<ucontext_t*>(context);
    const std::uint64_t rip = uc->uc_mcontext->__ss.__rip;
    const std::uint64_t original = rip >= kBase && rip < kBase + 0xb9a000 ? rip - kBase + kImageAddress : 0;
    std::fprintf(stderr, "signal %d in stage '%s': fault address %p, rip %#llx (original %#llx)\n", sig, gStage,
                 info->si_addr, static_cast<unsigned long long>(rip), static_cast<unsigned long long>(original));
    _exit(3);
}
inline void InstallCrashReport() {
    struct sigaction action = {};
    action.sa_sigaction = CrashReport;
    action.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
    sigaction(SIGILL, &action, nullptr);
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

// Redirect a whole original function to `function` (same calling convention): its entry becomes
// `movabs rax, function; jmp rax`. For callees that are not under test or reach services that cannot be faked.
inline bool Hook(std::uint64_t address, const void* function) {
    auto* code = reinterpret_cast<std::uint8_t*>(Here(address));
    auto* page = reinterpret_cast<std::uint8_t*>(reinterpret_cast<std::uintptr_t>(code) & ~std::uintptr_t(0xfff));
    if (mprotect(page, 0x2000, PROT_READ | PROT_WRITE) != 0) return false;
    code[0] = 0x48; code[1] = 0xb8;
    std::memcpy(code + 2, &function, 8);
    code[10] = 0xff; code[11] = 0xe0;
    return mprotect(page, 0x2000, PROT_READ | PROT_EXEC) == 0;
}

// A stand-in for a service the original reaches through a global pointer (SL::sp*): an object whose first qword points
// at a slot table. Set(offset, fn) fills the slot the original calls as `call [vtable + offset]` (fn takes `this`).
struct FakeService {
    void** vtable = slots;
    void* slots[256] = {};
    void Set(int offset, const void* fn) { slots[offset / 8] = const_cast<void*>(fn); }
    void Install(std::uint64_t global) { At<void*>(global) = this; }
};

}  // namespace machine_oracle

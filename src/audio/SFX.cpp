#include "audio/SFX.h"

#include <cstddef>

namespace sfx {
namespace {

ISfxHost* gHost = nullptr;

// The shared body of the three accessors: read the member at `offset` out of the director, ping its
// reference count (AddRef then Release, as the original does) and return it borrowed.
void* MemberAt(std::ptrdiff_t offset) {
    ISfxHost* host = Host();
    if (!host) return nullptr;
    void* director = host->Director();
    if (!director) return nullptr;
    void* member = *reinterpret_cast<void**>(static_cast<char*>(director) + offset);
    if (!member) return nullptr;
    host->AddRefUnknown(member);
    host->ReleaseUnknown(member);
    return member;
}

}  // namespace

ISfxHost* Host() { return gHost; }
void SetHost(ISfxHost* host) { gHost = host; }

// 0x1003ba4b2
void* Listener() { return MemberAt(0xc0); }

// 0x1003efc28
void* SoundManager() { return MemberAt(0xb8); }

// 0x10040d1ba
void* AmbienceManager() { return MemberAt(0xa0); }

}  // namespace sfx

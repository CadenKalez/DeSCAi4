// SFX: the three director member accessors, the borrowed-reference quirk and the null paths.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "audio/SFX.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {

// A stand-in for cSC4AudioCOMDirector: a buffer with the three members at their real offsets.
struct FakeDirector {
    alignas(8) unsigned char storage[0xd0] = {};
    void Put(std::size_t offset, void* value) { std::memcpy(storage + offset, &value, sizeof value); }
};

struct FakeHost final : sfx::ISfxHost {
    FakeDirector* director = nullptr;
    int addRefs = 0, releases = 0;
    void* Director() override { return director; }
    void AddRefUnknown(void* p) override { (void)p; ++addRefs; }
    void ReleaseUnknown(void* p) override { (void)p; ++releases; }
};

}  // namespace

int main() {
    FakeHost host;
    int listener = 1, sound = 2, ambience = 3;

    // no host: everything is null
    sfx::SetHost(nullptr);
    CHECK(sfx::Listener() == nullptr && sfx::SoundManager() == nullptr && sfx::AmbienceManager() == nullptr);

    sfx::SetHost(&host);
    CHECK(host.director == nullptr);
    CHECK(sfx::Listener() == nullptr && host.addRefs == 0);   // no director, no pings

    FakeDirector director;
    host.director = &director;
    CHECK(sfx::Listener() == nullptr && host.addRefs == 0);   // empty director: null members, no pings

    // the members sit at +0xc0 / +0xb8 / +0xa0 and come back borrowed, after one AddRef + Release each
    director.Put(0xc0, &listener);
    director.Put(0xb8, &sound);
    director.Put(0xa0, &ambience);

    CHECK(sfx::Listener() == &listener);
    CHECK(host.addRefs == 1 && host.releases == 1);
    CHECK(sfx::SoundManager() == &sound);
    CHECK(host.addRefs == 2 && host.releases == 2);
    CHECK(sfx::AmbienceManager() == &ambience);
    CHECK(host.addRefs == 3 && host.releases == 3);
    CHECK(host.addRefs == host.releases);                     // the ping never changes the count

    // the members are independent: clearing one leaves the others alone
    director.Put(0xb8, nullptr);
    CHECK(sfx::Listener() == &listener);
    CHECK(sfx::SoundManager() == nullptr);
    CHECK(sfx::AmbienceManager() == &ambience);
    CHECK(host.addRefs == 5 && host.releases == 5);            // only the two non-null members were pinged

    // a different director answers its own members
    FakeDirector other;
    other.Put(0xc0, &ambience);
    host.director = &other;
    CHECK(sfx::Listener() == &ambience);

    sfx::SetHost(nullptr);
    if (failures == 0) std::printf("sfx: ok\n");
    return failures == 0 ? 0 : 1;
}

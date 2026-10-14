// cGZPersistResourceKey — (type, group, instance) triple identifying a persisted resource.
// Layout from cHitResource<T>::GetKey/SetKey: three consecutive uint32.
#pragma once
#include <cstdint>

struct cGZPersistResourceKey {
    std::uint32_t type = 0;
    std::uint32_t group = 0;
    std::uint32_t instance = 0;

    friend bool operator==(const cGZPersistResourceKey&, const cGZPersistResourceKey&) = default;
};

// Ordering used by every key-indexed map of the persistence layer (cGZDBSegmentPackedFile::
// KeyTGIOrder / ResourceKeyOrder): instance first, then group, then type.
struct cGZPersistResourceKeyOrder {
    bool operator()(const cGZPersistResourceKey& a, const cGZPersistResourceKey& b) const noexcept {
        if (a.instance != b.instance) return a.instance < b.instance;
        if (a.group != b.group) return a.group < b.group;
        return a.type < b.type;
    }
};
static_assert(sizeof(cGZPersistResourceKey) == 12);

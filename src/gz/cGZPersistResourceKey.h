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
static_assert(sizeof(cGZPersistResourceKey) == 12);

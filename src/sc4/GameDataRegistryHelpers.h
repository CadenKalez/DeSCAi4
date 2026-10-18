// nsISC4GameDataRegistry — the header-only helpers every simulator uses to publish itself in the game-data tree
// (27 template instantiations scattered through the binary: AddNode<Owner, Parent>, AddValue<Parent> in its value and
// command forms, and the three scalar exchange functions). Each instantiation is the same few lines for a different type.
//
// AddNode registers `owner` (through its cIGZUnknown view) as a child node of `parent`'s node, or of the root when no
// parent is given. AddValue registers a value or a command under `parent`'s node (the root when null); a value is
// never flagged as a command (flags & ~1), a command always is (flags | 1). Both answer 0 when the parent node is
// unknown to the registry.
#pragma once
#include <cstdint>

#include "sc4/GameDataRegistry.h"

namespace nsISC4GameDataRegistry {

using sc4::EntryId;

namespace detail {
template <class Parent>
EntryId ParentEntry(Parent* parent, sc4::cISC4GameDataRegistry* registry, cIGZUnknown*& parentHandle) {
    EntryId entry = registry->GetRootNodeEntry();
    parentHandle = nullptr;
    if (parent) {
        parent->QueryInterface(1, reinterpret_cast<void**>(&parentHandle));
        entry = registry->GetEntryID(parentHandle);
    }
    return entry;
}
}  // namespace detail

template <class Owner, class Parent>
EntryId AddNode(Owner* owner, const char* name, Parent* parent, int flags, void* context, sc4::cISC4GameDataRegistry* registry) {
    if (!owner || !registry) return 0;
    cIGZUnknown* nodeHandle = nullptr;
    cIGZUnknown* parentHandle = nullptr;
    EntryId parentEntry = registry->GetRootNodeEntry();
    owner->QueryInterface(1, reinterpret_cast<void**>(&nodeHandle));
    if (parent) {
        parent->QueryInterface(1, reinterpret_cast<void**>(&parentHandle));
        parentEntry = registry->GetEntryID(parentHandle);
    }
    const EntryId result = parentEntry ? registry->AddChildEntry(nodeHandle, name, parentEntry, flags, context) : 0;
    if (parentHandle) parentHandle->Release();
    if (nodeHandle) nodeHandle->Release();
    return result;
}

template <class Parent>
EntryId AddValue(void* handle, const char* name, sc4::GameDataValueFn fn, Parent* parent, int flags, void* context, sc4::cISC4GameDataRegistry* registry) {
    if (!registry) return 0;
    cIGZUnknown* parentHandle = nullptr;
    const EntryId parentEntry = detail::ParentEntry(parent, registry, parentHandle);
    const EntryId result = parentEntry ? registry->AddChildEntry(handle, name, fn, parentEntry, flags & ~1, context) : 0;
    if (parentHandle) parentHandle->Release();
    return result;
}

template <class Parent>
EntryId AddValue(void* handle, const char* name, sc4::GameDataCommandFn fn, Parent* parent, int flags, void* context, sc4::cISC4GameDataRegistry* registry) {
    if (!registry) return 0;
    cIGZUnknown* parentHandle = nullptr;
    const EntryId parentEntry = detail::ParentEntry(parent, registry, parentHandle);
    const EntryId result = parentEntry ? registry->AddChildEntry(handle, name, fn, parentEntry, flags | 1, context) : 0;
    if (parentHandle) parentHandle->Release();
    return result;
}

// write == true stores the variant into the game value; false publishes the game value in the variant.
inline bool GenericValueExchnageFuncFloat32(void* value, cIGZVariant& variant, bool write, sc4::cISC4GameDataRegistry*) {
    if (write) return variant.GetValFloat32(*static_cast<float*>(value));
    variant.SetValFloat32(*static_cast<float*>(value));
    return true;
}
inline bool GenericValueExchnageFuncUint32(void* value, cIGZVariant& variant, bool write, sc4::cISC4GameDataRegistry*) {
    if (write) return variant.GetValUint32(*static_cast<std::uint32_t*>(value));
    variant.SetValUint32(*static_cast<std::uint32_t*>(value));
    return true;
}
inline bool GenericValueExchnageFuncUint8(void* value, cIGZVariant& variant, bool write, sc4::cISC4GameDataRegistry*) {
    if (write) return variant.GetValUint8(*static_cast<std::uint8_t*>(value));
    variant.SetValUint8(*static_cast<std::uint8_t*>(value));
    return true;
}

}  // namespace nsISC4GameDataRegistry

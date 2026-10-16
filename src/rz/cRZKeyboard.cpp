#include "rz/cRZKeyboard.h"

#include <cstring>

namespace {
cRZKeyboard::KeyStateFn g_async = nullptr, g_state = nullptr;
}

void cRZKeyboard::SetKeyStateFunctions(KeyStateFn async, KeyStateFn state) {
    g_async = async;
    g_state = state;
}

bool cRZKeyboard::IsKeyDownNowStatic(std::uint32_t key) {
    return g_async && ((static_cast<std::uint32_t>(g_async(key)) & 0x8000) >> 15) != 0;
}

bool cRZKeyboard::IsToggleKeySetStatic(std::uint32_t key) {
    return g_state && (static_cast<std::uint32_t>(g_state(key)) & 1) != 0;
}

void cRZKeyboard::ClearKeyboard() {
    std::memset(down_, 0, sizeof down_);
    pressed_.clear();
}

bool cRZKeyboard::IsKeyDown(std::uint32_t key, std::uint32_t modifiers) {
    return IsKeyDown(key) && GetCurrentModifierState() == modifiers;
}

std::uint32_t cRZKeyboard::GetCurrentModifierState() {
    std::uint32_t state = IsKeyDown(0x10) ? 1u : 0u;
    if (IsKeyDown(0x11)) state += 2;
    if (IsKeyDown(0x12)) state |= 4;
    return state;
}

void cRZKeyboard::ProcessKeyDown(std::uint32_t key) {
    if (down_[key]) return;
    down_[key] = true;
    pressed_.push_back(key);
}

void cRZKeyboard::ProcessKeyUp(std::uint32_t key) {
    if (!down_[key]) return;
    down_[key] = false;
    pressed_.remove(key);
}

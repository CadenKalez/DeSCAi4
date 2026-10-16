#include <cstdio>

#include "rz/cRZKeyboard.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {
std::int16_t Async(std::uint32_t key) { return key == 65 ? static_cast<std::int16_t>(0x8001) : 0; }
std::int16_t State(std::uint32_t key) { return key == 20 ? 1 : 0; }
}  // namespace

int main() {
    cRZKeyboard::SetKeyStateFunctions(&Async, &State);
    cRZKeyboard kb;
    CHECK(!kb.IsKeyDown(65) && kb.PressedKeys().empty());
    kb.ProcessKeyDown(65);
    kb.ProcessKeyDown(65);                                   // repeats are ignored
    kb.ProcessKeyDown(0x10);
    CHECK(kb.IsKeyDown(65) && kb.PressedKeys().size() == 2 && kb.PressedKeys().front() == 65);
    CHECK(kb.GetCurrentModifierState() == 1 && kb.IsKeyDown(65, 1) && !kb.IsKeyDown(65, 0));
    kb.ProcessKeyDown(0x11);
    kb.ProcessKeyDown(0x12);
    CHECK(kb.GetCurrentModifierState() == 7);
    CHECK(kb.CharIsModifier(0x11) && !kb.CharIsModifier(0x13) && !kb.CharIsModifier(0x0f));
    kb.ProcessKeyUp(0x10);
    CHECK(!kb.IsKeyDown(0x10) && kb.PressedKeys().size() == 3);
    kb.ProcessKeyUp(0x10);                                   // already up
    CHECK(kb.IsKeyDownNow(65) && !kb.IsKeyDownNow(66) && kb.IsToggleKeySet(20) && !kb.IsToggleKeySet(21));
    kb.ClearKeyboard();
    CHECK(!kb.IsKeyDown(65) && kb.PressedKeys().empty());
    std::printf(failures ? "FAILED\n" : "rz_keyboard ok\n");
    return failures != 0;
}

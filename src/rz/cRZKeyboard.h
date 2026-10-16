// cRZKeyboard — tracks which virtual keys are down (0x100241b08..0x100241d9c). A plain polymorphic class (no
// reference counting): +8 list of the pressed keys in press order, +0x18 its size, +0x20 a 256-entry down table.
// The asynchronous / toggle key state comes from the Windows-compatibility layer (GetAsyncKeyState / GetKeyState).
#pragma once
#include <cstdint>
#include <list>

class cRZKeyboard {
public:
    // GetAsyncKeyState / GetKeyState of the compatibility layer.
    using KeyStateFn = std::int16_t (*)(std::uint32_t virtualKey);
    static void SetKeyStateFunctions(KeyStateFn async, KeyStateFn state);

    cRZKeyboard() { ClearKeyboard(); }
    virtual ~cRZKeyboard() = default;

    virtual void ClearKeyboard();                                              // [0]
    virtual bool IsKeyDown(std::uint32_t key) { return down_[key]; }           // [1]
    virtual bool IsKeyDown(std::uint32_t key, std::uint32_t modifiers);        // [2]
    virtual bool IsKeyDownNow(std::uint32_t key) { return IsKeyDownNowStatic(key); }   // [3]
    virtual bool CharIsModifier(std::uint32_t key) { return key - 0x10 < 3; }  // [4] shift, control, alt
    virtual std::uint32_t GetCurrentModifierState();                           // [5] 1 shift, 2 control, 4 alt
    virtual bool IsToggleKeySet(std::uint32_t key) { return IsToggleKeySetStatic(key); }   // [6]
    virtual void ProcessKeyDown(std::uint32_t key);                            // [7]
    virtual void ProcessKeyUp(std::uint32_t key);                              // [8]

    static bool IsKeyDownNowStatic(std::uint32_t key);
    static bool IsToggleKeySetStatic(std::uint32_t key);
    const std::list<std::uint32_t>& PressedKeys() const { return pressed_; }

private:
    std::list<std::uint32_t> pressed_;      // +8
    bool down_[256] = {};                   // +0x20
};

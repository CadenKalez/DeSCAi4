// Minimal window manager for window-control tests.
#pragma once
#include <set>
#include <vector>

#include "gz/Win.h"

struct FakeMgr final : cIGZWinMgr {
    std::set<cIGZWin*> valid;
    cIGZWin *focus = nullptr, *capture = nullptr, *mouseWin = nullptr;
    std::vector<cGZMessage> posted;
    int cleanups = 0;
    struct Dialog { std::uint32_t code; cIGZWin* window; std::int64_t result; };
    std::vector<Dialog> dialogs;
    void EndDialog(std::uint32_t code, cIGZWin* window, std::int64_t result) override { dialogs.push_back({code, window, result}); }
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    cIGZWin* GetMainWindow() override { return nullptr; }
    void AppendToPlotStrategy(cIGZWin*) override {}
    void Plot() override {}
    void PostMessageToTarget(cIGZWin*, const cGZMessage& m) override { posted.push_back(m); }
    void AddWindowToValidList(cIGZWin* w) override { valid.insert(w); }
    void RemoveWindowFromValidList(cIGZWin* w) override { valid.erase(w); }
    void CleanUpWindowReferences(cIGZWin* w) override { ++cleanups; if (focus == w) focus = nullptr; if (capture == w) capture = nullptr; }
    bool DestroyWindow(cIGZWin* w) override { if (w->GetParentWin()) return w->GetParentWin()->ChildRemove(w); return true; }
    bool IsWindowValid(cIGZWin* w) override { return valid.count(w) != 0; }
    cIGZWin* GetWindowFromPoint(int, int) override { return nullptr; }
    void SetCurrentMouseWin(cIGZWin* w) override { mouseWin = w; }
    cIGZWin* GZGetCapture() override { return capture; }
    void GZSetCapture(cIGZWin* w) override { capture = w; }
    void GZReleaseCapture(cIGZWin* w) override { if (capture == w) capture = nullptr; }
    cIGZCursorManager* cursors = nullptr;
    cIGZCursorManager* GetCursorManager() override { return cursors; }
    cIGZWin* GZGetFocus() override { return focus; }
    void GZSetFocus(cIGZWin* w) override { focus = w; }
    cIGZKeyboard* GetKeyboard() override { return nullptr; }
};

// A graphics system whose native colour is simply 0xff000000 | rgb.
struct FakeWinGraphics final : IWinGraphics {
    std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { return 0xff000000u | (r << 16) | (g << 8) | b; }
    void NativeToRGB(std::uint32_t n, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override { r = (n >> 16) & 0xff; g = (n >> 8) & 0xff; b = n & 0xff; }
    IWinBuffer* PrimaryBuffer() override { return nullptr; }
    bool DrawSystemActive() override { return false; }
};

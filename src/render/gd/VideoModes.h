// The driver's video-mode list (nSGLDX7::cGDriver::CountVideoModes / VideoModeEnumerator / VideoModeEnumeratorWindowed /
// GetVideoModeInfo). Contents and order are what the game's display-mode UI sees, so they are reproduced exactly:
//  * built once, on the first CountVideoModes() (a non-empty list is never rebuilt);
//  * for each display mode (width, height, depth) two entries: a full-screen one (flags 0x10001) and, when the size is below the
//    maximum window size, a windowed one (flags 0x10000) with the next id; when it does not fit, the full-screen entry is
//    repeated *with the same id* (a quirk of the original);
//  * then a windowed 32-bit entry for each of 12 stock sizes that no display mode provides and that fit the maximum window size;
//  * then, unless the maximum window width is 9999 ("unbounded"), one windowed 32-bit entry of exactly the maximum window size.
#pragma once
#include <cstdint>
#include <vector>

namespace gd {

// sGDMode (0x34 bytes), as handed to callers.
struct sGDMode {
    std::int32_t id;
    std::uint32_t width, height, bitsPerPixel;
    std::uint32_t reserved10[4];
    std::uint32_t flags[2];      // +0x20: byte 0 = full screen, byte 2 = 1 for every entry
    std::uint32_t reserved28[2];
    std::uint32_t reserved30;
};
static_assert(sizeof(sGDMode) == 0x34);

constexpr std::uint32_t kModeFullScreen = 0x10001, kModeWindowed = 0x10000;

class IModeSource {
public:
    virtual ~IModeSource() = default;
    virtual int NumModes() = 0;                                              // MacDisplay::GetNumModes
    virtual void NthMode(int n, int& width, int& height, int& depth) = 0;    // MacDisplay::GetNthMode
    virtual void WindowMaxSizes(int& width, int& height) = 0;                // NCT_GetWindowMaxSizes
};

class cVideoModeList {
public:
    cVideoModeList(IModeSource* source, std::uint32_t adapter = 0xffffffffu) : source_(source), adapter_(adapter) {}
    int CountVideoModes();                                                   // @ 0x10060cd4a
    bool GetVideoModeInfo(std::uint32_t index, sGDMode& out) const;          // @ 0x10060d094 (false = driver error 2)
    const std::vector<sGDMode>& Modes() const { return modes_; }
    // Driver bookkeeping (cGDriver::GetDeviceProperties writes per-mode capability bytes into the current entry).
    sGDMode* MutableMode(std::size_t index) { return index < modes_.size() ? &modes_[index] : nullptr; }
    void Clear() { modes_.clear(); maxWidth_ = maxHeight_ = 0; }

private:
    void AddFullScreen(int width, int height, int depth);                    // VideoModeEnumerator @ 0x10060cf32
    void AddWindowed(int width, int height, int depth);                      // VideoModeEnumeratorWindowed @ 0x10060cc86
    IModeSource* source_;
    std::uint32_t adapter_;
    int maxWidth_ = 0, maxHeight_ = 0;
    std::vector<sGDMode> modes_;
};

}  // namespace gd

// cGZScreenBuffer — the buffer that stands for the screen (77 functions, 0x10062013c..0x100620e84; iids 0x20732180 /
// 0xe3094dc8 for the screen-buffer view, 0x86d72b57 shade). Drawing into it goes through the graphic system's driver:
// Fill clears the rectangle with the driver, Blt hands a buffer to RenderBufferToScreen, Swap presents the whole area.
// A software back buffer (cGZBuffer bits) takes the normal cGZBuffer path.
#pragma once
#include <functional>

#include "gz/Buffer.h"
#include "gz/IGraphicSystemControl.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZPoint.h"

constexpr std::uint32_t kGZIID_cIGZScreenBuffer = 0xe3094dc8;
constexpr std::uint32_t kGZCLSID_cGZScreenBuffer = 0x20732180;

// The screen-buffer view (vtable group 2 of cGZScreenBuffer, slots 3..29).
class cIGZScreenBuffer : public cIGZUnknown {
public:
    virtual cIGZBuffer* AsIGZBuffer() = 0;                                                           // [3]
    virtual bool Init(std::uint32_t width, std::uint32_t height, bool unused, cGZBufferColorType type) = 0;   // [4]
    virtual bool ShutdownScreen() = 0;                                                               // [5]
    virtual bool Restore() = 0;                                                                      // [6]
    virtual bool UninitializeScreen() = 0;                                                           // [7]
    virtual bool IsInitialized() = 0;                                                                // [8]
    virtual void GetAppWindow(cRZRect& out) = 0;                                                     // [9]
    virtual bool ModifyAppWindow(cRZRect& window) = 0;                                               // [10]
    virtual bool IsPageFlipping() = 0;                                                               // [11]
    virtual bool Swap() = 0;                                                                         // [12]
    virtual bool DisableFlipping(int) = 0;                                                           // [13]
    virtual bool DisableComposing(int) = 0;                                                          // [14]
    virtual cIGZBuffer* ActiveDrawBuffer() = 0;                                                      // [15]
    virtual int GetCurrentBackBufferIndex() = 0;                                                     // [16]
    virtual bool SetActiveBackBufferSurface(std::uint32_t) = 0;                                      // [17]
    virtual int GetBufferWidthInPixels() const = 0;                                                  // [18]
    virtual int GetBufferHeightInPixels() const = 0;                                                 // [19]
    virtual void RefreshVRAMPreferenceSetting() = 0;                                                 // [20]
    virtual bool IsReadyScreen() = 0;                                                                // [21]
    virtual bool PaletteChangedNotification() = 0;                                                   // [22]
    virtual bool LockFrontSurface(cIGZBuffer::eLockFlags flags) = 0;                                 // [23]
    virtual bool UnlockFrontSurface(cIGZBuffer::eLockFlags flags) = 0;                               // [24]
    virtual void AdjustClientRect(cRZRect& rect) = 0;                                                // [25]
    virtual void AdjustClientPoint(cRZPoint& point) = 0;                                             // [26]
    virtual void EnableBackgroundPicturePreview() = 0;                                               // [27]
    virtual void DisableBackgroundPicturePreview() = 0;                                              // [28]
    virtual void DumpBufferInformation() = 0;                                                        // [29]
};

class cGZScreenBuffer final : public cGZBuffer, public cIGZScreenBuffer {
public:
    cGZScreenBuffer() = default;                                    // @ 0x10062013c

    bool QueryInterface(std::uint32_t iid, void** out) override;    // @ 0x100620268
    std::uint32_t AddRef() override { return ++refCount_; }         // @ 0x1006202d6
    std::uint32_t Release() override;                               // @ 0x100620300

    void SetGraphicSystem(IGraphicSystemControl* graphics) { graphics_ = graphics; }   // (cGZScreenBuffer::SetGraphicSystem(cIGZGraphicSystem*) @ 0x100620262 itself does nothing)
    // what the original does through the draw system service when Init finishes (registers the render-to-buffer factory
    // and creates the active draw buffer); empty in the port until cGZDrawContextRenderToBuffer is recovered
    std::function<cIGZBuffer*(cGZScreenBuffer&)> createActiveDrawBuffer;

    // --- cGZBuffer overrides
    bool Shutdown() override;                                       // @ 0x1006206aa
    bool Uninitialize() override;                                   // @ 0x100620758
    bool Lock(eLockFlags flags) override;                           // @ 0x1006207da
    bool Unlock(eLockFlags flags) override;                         // @ 0x10062080e
    const cRZRect* GetBufferArea() const override { return &bufferArea_; }   // @ 0x100620c84
    void GetBufferArea(cRZRect& out) const override { out = bufferArea_; }   // @ 0x100620c8e
    bool Fill(std::uint32_t color, const cRZRect& rect, const cRZRect* clip) override;        // @ 0x100620a42
    bool Fill(std::uint32_t color, int l, int t, int r, int b, const cRZRect* clip) override; // @ 0x100620bf2
    bool Fill(std::uint32_t color, const cRZRect* clip) override;                             // @ 0x100620c28
    bool Blt(cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destination, const cRZRect* clip) override;   // @ 0x1006209ee
    bool IsReady() override { return cGZBuffer::IsReady(); }        // @ 0x100620976

    // --- cIGZScreenBuffer
    cIGZBuffer* AsIGZBuffer() override { return this; }             // @ 0x100620d5e
    bool Init(std::uint32_t width, std::uint32_t height, bool unused, cGZBufferColorType type) override;   // @ 0x10062035c
    using cGZBuffer::Init;
    bool ShutdownScreen() override { return Shutdown(); }
    bool Restore() override { return true; }                        // @ 0x100620d68
    bool UninitializeScreen() override { return Uninitialize(); }
    bool IsInitialized() override { return screenInitialized_; }    // @ 0x100620d70
    void GetAppWindow(cRZRect& out) override { out = appWindow_; }  // @ 0x100620d7a
    bool ModifyAppWindow(cRZRect& window) override;                 // @ 0x100620868
    bool IsPageFlipping() override { return false; }                // @ 0x100620d90
    bool Swap() override;                                           // @ 0x10062091e
    bool DisableFlipping(int) override { return false; }            // @ 0x100620d98
    bool DisableComposing(int) override { return false; }           // @ 0x100620da0
    cIGZBuffer* ActiveDrawBuffer() override;                        // @ 0x100620784
    int GetCurrentBackBufferIndex() override { return 0; }          // @ 0x100620dba
    bool SetActiveBackBufferSurface(std::uint32_t) override { return true; }   // @ 0x100620dc2
    int GetBufferWidthInPixels() const override { return appWindow_.right - appWindow_.left; }    // @ 0x100620dca
    int GetBufferHeightInPixels() const override { return appWindow_.bottom - appWindow_.top; }   // @ 0x100620dd6
    void RefreshVRAMPreferenceSetting() override {}                 // @ 0x100620de2
    bool IsReadyScreen() override { return IsReady(); }
    bool PaletteChangedNotification() override { return true; }     // @ 0x100620de8
    bool LockFrontSurface(eLockFlags flags) override { return Lock(flags); }      // @ 0x100620824
    bool UnlockFrontSurface(eLockFlags flags) override { return Unlock(flags); }  // @ 0x100620846
    void AdjustClientRect(cRZRect& rect) override;                  // @ 0x10062098e
    void AdjustClientPoint(cRZPoint& point) override;               // @ 0x1006209ca
    void EnableBackgroundPicturePreview() override {}               // @ 0x100620da8
    void DisableBackgroundPicturePreview() override {}              // @ 0x100620dae
    void DumpBufferInformation() override {}                        // @ 0x100620dba
    bool FloodFill(int, int, std::uint32_t) { return false; }       // @ 0x100620df0

private:
    bool screenInitialized_ = false;                                // +0x68
    cRZRect appWindow_;                                             // +0x6c
    cRZRect bufferArea_;                                            // +0x7c
    IGraphicSystemControl* graphics_ = nullptr;                     // +0x90 / +0x98
    cRZAutoRefCount<cIGZBuffer> overlay_;                           // +0xa0
    std::uint32_t overlayColor_ = 0;                                // +0xa8
    cRZAutoRefCount<cIGZBuffer> activeDrawBuffer_;                  // +0xb0
};

// IGraphicSystemControl — the parts of the graphic system (cIGZGraphicSystem / cIGZGraphicSystem2 of the service 0xc416025c)
// that the application and the screen buffer use; method names follow cGZGraphicSystem's vtable. Until the real
// cGZGraphicSystemW32 is recovered, tests and the boot code provide implementations.
#pragma once
#include <cstdint>

#include "gz/Buffer.h"
#include "render/gd/IGZGDriver.h"

// The graphics system as the application configures it before start-up (cIGZGraphicSystem / cIGZGraphicSystem2 of the
// service 0xc416025c; the method names are those of cGZGraphicSystem's vtable).
class IGraphicSystemControl {
public:
    virtual ~IGraphicSystemControl() = default;
    struct DisplayMetrics { std::uint32_t width = 0, height = 0, depth = 0; };
    virtual DisplayMetrics GetScreenMetrics() = 0;                                    // group 2 slot 10
    virtual void PreInitSetDesiredGameResolution(const DisplayMetrics& metrics) = 0;  // group 2 slot 53
    virtual void PreInitSetWindowedMode(bool windowed) = 0;                           // group 2 slot 55
    virtual void DisableScreenBuffer() = 0;                                           // group 3 slot 14
    virtual void EnableCursorEmulation(bool enable) = 0;                              // group 3 slot 21
    virtual void EnablePageFlipping(bool enable) = 0;                                 // group 3 slot 22
    virtual bool IsPageFlippingEnabled() = 0;                                         // group 3 slot 23
    virtual void SetDefaultDriverClassID(std::uint32_t driverClass) = 0;              // group 3 slot 8
    // used by cSC4App::Init
    virtual bool CanvasReady() = 0;                                                   // canvas vslot 0x28 (GetCanvas() is group 2 slot 15)
    virtual DisplayMetrics GetGameMetrics() = 0;                                      // group 2 slot 11
    virtual gd::cIGZGDriver* GetGDriver() = 0;                                        // group 3 slot 3
    virtual int GetGDriverCount() = 0;                                                // group 3 slot 4
    virtual gd::cIGZGDriver* GetGDriverByIndex(int index) = 0;                        // group 3 slot 5
    virtual void SetGDriver(gd::cIGZGDriver* driver) = 0;                             // group 3 slot 7
    virtual void SetForceNoTranslucency(bool force, float threshold) = 0;             // group 3 slot 18
    // used by cGZScreenBuffer
    virtual cIGZBuffer* CreateBuffer() = 0;                                           // group 2 slot 4
    virtual void RenderBufferToScreen(cIGZBuffer* buffer, const cRZRect& source, const cRZRect& destination) = 0;   // group 3 slot 13
    virtual void LazyFlush() = 0;                                                     // group 3 slot 16
};


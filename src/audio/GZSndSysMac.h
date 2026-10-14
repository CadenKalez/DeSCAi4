// cGZSndSysMac — the platform renderer registered with cGZSndSys: owns the 3D listener the
// positional sounds are heard from. Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable
// 0x100ada010, 0x20-byte object, ctor @ 0x100635e70, Create @ 0x100624fa0; service id 0xe9d27715,
// priority 500000).
//
// Quirks preserved: Pause and Unpause both set the paused flag; SetMasterVolume does nothing and
// GetMasterVolume returns 0; QueryInterface answers the listener interfaces (0x406077d2,
// 0x5b30e7b3) from the global listener created in Init.
#pragma once
#include <cstdint>

#include "audio/GZSnd3D.h"
#include "audio/interfaces/cIGZSndRenderer.h"
#include "gz/cIGZSystemService.h"

class cGZSndSysMac final : public cIGZSystemService, public cIGZSndRenderer {
public:
    static constexpr std::uint32_t kServiceId = 0xe9d27715;
    cGZSndSysMac() = default;                                          // @ 0x100635e70
    static cGZSndSysMac* Create() { return new cGZSndSysMac(); }       // @ 0x100624fa0
    ~cGZSndSysMac() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;       // @ 0x100635ff0
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }   // @ 0x1006360f0
    std::uint32_t Release() override;                                  // @ 0x100636130 (destroys below 2)

    // cIGZSystemService
    std::uint32_t GetServiceID() const override { return serviceId_; }         // @ 0x1006364d0
    void SetServiceID(std::uint32_t id) override { serviceId_ = id; }          // @ 0x1006364f0
    std::int32_t GetServicePriority() const override { return 500000; }       // @ 0x100636510
    bool IsServiceRunning() const override { return true; }                    // @ 0x100636520
    void SetServiceRunning(bool) override {}                                   // @ 0x100636540
    bool Init() override;                                              // @ 0x1006361b0
    bool Shutdown() override;                                          // @ 0x100636320
    bool OnTick(std::uint32_t) override { return false; }              // @ 0x100636420
    bool OnIdle(std::uint32_t) override { return false; }              // @ 0x100636440
    bool OnInstall() { return true; }                          // @ 0x100636560

    // cIGZSndRenderer
    bool SetMasterVolume(std::int32_t) override { return false; }      // @ 0x100636460
    std::int32_t GetMasterVolume() override { return 0; }              // @ 0x1006364a0
    bool Pause(bool) override { paused_ = true; return true; }         // @ 0x100636580
    bool Unpause() override { paused_ = true; return true; }           // @ 0x1006365b0 (sets, not clears)
    bool IsPaused() const override { return paused_; }                 // @ 0x1006365d0

private:
    std::int32_t refCount_ = 0;                                        // +0x10
    std::uint32_t serviceId_ = kServiceId;                             // +0x14
    bool paused_ = false;                                              // +0x18
};

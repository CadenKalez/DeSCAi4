// Four small window procedures.
//  * cGZDummyWinProc (6 functions, 0x10054a388..0x10054a428): swallows the install / remove messages.
//  * cSC4GenericDialogWinProc (6 functions, 0x10030b29e..0x10030bdce): any command from sender 0x287259f6 ends the
//    dialog with the control id as result (window manager EndDialog code 0xf).
//  * cSC4LegendDialogWinProc (6 functions, 0x1002c1bbe..0x1002dbd96): the query-tool legend dialog's buttons; it
//    works on the query view input control (+0x10), reached through ILegendView here.
//  * cSC4CompassWinProc (9 functions, 0x10032ef20..0x10032f212): shows only the compass child that matches the
//    camera's current rotation, refreshed on the rotation messages 0x26d31ec1 / 0xa6b79602.
#pragma once
#include <cstdint>

#include "gz/WinProc.h"
#include "gz/cIGZMessage2.h"
#include "rz/cRZRefCount.h"

namespace sc4 {

// ---- cGZDummyWinProc --------------------------------------------------------------------------------------
class IWinProcDirector {   // RZGetCOMDllDirector(): the owning COM library keeps a count of live procs
public:
    virtual ~IWinProcDirector() = default;
    virtual void AddRef() = 0;       // vslot 8
    virtual void OnProcReleased() = 0;   // vslot 0x50
};
void SetWinProcDirector(IWinProcDirector* director);

class cGZDummyWinProc final : public gz::cIGZWinProc {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override;
    std::uint32_t Release() override;
    bool DoWinProcMessage(cIGZWin*, cGZMessage&) override { return false; }
    // the attach / detach notifications 0xa2bf8ad0 / 0xa2bf8ad1 count as handled
    bool DoWinMsg(cIGZWin*, std::uint32_t type, std::uint64_t, std::uint64_t, std::uint64_t) override {
        return (type & 0xfffffffeu) == 0xa2bf8ad0u;
    }

private:
    std::uint32_t refCount_ = 0;   // +8
};

// ---- cSC4GenericDialogWinProc -----------------------------------------------------------------------------
class cSC4GenericDialogWinProc final : public gz::cIGZWinProc {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) override;

private:
    std::uint32_t refCount_ = 0;
};

// ---- cSC4LegendDialogWinProc ------------------------------------------------------------------------------
class ILegendView {
public:
    virtual ~ILegendView() = default;
    virtual cIGZWin* LegendPanel() = 0;          // +0x70 (shown by 0x4bb92c1f)
    virtual cIGZWin* LegendMinimizedPanel() = 0; // +0x78 (hidden by 0x4bb92c1f)
    virtual void UpdatePaths() = 0;
    virtual void SetLegendMinimized(bool minimized) = 0;
    virtual void SetButtonToggleState(cIGZWin* dialog, std::uint32_t buttonId, bool on) = 0;   // nsGZWinUtils
};

class cSC4LegendDialogWinProc final : public gz::cIGZWinProc {
public:
    explicit cSC4LegendDialogWinProc(ILegendView* view) : view_(view) {}
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) override;

private:
    std::uint32_t refCount_ = 0;
    ILegendView* view_;   // +0x10
};

// ---- cSC4CompassWinProc -----------------------------------------------------------------------------------
class ICompassHost {
public:
    virtual ~ICompassHost() = default;
    virtual int CameraRotation() = 0;                                    // renderer vslot 0x80 -> +0x114
    virtual void AddNotification(cIGZMessageTarget2* target, std::uint32_t message) = 0;
    virtual void RemoveNotification(cIGZMessageTarget2* target, std::uint32_t message) = 0;
};

constexpr std::uint32_t kMessageCameraRotated = 0x26d31ec1, kMessageCameraViewChanged = 0xa6b79602;

class cSC4CompassWinProc final : public cRZRefCount, public gz::cIGZWinProc, public cIGZMessageTarget2 {
public:
    explicit cSC4CompassWinProc(ICompassHost* host) : host_(host) {}

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { cRZRefCount::AddRef(); return RefCount(); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(cRZRefCount::Release()); }
    bool DoWinProcMessage(cIGZWin* window, cGZMessage& message) override;
    bool DoMessage(cIGZMessage2* message) override;
    void UpdateRotation();

private:
    ICompassHost* host_;
    cIGZWin* window_ = nullptr;   // +0x20
};

}  // namespace sc4

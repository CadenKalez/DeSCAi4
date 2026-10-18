// Small framework objects.
//  * cGZClipboardManagerClipboardData (7 functions, 0x1000642bc..0x10006437e, iid 0x07ec0006): a typed value on the
//    clipboard (+0xc data type, +0x10 the held object, reference counted at +8).
//  * cGZWinSpyMessageFilter (9 functions, 0x10069aa20..0x10069abb0, iid 0xc6ae7085): hooks a window's message
//    filter list and reports every message to a spy target without consuming it.
//  * cGZWinDlg (10 functions, 0x1005228f0..0x100522b04, view iid 0xc12e57e2): the base of dialog windows - key
//    presses are routed to the child whose key equivalent matches, GZEndDialog tells the window manager.
#pragma once
#include <cstdint>

#include "gz/Win.h"
#include "rz/cRZRefCount.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZClipboardData = 0x07ec0006;
constexpr std::uint32_t kGZIID_cIGZWinSpyMessageFilter = 0xc6ae7085;
constexpr std::uint32_t kGZIID_cIGZWinDlg = 0xc12e57e2;
constexpr std::uint32_t kDialogEndCode = 0xf;

class cIGZClipboardData : public cIGZUnknown {
public:
    virtual std::uint32_t GetDataType() const = 0;
    virtual cIGZUnknown* GetData() = 0;
    virtual bool GetData(std::uint32_t iid, void** out) = 0;
};

class cGZClipboardManagerClipboardData final : public cIGZClipboardData {
public:
    cGZClipboardManagerClipboardData(std::uint32_t dataType, cIGZUnknown* data) : type_(dataType), data_(data) {}
    ~cGZClipboardManagerClipboardData() { if (data_) data_->Release(); }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override;
    std::uint32_t GetDataType() const override { return type_; }
    cIGZUnknown* GetData() override { return data_; }
    bool GetData(std::uint32_t iid, void** out) override { return data_ ? data_->QueryInterface(iid, out) : false; }

private:
    std::uint32_t refCount_ = 0;      // +8
    std::uint32_t type_ = 0;          // +0xc
    cIGZUnknown* data_ = nullptr;     // +0x10 (owned reference)
};

// What the spy reports to (vslot 0 of cIGZWinSpyTarget).
class cIGZWinSpyTarget {
public:
    virtual ~cIGZWinSpyTarget() = default;
    virtual void OnSpyMessage(cIGZWin* window, cGZMessage& message) = 0;
};

class cGZWinSpyMessageFilter final : public cRZRefCount, public cIGZWinMessageFilter {
public:
    cGZWinSpyMessageFilter(cIGZWin* window, cIGZWinSpyTarget* target);
    ~cGZWinSpyMessageFilter() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { cRZRefCount::AddRef(); return RefCount(); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(cRZRefCount::Release()); }
    void Detach();
    bool DoMessage(cIGZWin* window, cGZMessage& message) override;

private:
    cIGZWin* window_;               // +0x18
    cIGZWinSpyTarget* target_;      // +0x20
};

class cIGZWinDlg : public cIGZUnknown {
public:
    virtual void GZEndDialog(std::uint64_t result) = 0;
};

class cGZWinDlg : public cGZWin, public cIGZWinDlg {
public:
    explicit cGZWinDlg(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    ~cGZWinDlg() override { cGZWin::Shutdown(); }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnCharacter(char) override { return true; }
    bool GZOnKeyUp(std::uint32_t, std::uint32_t) override { return true; }
    void GZEndDialog(std::uint64_t result) override;
};

}  // namespace gz

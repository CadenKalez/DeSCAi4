// cGZCOMLibrary (12 functions, 0x10003b430..0x10003c4be, iid 0x07d4922e): a COM library listed by the framework -
// its file path, whether it is loaded, the director it exposes and the module handle. Layout: +8 loaded, +0xc
// refcount, +0x10 director, +0x18 path (cRZString), +0x40 handle. The ordering used by the library list is
// plain string order on the path. Load() is a stub on this platform: it converts the path to the system code page
// and then reports failure (the Mac build links its COM directors statically).
#pragma once
#include <cstdint>

#include "gz/cIGZString.h"
#include "gz/cIGZUnknown.h"
#include "rz/cRZString.h"
#include "rz/cRZCOMDllDirector.h"

constexpr std::uint32_t kGZIID_cIGZCOMLibrary = 0x07d4922e;


class cGZCOMLibrary final : public cIGZUnknown {
public:
    explicit cGZCOMLibrary(const cIGZString& path) : path_(path) {}
    ~cGZCOMLibrary() = default;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refCount_); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refCount_); }   // owned by the library list: never self-deletes

    bool Load();
    void GetPath(cIGZString& out) const { out.Copy(path_); }
    cIGZCOMDirector* GetDirector() const { return director_; }
    void* GetHandle() const { return handle_; }
    bool IsLoaded() const { return loaded_; }
    bool operator<(const cGZCOMLibrary& other) const;
    // library bookkeeping the COM registry does when it unloads (and that a test loader does when it loads)
    void MarkUnloaded() { loaded_ = false; director_ = nullptr; handle_ = nullptr; }
    void MarkLoaded(cIGZCOMDirector* director, void* handle) { loaded_ = true; director_ = director; handle_ = handle; }

private:
    bool loaded_ = false;                 // +8
    int refCount_ = 0;                    // +0xc
    cIGZCOMDirector* director_ = nullptr; // +0x10
    cRZString path_;                      // +0x18
    void* handle_ = nullptr;              // +0x40
};

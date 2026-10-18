// The mouse cursor classes: cRZCursor (21 functions at 0x1000572a0..0x100058798), the stock Windows-95 style system
// cursor cRZSystemCursorW95 (4 functions at 0x1000587b0..0x100058814) and the manager that owns them by id,
// cRZCursorManager (21 functions at 0x1000580ae..0x10005866a, iid 0xa34c498a).
//
// A cursor is either a system cursor (a handle: the Mac port maps handle - 1 to a theme cursor), a named cursor
// registered with the OS (supported once a .cur file has been converted), or a plain buffer the graphics system shows as a
// hardware cursor. Loading a .cur file understands the 32x32 icon / cursor directory entry, 1, 4, 8 and 32 bit images and
// the AND mask; masked pixels get the magenta colour key.
// Quirks kept: an image with a colour count of 8 skips the colour pass entirely; a 32 bit image first swallows one "palette"
// entry (1 << (32 & 31) == 1) so its pixels start one word late; the mask pass stops at the end of the file.
// Deviation: SetCursor(cursor) / PushAutoCursor with no cursor do nothing instead of dereferencing null.
#pragma once
#include <cstdint>
#include <deque>
#include <map>

#include "gz/Buffer.h"
#include "gz/WinInterfaces.h"
#include "rz/cRZString.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZCursor = 0x834c471e, kGZIID_cIGZCursorManager = 0xa34c498a;

// What the cursor code asks of the platform and the graphics system.
class ICursorPlatform {
public:
    virtual ~ICursorPlatform() = default;
    virtual cIGZBuffer* CreateBuffer() = 0;                                   // graphics system vslot 0x20 (a counted reference)
    virtual void SetHardwareCursor(cIGZBuffer* buffer, int hotX, int hotY) = 0;   // graphics system vslot 0xf8; the platform raises
                                                                                 // SetHardwareCursorApplied() when it takes the buffer
    virtual void SetSystemCursor(int index) = 0;                              // ASLCocoa_SetSystemCursor
    virtual void SetNamedCursor(const char* name) = 0;                        // MacSetNamedCursor
    virtual void AddNamedCursor(const char* name, const std::uint32_t* argb, int width, int height, int stride, double hotX, double hotY) = 0;   // MacAddNamedCursor
    virtual void ShowCursor() = 0;
    virtual void HideCursor() = 0;
    virtual bool HasGraphicsSystem() { return true; }                         // RZGetFramework() != null
};
ICursorPlatform* CursorPlatform();
void SetCursorPlatform(ICursorPlatform* platform);
// gMacHardwardCursorSet: true while a hardware / named / system cursor is in force.
extern bool gMacHardwareCursorSet;

class cRZCursor : public cIGZCursor {
public:
    cRZCursor();                                                 // @ 0x1000572a0
    explicit cRZCursor(const cIGZString& path);                  // @ 0x100057346 (loads the .cur file)
    ~cRZCursor();                                                // @ 0x10005748a

    bool QueryInterface(std::uint32_t iid, void** out) override;                // @ 0x100057558
    std::uint32_t AddRef() override { return ++refCount_; }                     // @ 0x10005752e
    std::uint32_t Release() override;                                           // @ 0x10005753c
    bool Init() override { return true; }                                       // @ 0x10005757a
    bool Shutdown() override { return true; }                                   // @ 0x100057582
    void SetCursor(bool apply) override;                                        // @ 0x10005758a
    void ShowCursor(bool show) override;                                        // @ 0x10005762e
    void GetCursorHotSpot(int& x, int& y) override { x = hotX_; y = hotY_; }    // @ 0x100057644
    void SetCursorHotSpot(int x, int y) override { hotX_ = static_cast<std::int16_t>(x); hotY_ = static_cast<std::int16_t>(y); }   // @ 0x100057656
    bool LoadCursorA(const cIGZString& path) override;                          // @ 0x100057664
    void SetCursorHandle(void* handle) override { if (handle_ != handle) handle_ = handle; }   // @ 0x100057fc6
    void SetCursorBuffer(cIGZBuffer* buffer) override;                          // @ 0x100057fd6
    bool SystemHasCursorSupport() override { return supportsCursor_; }          // @ 0x100058012
    cIGZBuffer* GetCursorBuffer() override { return buffer_; }                  // @ 0x10005878e
    bool IsLoaded() override { return buffer_ || handle_; }                     // @ 0x100058798

protected:
    std::uint32_t refCount_ = 0;                // +8
    bool supportsCursor_ = false;               // +0xc
    cRZString name_;                            // +0x18 ("<n>-SC4")
    cIGZBuffer* buffer_ = nullptr;              // +0x40
    std::int16_t hotX_ = 0, hotY_ = 0;          // +0x48, +0x4a
    void* handle_ = nullptr;                    // +0x50
    static inline int sID = 0;
};

class cRZSystemCursorW95 final : public cRZCursor {
public:
    explicit cRZSystemCursorW95(std::intptr_t systemCursor = 0) { handle_ = reinterpret_cast<void*>(systemCursor); }
    ~cRZSystemCursorW95() { handle_ = nullptr; }                                 // @ 0x1000587b0
    bool LoadCursorA(const cIGZString&) override { return false; }               // @ 0x10005880c
    void SetCursorHandle(void* handle) override { handle_ = handle; }            // @ 0x100058814
};

class cRZCursorManager final : public cIGZCursorManager {
public:
    cRZCursorManager() = default;                                                // @ 0x1000580ae
    ~cRZCursorManager();                                                         // @ 0x1000580f8

    bool QueryInterface(std::uint32_t iid, void** out) override;                 // @ 0x100058496
    std::uint32_t AddRef() override { return ++refCount_; }                      // @ 0x100058468
    std::uint32_t Release() override;                                            // @ 0x100058476
    bool Init() override;                                                        // @ 0x10005817c
    bool Shutdown() override;                                                    // @ 0x1000582ca
    bool AddCursor(cIGZCursor* cursor, std::uint32_t id) override;               // @ 0x10005866a
    bool RemoveCursor(std::uint32_t id) override;                                // @ 0x100058408
    cIGZCursor* LoadCursor(std::uint32_t id) override { return GetCursorByID(id); }   // @ 0x100058620 (GetCursor(id))
    cIGZCursor* GetCursor() override { return current_; }                        // @ 0x100058660
    bool SetCursor(std::uint32_t id) override;                                   // @ 0x1000584b8
    void SetCursor(cIGZCursor* cursor) override;                                 // @ 0x10005851a
    bool PushAutoCursor(std::uint32_t id) override;                              // @ 0x100058566
    bool PopAutoCursor() override;                                               // @ 0x1000585c2
    void AddCursor(const cIGZString& path, std::uint32_t id) override;           // @ 0x1000583ae
    void ShowCursor() override;                                                  // @ 0x1000583a4
    void HideCursor() override;                                                  // @ 0x10005839a

    cIGZCursor* GetCursorByID(std::uint32_t id) const;
    void release_all_cursors();                                                  // @ 0x100058312

private:
    bool initialised_ = false;                          // +8
    std::uint32_t refCount_ = 0;                        // +0xc
    std::map<std::uint32_t, cIGZCursor*> cursors_;      // +0x10 (a reference each)
    std::deque<cIGZCursor*> autoStack_;                 // +0x28 (a reference each)
    cIGZCursor* current_ = nullptr;                     // +0x58 (a reference)
};

}  // namespace gz

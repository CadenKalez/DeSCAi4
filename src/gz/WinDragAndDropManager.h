// cGZWinDragAndDropManager — the window drag-and-drop service (33 functions, 0x1007b10d0..0x1007b28f0). It is a
// system service (id 0x0c0874c6, priority 0x1312cf), a window message filter and a cGZWin (the drag image window).
// Windows that can start a drag register themselves as "drop source proxies"; the manager then watches their mouse
// messages, builds a DragData on a left mouse-down, tracks the pointer, finds the window under the pointer as the
// drop target (by walking the source window's parent chain and enumerating each level's children) and sends the
// drag enter / leave / over / begin / drop / cancel messages to the source and the target.
//
// Layout (0x1b0 bytes): +0 cIGZWinDragAndDropManager (iid 0x4c0874d7), +8 cIGZWinMessageFilter (0xc6ae7085),
// +0x10 cRZSystemService (cIGZSystemService vptr at +0x10, cRZUnknown vptr + refcount at +0x18/+0x20, service id
// +0x24, priority +0x28, tick priority +0x2c, running +0x30), +0x38 cGZWin (0x150 bytes: the drag image window),
// +0x188 initialised, +0x189 the source window's kWinPrivateBuffer flag saved while the drag runs, +0x190 the
// DragData (0x60 bytes, created in Init), +0x198 the drop source proxies (libc++ std::list: next +0, prev +8,
// size +0x10).
//
// Drag image buffers: the original holds cIGZBuffer* at DragData +0x40/+0x48 and creates the image with
// GZCOM()->CreateInstance(0xc470d325, 0x20732180) + Init(w, h, ARGB8888). In this port a window's buffer is the
// window-layer seam IWinBuffer (gz/Win.h) and the image comes from WinGraphics()->GraphicsCreateBuffer(), i.e. the
// same factory cGZWin's private buffers use; see the DND-IMAGE note in the ledger evidence.
//
// Window manager access: the original re-fetches the window manager service (0xa417445e, 0x5a4) from RZGetFramework
// in every message send; SendMessageA here uses the manager the window was created with (cGZWin::mgr_), the same
// object under the game's single-manager invariant.
#pragma once
#include <cstdint>
#include <list>

#include "gz/Win.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZSystemService.h"
#include "rz/cRZVariant.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinDragAndDropManager = 0x4c0874d7;
constexpr std::uint32_t kGZIID_cIGZWinMessageFilter = 0xc6ae7085;   // the +8 view (cIGZWinMessageFilter)
constexpr std::uint32_t kGZSID_cGZWinDragAndDropManager = 0x0c0874c6;
constexpr std::int32_t kGZWinDragAndDropServicePriority = 0x1312cf;

// The messages the manager sends while a drag runs. They all carry the same payload (DragMessage in the .cpp):
// param1 = the DragData*, param2 = the drag x and param3 = the drag y, each zero-extended from 32 bits
// (cGZMessage +8/+0x10/+0x18; the original writes DragData +0x38 as one qword through PMOVZXDQ). None of them
// carries the drag image - a receiver gets it from the DragData* (image at +0x40).
constexpr std::uint32_t kDragMsgBegin = 0xac0fe76d;    // the deferred drag left its 4-pixel threshold: start it
constexpr std::uint32_t kDragMsgEnter = 0xac085f43;    // the pointer entered a new drop target
constexpr std::uint32_t kDragMsgLeave = 0x6c085f45;    // ... and left the previous one
constexpr std::uint32_t kDragMsgOver = 0xac085f47;     // the pointer moved inside the target
constexpr std::uint32_t kDragMsgDrop = 0x8c085f48;     // Drop()
constexpr std::uint32_t kDragMsgCancel = 0x8c0a1fcc;   // CancelDrag()

// DragData::type / DragData::magic. DoMessage turns a mouse-down on a drop source proxy into a window drag
// (type 0xcc0cae33, magic 0xac0c7f4a); a right-button drag elsewhere uses magic 0x0c0c7f51.
constexpr std::uint32_t kDragTypeWindow = 0xcc0cae33;
constexpr std::uint32_t kDragMagicLeft = 0xac0c7f4a;
constexpr std::uint32_t kDragMagicRight = 0x0c0c7f51;

class cIGZWinDragAndDropManager : public cIGZUnknown {
public:
    // The drag in progress (0x60 bytes in the original, 0x68 here). The original's destructor (0x1007b29a2)
    // releases the image / target / source pointers in that order and then destroys the variant, which is what
    // member destruction does here. Offsets are given for this port with the original's in parentheses: the
    // recovered cRZVariant is 0x20 bytes (vptr + 0x18), the binary's is 0x18, so everything after the payload sits
    // 8 bytes later than in the original.
    struct DragData {
        std::uint32_t type = 0;                     // +0x00 (0x00) 0 when there is no drag, else an id (kDragType*)
        cRZVariant value;                           // +0x08 (0x08, 0x18 bytes there) payload; DoMessage stores the
                                                    //   source window in it
        cRZAutoRefCount<cIGZWin> source;            // +0x28 (0x20) window the drag started from
        int startX = 0;                             // +0x30 (0x28) drag start, source window coordinates
        int startY = 0;                             // +0x34 (0x2c)
        cRZAutoRefCount<cIGZWin> target;            // +0x38 (0x30) window under the pointer (found in GZOnMouseMove)
        int x = 0;                                  // +0x40 (0x38) pointer, from the last mouse move
        int y = 0;                                  // +0x44 (0x3c)
        cRZAutoRefCount<IWinBuffer> image;          // +0x48 (0x40) drag image (cIGZBuffer* in the original)
        cRZAutoRefCount<IWinBuffer> imageCopy;      // +0x50 (0x48) second slot (the original stores the same buffer)
        int imageMode = 0;                          // +0x58 (0x50) 1 = GZPaint draws imageCopy, else image
        bool cancelOnMouseUp = false;               // +0x5c (0x54) the drag has not left its start cell yet
        std::uint32_t magic = 0;                    // +0x60 (0x58) kDragMagicLeft / kDragMagicRight
    };

    virtual bool Init() = 0;                                                        // [3] 0x28
    virtual bool Shutdown() = 0;                                                    // [4] 0x30
    // Copies `data` into the manager's DragData (cancelling the drag in progress first). `cancelOnMouseUp` leaves
    // the drag pending until the pointer leaves its start cell and skips AddWindow. 0x1007b17a2
    virtual bool StartDrag(DragData* data, bool cancelOnMouseUp) = 0;               // [5] 0x38
    virtual bool CancelDrag() = 0;                                                  // [6] 0x40 0x1007b19ec
    virtual bool Drop() = 0;                                                        // [7] 0x48 0x1007b1b28
    virtual bool IsDragging() const = 0;                                            // [8] 0x50 0x1007b1b96
    virtual DragData* GetDragData() = 0;                                            // [9] 0x58 0x1007b1bb2
    virtual void SetDragData(DragData* data) = 0;                                   // [10] 0x60 0x1007b1bc0
    // Fills `out` with a new DragData (type, payload, source, magic, start position, images). 0x1007b1c9c;
    // (magic, startX, startY, image, imageCopy) is the original's tail argument list.
    virtual void CreateDragData(DragData* out, std::uint32_t type, cRZVariant& value, cIGZWin* source,
                                int magic, int startX, int startY, IWinBuffer* image,
                                IWinBuffer* imageCopy) = 0;                         // [11] 0x68
    // Registers (or unregisters) `window` as a drag source: the manager adds itself as one of the window's message
    // filters. Quirk kept: it answers false when it did add the window, true when it was already there. 0x1007b1d92
    virtual bool AddDropSourceProxy(cIGZWin* window, bool add) = 0;                 // [12] 0x70
    // Same, for the child `id` of `window` (or of this window's parent when `window` is null; the lookup is
    // recursive). 0x1007b1f24
    virtual bool AddDropSourceProxy(std::uint32_t id, cIGZWin* window, bool add) = 0;   // [13] 0x78
};

class cGZWinDragAndDropManager : public cIGZWinDragAndDropManager, public cIGZWinMessageFilter,
                                 public cRZSystemService, public cGZWin {
public:
    explicit cGZWinDragAndDropManager(cIGZWinMgr* mgr);                             // 0x1007b10d0 / 0x1007b11cc
    // Quirk kept: the original destructor (0x1007b11d6) only clears the proxy list and never frees the DragData
    // that Init allocated - Shutdown() does. Every test pairs Init with Shutdown so nothing leaks.
    ~cGZWinDragAndDropManager() override;                                          // 0x1007b1276 / 0x1007b12aa

    // One override serves the cIGZWinDragAndDropManager, cIGZWinMessageFilter, cRZSystemService and cGZWin views
    // (the binary has a thunk per base). 0x1007b1386
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cRZSystemService::AddRef(); }          // 0x1007b1302
    std::uint32_t Release() override { return cRZSystemService::Release(); }        // 0x1007b1344
    bool Init() override;                                                          // 0x1007b140e
    bool Shutdown() override;                                                      // 0x1007b150c
    // cIGZWinMessageFilter: the manager only reacts to messages from a registered drop source proxy, and it always
    // answers false so the window still gets its own mouse handling. 0x1007b15a0
    bool DoMessage(cIGZWin* window, cGZMessage& message) override;
    using cGZWin::DoMessage;   // the window's own DoMessage(cGZMessage&) stays reachable (it is in the +0x38 vtable)

    bool StartDrag(DragData* data, bool cancelOnMouseUp) override;
    bool CancelDrag() override;
    bool Drop() override;
    bool IsDragging() const override;
    DragData* GetDragData() override { return dragData_; }
    void SetDragData(DragData* data) override;
    void CreateDragData(DragData* out, std::uint32_t type, cRZVariant& value, cIGZWin* source, int magic,
                        int startX, int startY, IWinBuffer* image, IWinBuffer* imageCopy) override;
    bool AddDropSourceProxy(cIGZWin* window, bool add) override;
    bool AddDropSourceProxy(std::uint32_t id, cIGZWin* window, bool add) override;

    bool GZOnCaptureChanged(cIGZWin* lost, cIGZWin* gained) override;                // 0x1007b1f8e
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;          // 0x1007b1ff2
    bool GZOnMouseWheel(int x, int y, std::uint32_t flags, int delta) override;     // 0x1007b2052
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;                // 0x1007b206a
    bool GZOnMouseDownR(int x, int y, std::uint32_t flags) override;                // 0x1007b20be
    bool GZOnMouseUpL(int x, int y, std::uint32_t flags) override;                  // 0x1007b2112
    bool GZOnMouseUpR(int x, int y, std::uint32_t flags) override;                  // 0x1007b219a
    bool GZOnMouseMove(int x, int y, std::uint32_t flags) override;                 // 0x1007b2222
    bool GZPaint() override;                                                       // 0x1007b26fe

private:
    // Puts the drag image window at the drag start point, sized to the image (16x16 before there is one) and adds
    // it as a child of the drag source's parent. 0x1007b18f0
    void AddWindow();
    // type = 0 and the refcounted pointers cleared; the DragData object itself stays. 0x1007b1a8c
    void ClearDragData();
    // Copies the source window's private buffer into a fresh image buffer (once per drag). 0x1007b24d8
    void EndCaptureSourceWindowImage();
    // Delivers `message` to both windows when the manager considers them valid (see the header note on the window
    // manager lookup). 0x1007b27cc
    void SendMessageA(cIGZWin* primary, cIGZWin* secondary, cGZMessage& message);
    // cGZWin::EnumChildren callback that picks the drop target (first window under the pointer that is visible, may
    // take the focus and ignores no cursor) and recurses into it. 0x1007b0fd0
    static bool SetDestWin(cIGZWin* parent, std::uint32_t id, void* child, void* context);

    bool initialised_ = false;                                // +0x188
    bool oldPrivateBufferFlag_ = false;                       // +0x189
    DragData* dragData_ = nullptr;                            // +0x190
    std::list<cRZAutoRefCount<cIGZWin>> dropSources_;         // +0x198
};

}  // namespace gz

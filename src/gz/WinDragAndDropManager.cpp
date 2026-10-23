#include "gz/WinDragAndDropManager.h"

#include "gz/DrawContext.h"
#include "gz/WinMgrBase.h"   // kWinFlagCanFocus (0x8000), the drop-target test

namespace gz {
namespace {

// cIGZWin flag 0x40000: Init sets it on the drag image window together with kWinPrivateBufferTransparent and
// kWinFlag4. Its meaning is not recovered (no other recovered class reads it); kept as the original's literal.
constexpr std::uint32_t kWinFlag40000 = 0x40000;

int AbsInt(int value) { return value < 0 ? -value : value; }

// Every drag notification the manager builds has the same payload: param1 = DragData*, param2 = the drag x and param3
// = the drag y (no image: the receiver finds it in the DragData at +0x40). The original loads DragData +0x38 as one
// qword and zero-extends both 32-bit halves into the two 64-bit parameters with PMOVZXDQ, i.e. param2 = (uint64)x and
// param3 = (uint64)y (see 0x1007b1b5e in Drop, 0x1007b1a3c in CancelDrag, 0x1007b22cc/0x1007b243d in GZOnMouseMove).
cGZMessage DragMessage(std::uint32_t type, const cIGZWinDragAndDropManager::DragData& data) {
    cGZMessage message;
    message.type = type;
    message.param1 = reinterpret_cast<std::uintptr_t>(&data);
    message.param2 = static_cast<std::uint32_t>(data.x);
    message.param3 = static_cast<std::uint32_t>(data.y);
    return message;
}

// cIGZBuffer::Width() / Height() in the original; the seam exposes the buffer's rectangle instead.
int BufferWidth(const IWinBuffer& buffer) { return buffer.GetBounds()->right - buffer.GetBounds()->left; }
int BufferHeight(const IWinBuffer& buffer) { return buffer.GetBounds()->bottom - buffer.GetBounds()->top; }

}  // namespace

cGZWinDragAndDropManager::cGZWinDragAndDropManager(cIGZWinMgr* mgr)   // @ 0x1007b10d0
    : cRZSystemService(kGZSID_cGZWinDragAndDropManager, kGZWinDragAndDropServicePriority,
                       kGZWinDragAndDropServicePriority),
      cGZWin(mgr) {}   // the original's cGZWin() takes no manager (it reaches the framework itself); the recovered base
                       // takes one, so the manager's creator passes the window manager it was built with

cGZWinDragAndDropManager::~cGZWinDragAndDropManager() {   // @ 0x1007b11d6 (body) / 0x1007b12aa (deleting)
    // Quirk kept: the DragData is only freed by Shutdown() in the original; make sure it ran before this point.
    dropSources_.clear();
}

bool cGZWinDragAndDropManager::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1007b1386
    void* view = nullptr;
    if (iid == kGZIID_cIGZWinMessageFilter) view = static_cast<cIGZWinMessageFilter*>(this);
    else if (iid == kGZIID_cIGZWin) view = static_cast<cGZWin*>(this);
    else if (iid == kGZIID_cIGZSystemService) view = static_cast<cRZSystemService*>(this);
    else if (iid == kGZIID_cIGZWinDragAndDropManager) view = this;
    else return false;
    *out = view;
    AddRef();
    return true;
}

bool cGZWinDragAndDropManager::Init() {   // @ 0x1007b140e
    if (!initialised_) {
        initialised_ = true;
        if (!dragData_) {
            dragData_ = new DragData;
            dragData_->magic = kDragMagicLeft;   // the original pre-fills the persistent DragData with this magic
        }
    }
    SetFlag(kWinFlag40000, true);
    SetFlag(kWinPrivateBufferTransparent, true);
    SetFlag(kWinFlag4, true);
    return cGZWin::Init();
}

bool cGZWinDragAndDropManager::Shutdown() {   // @ 0x1007b150c
    if (!initialised_) return false;
    initialised_ = false;
    if (dragData_) {
        delete dragData_;
        dragData_ = nullptr;
    }
    dropSources_.clear();
    return cGZWin::Shutdown();
}

// A window message filter: only a registered drop source proxy gets the drag handling, and the message is always
// passed on to the window (return false).
bool cGZWinDragAndDropManager::DoMessage(cIGZWin* window, cGZMessage& message) {   // @ 0x1007b15a0
    if (window) window->AddRef();
    bool known = false;
    for (auto it = dropSources_.rbegin(); it != dropSources_.rend(); ++it) {   // the original walks the list backwards
        if (it->get() == window) {
            known = true;
            break;
        }
    }
    if (known) {
        switch (message.type) {
            case kGZMsgKeyDown:   // any key cancels a drag in progress
                if (IsDragging()) CancelDrag();
                break;
            case kGZMsgMouseDownL: {   // start a window drag at the mouse-down point; deferred until the mouse moves
                cRZVariant payload(static_cast<cIGZUnknown*>(window));
                DragData data;
                data.magic = kDragMagicLeft;
                CreateDragData(&data, kDragTypeWindow, payload, window, kDragMagicLeft,
                               static_cast<int>(std::uint32_t(message.param1)),
                               static_cast<int>(std::uint32_t(message.param2)), nullptr, nullptr);
                StartDrag(&data, true);
                break;
            }
            case kGZMsgMouseUpL:   // a click without a drag: cancel
                CancelDrag();
                break;
            case kGZMsgMouseMove:
                if (IsDragging()) GZSetCapture();
                break;
            default:
                break;
        }
    }
    if (window) window->Release();
    return false;
}

bool cGZWinDragAndDropManager::StartDrag(DragData* data, bool cancelOnMouseUp) {   // @ 0x1007b17a2
    if (!initialised_) return false;
    if (IsDragging()) CancelDrag();
    if (data && data->type != 0) {
        DragData* drag = dragData_;
        drag->type = data->type;
        drag->value = data->value;
        if (drag != data) {
            drag->source = data->source;
            drag->startX = data->startX;
            drag->startY = data->startY;
            drag->target = data->target;
        }
        drag->x = data->x;
        drag->y = data->y;
        drag->image = data->image;
        drag->imageCopy = data->imageCopy;
        drag->magic = data->magic;
        drag->imageMode = data->imageMode;
        drag->cancelOnMouseUp = cancelOnMouseUp;
        if (!cancelOnMouseUp) AddWindow();
    }
    return true;
}

bool cGZWinDragAndDropManager::CancelDrag() {   // @ 0x1007b19ec
    if (!initialised_) return false;
    if (IsDragging() && dragData_) {
        if (!dragData_->cancelOnMouseUp && (dragData_->source || dragData_->target)) {
            cGZMessage message = DragMessage(kDragMsgCancel, *dragData_);
            SendMessageA(dragData_->target.get(), dragData_->source.get(), message);
        }
        ClearDragData();
    }
    if (GZAmCapturing()) GZReleaseCapture();
    return true;
}

void cGZWinDragAndDropManager::ClearDragData() {   // @ 0x1007b1a8c
    dragData_->type = 0;
    dragData_->source = nullptr;
    dragData_->target = nullptr;
    dragData_->image = nullptr;
    dragData_->imageCopy = nullptr;
}

bool cGZWinDragAndDropManager::Drop() {   // @ 0x1007b1b28
    if (!initialised_) return false;
    if (dragData_) {
        if (dragData_->source || dragData_->target) {
            cGZMessage message = DragMessage(kDragMsgDrop, *dragData_);
            SendMessageA(dragData_->target.get(), dragData_->source.get(), message);
        }
        ClearDragData();
    }
    return true;
}

bool cGZWinDragAndDropManager::IsDragging() const {   // @ 0x1007b1b96
    return dragData_ != nullptr && dragData_->type != 0;
}

void cGZWinDragAndDropManager::SetDragData(DragData* data) {   // @ 0x1007b1bc0
    DragData* drag = dragData_;
    if (!drag) return;
    drag->type = data->type;
    drag->value = data->value;
    if (drag != data) {
        drag->source = data->source;
        drag->startX = data->startX;
        drag->startY = data->startY;
        drag->target = data->target;
    }
    drag->x = data->x;
    drag->y = data->y;
    drag->image = data->image;
    drag->imageCopy = data->imageCopy;
    drag->imageMode = data->imageMode;
    drag->cancelOnMouseUp = data->cancelOnMouseUp;
    drag->magic = data->magic;
}

void cGZWinDragAndDropManager::CreateDragData(DragData* out, std::uint32_t type, cRZVariant& value,
                                              cIGZWin* source, int magic, int startX, int startY,
                                              IWinBuffer* image, IWinBuffer* imageCopy) {   // @ 0x1007b1c9c
    out->type = type;
    out->value = value;
    out->source = source;
    out->magic = magic;
    out->startX = startX;
    out->startY = startY;
    out->target = nullptr;
    out->x = 0;
    out->y = 0;
    out->image = image;
    out->imageCopy = imageCopy;
    out->imageMode = 0;
    out->cancelOnMouseUp = false;
}

// Quirk kept: this answers false when it registered the window and true when there was nothing to do.
bool cGZWinDragAndDropManager::AddDropSourceProxy(cIGZWin* window, bool add) {   // @ 0x1007b1d92
    cRZAutoRefCount<cIGZWin> hold(window);
    auto found = dropSources_.end();
    for (auto it = dropSources_.begin(); it != dropSources_.end(); ++it) {
        if (it->get() == window) {
            found = it;
            break;
        }
    }
    bool result = true;
    if (add) {
        if (found == dropSources_.end()) {
            window->AddMessageFilter(static_cast<cIGZWinMessageFilter*>(this));
            dropSources_.push_back(hold);
            result = false;
        }
    } else if (found != dropSources_.end()) {
        (*found)->RemoveMessageFilter(static_cast<cIGZWinMessageFilter*>(this));
        dropSources_.erase(found);
    }
    return result;
}

bool cGZWinDragAndDropManager::AddDropSourceProxy(std::uint32_t id, cIGZWin* window, bool add) {   // @ 0x1007b1f24
    if (!window) {
        window = GetParentWin();
        if (!window) return false;
    }
    cIGZWin* child = window->GetChildWindowFromIDRecursive(id);
    if (!child) return false;
    return AddDropSourceProxy(child, add);
}

void cGZWinDragAndDropManager::AddWindow() {   // @ 0x1007b18f0
    if (!dragData_) return;
    cIGZWin* parent = GetParentWin();
    if (!parent) return;
    cIGZWin* self = static_cast<cGZWin*>(this);
    if (parent->ChildExists(self)) return;
    int x = dragData_->startX, y = dragData_->startY;
    dragData_->source->WindowToWindowCoordinates(parent, x, y);   // the original dereferences source unguarded
    GZWinMoveTo(x, y);
    int width = 0x10, height = 0x10;   // no image yet: the deferred drag adds a 16x16 window first
    if (dragData_->image) {
        width = BufferWidth(*dragData_->image);
        height = BufferHeight(*dragData_->image);
    }
    SetSize(width, height);
    parent->ChildAdd(self);
}

bool cGZWinDragAndDropManager::GZOnCaptureChanged(cIGZWin* lost, cIGZWin* gained) {   // @ 0x1007b1f8e
    (void)lost;
    if (static_cast<cGZWin*>(this) != gained && IsDragging()) CancelDrag();
    return true;
}

bool cGZWinDragAndDropManager::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {   // @ 0x1007b1ff2
    (void)modifiers;
    if (key == 0x1b && IsDragging()) CancelDrag();   // VK_ESCAPE
    return true;
}

bool cGZWinDragAndDropManager::GZOnMouseWheel(int x, int y, std::uint32_t flags, int delta) {   // @ 0x1007b2052
    return cGZWin::GZOnMouseWheel(x, y, flags, delta);
}

bool cGZWinDragAndDropManager::GZOnMouseDownL(int x, int y, std::uint32_t flags) {   // @ 0x1007b206a
    (void)x;
    (void)y;
    (void)flags;
    if (IsDragging()) CancelDrag();
    return true;
}

bool cGZWinDragAndDropManager::GZOnMouseDownR(int x, int y, std::uint32_t flags) {   // @ 0x1007b20be
    (void)x;
    (void)y;
    (void)flags;
    if (IsDragging()) CancelDrag();
    return true;
}

bool cGZWinDragAndDropManager::GZOnMouseUpL(int x, int y, std::uint32_t flags) {   // @ 0x1007b2112
    (void)x;
    (void)y;
    (void)flags;
    if (IsDragging() && dragData_ && dragData_->magic == kDragMagicLeft) {
        if (!dragData_->cancelOnMouseUp) Drop();   // a real drag: drop
        else CancelDrag();                         // never left the start cell: a click
    }
    return true;
}

bool cGZWinDragAndDropManager::GZOnMouseUpR(int x, int y, std::uint32_t flags) {   // @ 0x1007b219a
    (void)x;
    (void)y;
    (void)flags;
    if (IsDragging() && dragData_ && dragData_->magic == kDragMagicRight) {
        if (!dragData_->cancelOnMouseUp) Drop();
        else CancelDrag();
    }
    return true;
}

bool cGZWinDragAndDropManager::GZOnMouseMove(int x, int y, std::uint32_t flags) {   // @ 0x1007b2222
    (void)flags;
    if (!IsDragging()) {   // quirk kept: a move without a drag cancels (releasing the capture)
        CancelDrag();
        return true;
    }
    DragData* data = dragData_;
    if (data->cancelOnMouseUp) {
        if (!data->source) {
            data->cancelOnMouseUp = false;
        } else {
            int sourceX = x, sourceY = y;
            static_cast<cGZWin*>(this)->WindowToWindowCoordinates(data->source.get(), sourceX, sourceY);
            if (AbsInt(x - sourceX) < 4 && AbsInt(y - sourceY) < 4) return true;   // still inside the start cell
            data->cancelOnMouseUp = false;
            cGZMessage begin = DragMessage(kDragMsgBegin, *data);   // sent before the image exists (see below)
            SendMessageA(data->target.get(), data->source.get(), begin);
            oldPrivateBufferFlag_ = data->source->GetFlag(kWinPrivateBuffer);
            data->source->SetFlag(kWinPrivateBuffer, true);
            AddWindow();
            SetFocus();
            return true;   // the image is captured on the next move
        }
    }
    // Quirk kept: the original re-tests the (now cleared) byte here and returns; the join point is the same.
    EndCaptureSourceWindowImage();
    data = dragData_;
    const int dx = x - data->startX, dy = y - data->startY;
    if ((dx | dy) != 0) {
        GZWinOffset(dx, dy);
        data = dragData_;
    }
    data->x = x;
    data->y = y;
    cRZAutoRefCount<cIGZWin> oldTarget(data->target.get());
    data->target = nullptr;
    // The drop target is the first window under the pointer found while walking the source's parent chain upwards;
    // every level enumerates its children recursively (the original dereferences source unguarded).
    cIGZWin* window = data->source ? data->source->GetParentWin() : nullptr;
    while (window && !data->target) {
        window->EnumChildren(kGZIID_cIGZWin, &cGZWinDragAndDropManager::SetDestWin, this);
        window = window->GetParentWin();
    }
    if (oldTarget && oldTarget.get() != data->target.get()) {
        cGZMessage leave = DragMessage(kDragMsgLeave, *data);
        SendMessageA(oldTarget.get(), data->source.get(), leave);
    }
    if (oldTarget.get() != data->target.get() && data->target) {
        cGZMessage enter = DragMessage(kDragMsgEnter, *data);
        SendMessageA(data->target.get(), data->source.get(), enter);
    }
    if (data->source) {
        cGZMessage over = DragMessage(kDragMsgOver, *data);
        SendMessageA(data->target.get(), data->source.get(), over);
    }
    return true;
}

bool cGZWinDragAndDropManager::SetDestWin(cIGZWin* parent, std::uint32_t id, void* child,
                                          void* context) {   // @ 0x1007b0fd0
    (void)parent;
    (void)id;
    auto* self = static_cast<cGZWinDragAndDropManager*>(context);
    cIGZWin* window = static_cast<cIGZWin*>(child);
    DragData* data = self->GetDragData();
    // Quirk kept: the original reads the pointer position before it looks at the window at all, so every enumerated
    // window costs one GetMousePosition call - including the drag source and windows the flags then reject.
    int mouseX = 0, mouseY = 0;
    window->GetWindowManager()->GetMousePosition(mouseX, mouseY);   // cIGZWinMgr vslot 0x108
    if (window == static_cast<cGZWin*>(self) || window == data->source.get()) return true;
    if (!window->GetFlag(kWinVisible) || !window->GetFlag(kWinFlagCanFocus) ||
        window->GetFlag(kWinIgnoreCursor))
        return true;
    if (!window->IsPointInWindowScreenCoordinates(mouseX, mouseY)) return true;
    data->target = window;
    window->EnumChildren(kGZIID_cIGZWin, &cGZWinDragAndDropManager::SetDestWin, self);
    return true;
}

void cGZWinDragAndDropManager::EndCaptureSourceWindowImage() {   // @ 0x1007b24d8
    if (!IsDragging()) return;
    if (dragData_->image && dragData_->imageCopy) return;
    cRZAutoRefCount<IWinBuffer> image;
    if (IWinGraphics* graphics = WinGraphics()) image = graphics->GraphicsCreateBuffer();
    cIGZWin* source = dragData_->source.get();
    IWinBuffer* sourceBuffer = source ? static_cast<IWinBuffer*>(source->GetPrivateBuffer()) : nullptr;
    if (sourceBuffer) {
        sourceBuffer->AddRef();
        if (image) {
            const int width = BufferWidth(*sourceBuffer), height = BufferHeight(*sourceBuffer);
            image->Init(width, height);   // the original asks for ARGB8888 (colour type 9 / 0x20 bits)
            std::uint32_t transparent = 0;
            image->GetTransparentColor(transparent);        // false on a fresh buffer: the key stays 0
            image->Fill(transparent, *image->GetBounds());   // the original fills the whole buffer (null clip)
            const cRZRect sourceRect = *sourceBuffer->GetBounds();
            const cRZRect destRect = *image->GetBounds();
            image->Blt(sourceBuffer, sourceRect, destRect);
            dragData_->image = image.get();
            dragData_->imageCopy = image.get();   // both slots hold the same buffer in the original
            SetSize(width, height);
        }
    }
    if (source) source->SetFlag(kWinPrivateBuffer, oldPrivateBufferFlag_);
    if (sourceBuffer) sourceBuffer->Release();
}

bool cGZWinDragAndDropManager::GZPaint() {   // @ 0x1007b26fe
    if (IsDragging()) {
        IWinBuffer* image = dragData_->imageMode == 1 ? dragData_->imageCopy.get() : dragData_->image.get();
        if (image) {
            const cRZRect source = *image->GetBounds();
            const cRZRect destination{drawArea_.left, drawArea_.top,
                                      source.right + drawArea_.left - source.left,
                                      source.bottom + drawArea_.top - source.top};
            if (auto* context = static_cast<cIGZDrawContext*>(drawContext_))   // the original does not test it
                context->Blt(image->AsDrawSource(), source, destination);
        }
    } else if (cIGZWin* parent = GetParentWin()) {
        parent->ChildRemove(static_cast<cGZWin*>(this));
        parent_ = nullptr;   // the original clears the parent field itself after ChildRemove
    }
    return true;
}

void cGZWinDragAndDropManager::SendMessageA(cIGZWin* primary, cIGZWin* secondary,
                                            cGZMessage& message) {   // @ 0x1007b27cc
    cIGZWinMgr* manager = mgr_;
    if (!manager) return;
    cIGZWin* const windows[2] = {primary, secondary};
    for (cIGZWin* window : windows) {
        if (!window || !manager->IsWindowValid(window)) continue;
        cRZAutoRefCount<cIGZWin> filter;
        if (!window->QueryInterface(kGZIID_cGZWinAlt, filter.AsOutParam())) continue;
        if (filter) filter->DoMessage(message);
    }
}

}  // namespace gz

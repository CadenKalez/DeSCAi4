#include "gz/WinTextTicker.h"

#include <iterator>

#include "gz/Buffer.h"
#include "gz/DrawContext.h"
#include "gz/Font.h"
#include "rz/CharLength.h"
#include "rz/TextEncoding.h"

namespace gz {
namespace {

// Lock flag words the original passes to cIGZBuffer::Lock (cGZBuffer reads them as a bit set).
constexpr std::uint32_t kLockClear = 0x8001;   // read the fill
constexpr std::uint32_t kLockLine = 0x8004;    // read the line
constexpr std::uint32_t kLockScroll = 0x8010;  // read the blit
inline cIGZBuffer::eLockFlags Flags(std::uint32_t flags) { return static_cast<cIGZBuffer::eLockFlags>(flags); }

IWinTextTickerHost* g_host = nullptr;

}  // namespace

IWinTextTickerHost* WinTextTickerHost() { return g_host; }
void SetWinTextTickerHost(IWinTextTickerHost* host) { g_host = host; }

// @ 0x100513f60: set the two vtables, construct the cGZWin at +8, the timer and the lists, then let the graphics
// system create the strip (RZ::GS() vslot 0x20).
cGZWinTextTicker::cGZWinTextTicker(cIGZWinMgr* mgr)
    : cIGZWinTextTicker(), cGZWin(mgr), timer_(cRZTimer::kMilliseconds) {
    if (IWinTextTickerHost* host = WinTextTickerHost()) buffer_ = host->CreateBuffer();
    current_ = active_.end();   // the original stores 0 in +0x188 until Init (see DoDraw's end-iterator guard)
}

// @ 0x1005140c6: Shutdown, drop the list nodes, destroy the timer, then the cGZWin base.
cGZWinTextTicker::~cGZWinTextTicker() {
    Shutdown();
    active_.clear();   // the messages themselves were already deleted by ClearQueue / Shutdown
    queue_.clear();
}

// @ 0x1005141d0
bool cGZWinTextTicker::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != kGZIID_cIGZWinTextTicker) return cGZWin::QueryInterface(iid, out);
    if (out) {
        *out = static_cast<cIGZWinTextTicker*>(this);
        AddRef();
    }
    return out != nullptr;
}

// @ 0x10051425c
bool cGZWinTextTicker::Init() {
    if (cGZWin::get_init_flag()) return true;   // the original answers 1 without touching anything
    scrollX_ = 0;
    timer_.Restart();
    if (!font_ || !buffer_) return false;   // guarded: the original dereferences +0x1a0 / +0x178 here
    SetH(font_->CharHeight(nullptr, 0));    // the strip is one font line high (font vslot 0x128)
    const cRZRect& area = GetAreaToDrawTo();
    // Colour type 0x1000000003 = {type = 3 (default / display format), bits per pixel = 1}.
    if (!buffer_->Init(static_cast<std::uint32_t>(area.right - area.left),
                       static_cast<std::uint32_t>(area.bottom - area.top), cGZBufferColorType{kBufferColorDefault, 1})) {
        return false;
    }
    std::uint32_t keyColor = 0;
    if (IWinTextTickerHost* host = WinTextTickerHost()) keyColor = host->DefaultColor(kTextTickerKeyColorId);
    buffer_->SetTransparency(keyColor);   // buffer vslot 0xc8
    if (buffer_->Lock(Flags(kLockClear))) {
        buffer_->Fill(GetFillColor(), nullptr);   // buffer vslot 0xb0: fill the whole strip
        buffer_->Unlock(Flags(kLockClear));
    }
    current_ = active_.end();
    if (mgr_) {
        cIGZCursorManager* cursors = mgr_->GetCursorManager();
        SetCursor(cursors ? cursors->LoadCursor(kTextTickerCursorId) : nullptr, false);
    }
    codePage_ = GetCurrentFrameworkCodePage();
    return cGZWin::Init();
}

// @ 0x1005143da
bool cGZWinTextTicker::Shutdown() {
    ClearQueue();   // the original calls the virtual slot 0x30
    // The original drains the active list here as well; ClearQueue already emptied it.
    while (!active_.empty()) {
        delete active_.back();
        active_.pop_back();
    }
    if (font_) {
        font_->Release();
        font_ = nullptr;
    }
    if (buffer_) {
        buffer_->Release();
        buffer_ = nullptr;
    }
    // messageFont_ is left as is (the original never clears +0x1a8 either).
    return cGZWin::Shutdown();
}

// @ 0x100514490
bool cGZWinTextTicker::ClearQueue() {
    while (!queue_.empty()) {   // the original walks the queue list from its last node towards the head
        delete queue_.back();
        queue_.pop_back();
    }
    while (!active_.empty()) {
        delete active_.back();
        active_.pop_back();
    }
    if (buffer_ && buffer_->Lock(Flags(kLockClear))) {
        buffer_->Fill(GetFillColor(), nullptr);
        buffer_->Unlock(Flags(kLockClear));
    }
    // Guarded: the original leaves +0x188 / +0x190 pointing at the messages it just deleted (the scroll position at
    // +0x170 is left as is, which is kept).
    current_ = active_.end();
    textCursor_ = nullptr;
    return true;
}

// @ 0x10051459a: scroll the strip one step left, clear the exposed edge, shift the message spans and draw the
// current message (or start the next one).
void cGZWinTextTicker::DoDraw() {
    if (!buffer_) return;   // guarded (the original assumes the strip exists)
    cIGZBuffer* strip = buffer_;
    const int width = static_cast<int>(strip->Width());     // buffer vslot 0x58
    const int height = static_cast<int>(strip->Height());   // buffer vslot 0x60

    // Move the strip to the left: blit [speed .. width] onto [0 .. width - speed], in place.
    if (strip->Lock(Flags(kLockScroll))) {
        const cRZRect source{speed_, 0, width, height};
        const cRZRect destination{0, 0, width - speed_, height};
        strip->Blt(strip, source, destination, nullptr);   // buffer vslot 0xf8
        strip->Unlock(Flags(kLockScroll));
    }
    // Clear the strip that scrolled in on the right with the window's fill colour.
    if (strip->Lock(Flags(kLockClear))) {
        const cRZRect clearArea{width - speed_, 0, width, height};
        strip->Fill(GetFillColor(), clearArea, nullptr);   // buffer vslot 0xa0 (fill colour = cGZWin vslot 0x340)
        strip->Unlock(Flags(kLockClear));
    }

    scrollX_ += speed_;
    shiftSpans();
    if (active_.empty()) advanceMessage();
    // Guarded: the original reads the message out of the end node and dereferences the null it gets (0x1005146dd).
    TickerMessage* message = current_ == active_.end() ? nullptr : *current_;
    if (!message || !message->text_) return;

    const char* text = message->text_->Data();                                // cIGZString [8] +0x40
    const std::uint32_t length = message->text_->Strlen();                    // cIGZString [9] +0x48
    if (!textCursor_ || text + length <= textCursor_) {
        advanceMessage();
        return;
    }

    std::uint32_t transparent = 0;   // the original compares an uninitialised word when the query fails
    strip->GetTransparentColor(transparent);   // buffer vslot 0xe0
    if (message->word34_ != transparent) {
        if (strip->Lock(Flags(kLockClear))) {
            const cRZRect backgroundArea{width - scrollX_, 0, width, height};
            strip->Fill(message->word34_, backgroundArea, nullptr);
            strip->Unlock(Flags(kLockClear));
        }
    }
    if ((flags_ & kTextTickerUnderline) != 0) {
        if (strip->Lock(Flags(kLockLine))) {
            strip->DrawLine(width - scrollX_, height - 1, width, height - 1, textColor_, 1);   // buffer vslot 0xf0
            strip->Unlock(Flags(kLockLine));
        }
    }
    if (scrollX_ > 0 && messageFont_) {   // the font arrives with the message, so it is set when scrollX_ > 0
        messageFont_->GetFontColor();   // the original reads and drops the font's colour here
        const int chars = GetCharLength(textCursor_, static_cast<std::uint32_t>(text + length - textCursor_), codePage_);
        if (messageFont_->LockSurfaceForFontUse(strip)) {   // font vslot 0x1b8
            messageFont_->DrawTextLine(strip, width - scrollX_, 0, textCursor_, static_cast<std::uint32_t>(chars),
                                       textColor_, backgroundColor_, nullptr, nullptr);   // font vslot 0x198
            messageFont_->UnlockSurfaceForFontUse(strip);   // font vslot 0x1c0
        }
        const int advance = font_ ? font_->GetAdvanceWidth(textCursor_, static_cast<std::uint32_t>(chars)) : 0;
        if (advance <= scrollX_) {
            scrollX_ -= advance;
            textCursor_ += chars;
        }
    }
}

// @ 0x1005148fe: shift every on-screen message with the strip and delete the ones whose right edge passed the left
// edge. The original walks the list from its last node towards the head and so also visits a message that
// advanceMessage inserted at the front while it walks.
void cGZWinTextTicker::shiftSpans() {
    if (active_.empty()) return;
    for (auto next = active_.end(); next != active_.begin();) {
        auto node = std::prev(next);
        TickerMessage* message = *node;
        next = node;
        if (!message) {
            next = active_.erase(node);
            continue;
        }
        message->state20_ -= static_cast<std::uint32_t>(speed_);
        message->state28_ -= static_cast<std::uint32_t>(speed_);
        if (static_cast<std::int32_t>(message->state28_) < 0) {
            if (current_ == node) {
                advanceMessage();
                // Guarded: when advanceMessage picked the message being deleted (a lone message whose span ran out
                // without its text being drawn) the original goes on with a dangling +0x188; drop it here instead.
                if (current_ == node) {
                    current_ = active_.end();
                    textCursor_ = nullptr;
                    messageFont_ = nullptr;
                }
            }
            delete message;
            next = active_.erase(node);   // erase returns the node we came from
        }
    }
}

// @ 0x10051499a
void cGZWinTextTicker::advanceMessage() {
    GetNextMessage();   // virtual slot 0xb0: move the oldest queued message into the active list
    if (active_.empty()) {
        textCursor_ = nullptr;
        current_ = active_.end();
        return;
    }
    current_ = active_.begin();
    TickerMessage* message = *current_;
    messageFont_ = message->font_;          // borrowed from the message, never released here
    flags_ = message->word0c_;
    textColor_ = message->word30_;
    backgroundColor_ = message->word34_;
    textCursor_ = message->text_ ? message->text_->Data() : nullptr;
    const std::uint32_t width = message->text_ ? message->text_->Strlen() : 0;
    message->state20_ = static_cast<std::uint32_t>(GetW() + kTextTickerLeftMargin);
    // The right edge is measured from the window's own width again (the original calls GetW a second time here).
    const int textWidth = messageFont_ ? messageFont_->GetStringWidth(textCursor_, width) : 0;
    message->state28_ = static_cast<std::uint32_t>(textWidth + kTextTickerLeftMargin + GetW());
    scrollX_ = -kTextTickerLeftMargin;
}

// @ 0x100514aaa
bool cGZWinTextTicker::GZPaint() {
    if (frequency_ < timer_.GetElapsedTimeU32()) {   // cRZTimer::GetElapsedTimeU32 @ 0x1007b6d54
        DoDraw();
        timer_.Restart();
    }
    cIGZDrawContext* context = static_cast<cIGZDrawContext*>(GetDrawContext());
    if (context && buffer_) {
        context->Blt(buffer_, *buffer_->GetBufferArea(), GetAreaToDrawTo());   // context vslot 0x130
    }
    InvalidateSelf();   // cGZWin vslot 0x2d8 (the original does not call cGZWin::GZPaint)
    return true;
}

// @ 0x100514b3c: a click on a message calls its callback and posts its message when it has one. The original compares
// the x coordinate only and leaves y in the third argument register of the callback.
bool cGZWinTextTicker::GZOnMouseDownL(int x, int /*y*/, std::uint32_t /*modifiers*/) {
    for (auto next = active_.end(); next != active_.begin();) {
        auto node = std::prev(next);
        TickerMessage* message = *node;
        next = node;
        if (!message) continue;
        if (static_cast<std::int32_t>(message->state20_) > x) continue;
        if (x >= static_cast<std::int32_t>(message->state28_)) continue;
        if (message->callback_ != nullptr) message->callback_(message->id_, message->context_);
        if (message->message_.type == 0) return true;
        if (IWinTextTickerHost* host = WinTextTickerHost()) host->MessagePost(message->message_);
        return true;
    }
    return true;
}

// @ 0x100514bca
std::uint32_t cGZWinTextTicker::AddMessage(cIGZString* text, std::uint32_t color, std::uint32_t background,
                                           std::uint32_t flags, MessageCallback callback, void* context) {
    if (!font_ || !text) return 0;   // guarded: the original only checks the font and dereferences the text later
    auto* message = new TickerMessage(++nextId_, text, font_, color, background, flags, 0, 0, nullptr, callback, context);
    queue_.push_front(message);   // the original links the node behind the queue head and bumps +0x1e0
    return message->id_;
}

// @ 0x100514cb8
std::uint32_t cGZWinTextTicker::AddMessage(cIGZString* text, std::uint32_t color, std::uint32_t background,
                                           std::uint32_t flags, const cGZMessage* message) {
    if (!font_ || !text) return 0;
    auto* created = new TickerMessage(++nextId_, text, font_, color, background, flags, 0, 0, message, nullptr, nullptr);
    queue_.push_front(created);
    return created->id_;
}

// @ 0x100514d9a
void cGZWinTextTicker::AddMessageInternal(TickerMessage* message) {
    if (!message) return;   // the original's GetNextMessage can hand over a null
    const int textWidth = (font_ && message->text_)
                              ? font_->GetStringWidth(message->text_->Data(), message->text_->Strlen())
                              : 0;
    message->state20_ = static_cast<std::uint32_t>(GetW() + kTextTickerLeftMargin);
    message->state28_ = static_cast<std::uint32_t>(textWidth + kTextTickerLeftMargin + GetW());
    active_.push_front(message);
    if (!messageFont_) {   // only the very first message ever shown hands over its font and colours here
        messageFont_ = message->font_;
        flags_ = message->word0c_;
        textColor_ = message->word30_;
        backgroundColor_ = message->word34_;
    }
}

// @ 0x100514e98
void cGZWinTextTicker::GetNextMessage() {
    if (queue_.empty()) return;
    TickerMessage* message = queue_.back();   // the queue is filled at the front and drained at the back (FIFO)
    queue_.pop_back();
    AddMessageInternal(message);   // virtual slot 0xb8
}

// @ 0x100514ef4: only the queued messages are searched; the ones already on screen are not.
bool cGZWinTextTicker::RemoveMessage(std::uint32_t id) {
    for (auto next = queue_.end(); next != queue_.begin();) {
        auto node = std::prev(next);
        TickerMessage* message = *node;
        next = node;
        if (message && message->id_ == id) {
            queue_.erase(node);
            delete message;
            return true;
        }
    }
    return false;
}

// @ 0x100514f62
bool cGZWinTextTicker::SetFont(cIGZFont* font) {
    if (font) font->AddRef();
    if (font_) font_->Release();
    font_ = font;
    return true;
}

// @ 0x100515048
void cGZWinTextTicker::SetSpeed(std::uint32_t pixelsPerTick) { speed_ = static_cast<std::int32_t>(pixelsPerTick); }

// @ 0x100515054
void cGZWinTextTicker::SetFrequency(std::uint32_t milliseconds) { frequency_ = milliseconds; }

// @ 0x100515060
std::uint32_t cGZWinTextTicker::GetSpeed() const { return static_cast<std::uint32_t>(speed_); }

// @ 0x10051506c
std::uint32_t cGZWinTextTicker::GetFrequency() const { return frequency_; }

// @ 0x100514f9e
cIGZWin* cGZWinTextTicker::AsIGZWin() { return static_cast<cGZWin*>(this); }

// @ 0x100514fa8 / @ 0x100514ff8: build a key message and pass it to the notification target. The original tail-jumps
// into cGZWin::SendMsg (vslot 0x488), whose recovered five-argument overload returns void, so the message is built and
// dispatched through the two-argument SendMsg here to keep the answer (cGZWin::DoMessage decides with it).
bool cGZWinTextTicker::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {
    cGZMessage message{kGZMsgKeyDown, key, modifiers, 0};
    return SendMsg(GetNotificationTarget(), message);
}

bool cGZWinTextTicker::GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) {
    cGZMessage message{kGZMsgKeyUp, key, modifiers, 0};
    return SendMsg(GetNotificationTarget(), message);
}

// @ 0x100515078: the caption is ignored (the original body is a single RET).
void cGZWinTextTicker::SetCaption(const cIGZString& caption) { (void)caption; }

}  // namespace gz

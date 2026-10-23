// cGZWinTextTicker — the scrolling text ticker control (30 functions / 42 symbols with the compiler thunks,
// 0x100513f60..0x10051507e; iid 0x032f0b3c for the cIGZWinTextTicker view at +0; GZCOM class 0xe32f0b31, created by
// cGZWinCOMDirector::CreateGZWinTextTicker). Messages are queued with AddMessage and shown as a train: every tick the whole
// strip is scrolled one `speed` pixel to the left, the freshly exposed right edge is filled with the window colour and
// the current message is drawn at `width - scroll position`, so it slides in from the right. When a message's text has
// been drawn to its end the next queued message starts entering; a message's span (+0x20..+0x28 of TickerMessage) is
// shifted with the strip and the message is deleted once its right edge passes the left edge.
//
// Layout (0x1f0 bytes): +0 the cIGZWinTextTicker view (vtable 0x100b0dc18), +8 the 0x150-byte cGZWin, +0x158 the frame
// timer (millisecond units), +0x170 scroll position in pixels, +0x178 the off-screen strip (cIGZBuffer, counted),
// +0x180 the current message's flag word (bit 0 = underline), +0x184 frame code page, +0x188 iterator of the current
// message inside the active list, +0x190 next character of that message to draw, +0x198 message id counter, +0x1a0 the
// font (counted), +0x1a8 the current message's font (borrowed), +0x1b0 / +0x1b4 its text / background colours,
// +0x1b8 the active list (front = the message that starts entering next), +0x1d0 the queue (AddMessage pushes there,
// GetNextMessage moves the oldest to the active list), +0x1e8 speed in pixels per tick (2), +0x1ec frequency in
// milliseconds (0x32). Both lists are std::list<TickerMessage*>; the original's libc++ keeps a node's next pointer
// before its prev pointer, so only the front / back order of every walk is carried over here (queue: push front, take
// the oldest at the back; active: push front, walk from the back towards the head).
//
// Quirks kept: RemoveMessage only searches the queued messages, never the ones already on screen; ClearQueue does not
// reset the scroll position; the message id counter starts at 1 and is never rewound; AddMessageInternal only copies
// the font / colours of the first message it ever sees (the current-message font is never cleared again), and the last
// message of a train starts over from the right when its text has been drawn to the end.
// Quirks guarded (the original dereferences a null font / strip / message): Init returns false, DoDraw returns early,
// and ClearQueue resets the current iterator and text cursor (the original leaves them dangling).
#pragma once
#include <cstddef>
#include <cstdint>
#include <list>

#include "gz/Win.h"
#include "rz/cRZTimer.h"
#include "sc4/TickerMessage.h"

class cIGZBuffer;
class cIGZFont;

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinTextTicker = 0x032f0b3c;
constexpr std::uint32_t kTextTickerDefaultSpeed = 2;          // +0x1e8 pixels per tick
constexpr std::uint32_t kTextTickerDefaultFrequency = 0x32;   // +0x1ec milliseconds between ticks
constexpr std::uint32_t kTextTickerUnderline = 1;             // +0x180 flag bit: draw a line under the text
constexpr std::uint32_t kTextTickerKeyColorId = 0x08963f6c;   // RZ::WCM() vslot 0xf8: the strip's transparent colour
constexpr std::uint32_t kTextTickerCursorId = 0x12;           // cursor manager vslot 0x38 (LoadCursor)
constexpr std::int32_t kTextTickerLeftMargin = 0x14;          // blank pixels left of the text / initial scroll position

// The engine services the ticker asks for: the graphics system (RZ::GS()), the window colour manager (RZ::WCM()) and
// the message server (RZ::MS()). No default host is installed; without one no strip is created, the transparent key
// colour is 0 and clicked messages are not posted.
class IWinTextTickerHost {
public:
    virtual ~IWinTextTickerHost() = default;
    virtual cIGZBuffer* CreateBuffer() = 0;                          // RZ::GS() vslot 0x20: a new strip, one reference
    virtual std::uint32_t DefaultColor(std::uint32_t colorId) = 0;   // RZ::WCM() vslot 0xf8
    virtual bool MessagePost(const cGZMessage& message) = 0;         // RZ::MS() vslot 0x20 (toFront = false)
};
IWinTextTickerHost* WinTextTickerHost();
void SetWinTextTickerHost(IWinTextTickerHost* host);

// The ticker view (vtable 0x100ac1380 primary group). `color` and `background` are native colours handed to the font;
// `flags` bit 0 (kTextTickerUnderline) draws a line under the text. AddMessage returns the new message id, or 0 when
// the ticker has no font or the text is null. GetNextMessage / AddMessageInternal are the internal virtual steps of
// advanceMessage (the original keeps them in the interface, at slots 0xb0 / 0xb8).
class cIGZWinTextTicker : public cIGZUnknown {
public:
    using MessageCallback = TickerMessage::Callback;   // void (*)(std::uint32_t id, void* context)

    virtual std::uint32_t AddMessage(cIGZString* text, std::uint32_t color, std::uint32_t background,
                                     std::uint32_t flags, MessageCallback callback, void* context) = 0;   // [3] 0x18
    virtual std::uint32_t AddMessage(cIGZString* text, std::uint32_t color, std::uint32_t background,
                                     std::uint32_t flags, const cGZMessage* message) = 0;                  // [4] 0x20
    virtual bool RemoveMessage(std::uint32_t id) = 0;                             // [5] 0x28
    virtual bool ClearQueue() = 0;                                                // [6] 0x30
    virtual bool SetFont(cIGZFont* font) = 0;                                     // [7] 0x38
    virtual void SetSpeed(std::uint32_t pixelsPerTick) = 0;                       // [8] 0x40
    virtual void SetFrequency(std::uint32_t milliseconds) = 0;                    // [9] 0x48
    virtual std::uint32_t GetSpeed() const = 0;                                   // [10] 0x50
    virtual std::uint32_t GetFrequency() const = 0;                               // [11] 0x58
    virtual cIGZWin* AsIGZWin() = 0;                                              // [12] 0x60
    virtual bool GZPaint() = 0;                                                   // [13] 0x68
    virtual bool Init() = 0;                                                      // [14] 0x70
    virtual bool Shutdown() = 0;                                                  // [15] 0x78
    virtual void SetCaption(const cIGZString& caption) = 0;                       // [16] 0x80 (ignored)
    virtual bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) = 0;       // [17] 0x88
    virtual bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) = 0;     // [18] 0x90
    virtual bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) = 0;       // [19] 0x98
    // The original's destructor sits between the handlers and the two internal virtuals: slots 0xa0 / 0xa8.
    virtual ~cIGZWinTextTicker() = default;
    virtual void GetNextMessage() = 0;                                            // [22] 0xb0
    virtual void AddMessageInternal(TickerMessage* message) = 0;                  // [23] 0xb8
};

class cGZWinTextTicker : public cIGZWinTextTicker, public cGZWin {
public:
    explicit cGZWinTextTicker(cIGZWinMgr* mgr);       // @ 0x100513f60 (the original takes the manager from RZ::WM())
    ~cGZWinTextTicker() override;                     // @ 0x1005140c6

    bool QueryInterface(std::uint32_t iid, void** out) override;      // @ 0x1005141d0
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }      // @ 0x10051422c
    std::uint32_t Release() override { return cGZWin::Release(); }    // @ 0x100514244

    // cGZWin overrides (the original's cGZWin vtable thunks here)
    bool Init() override;                                             // @ 0x10051425c
    bool Shutdown() override;                                         // @ 0x1005143da
    bool GZPaint() override;                                          // @ 0x100514aaa
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;   // @ 0x100514fa8
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;     // @ 0x100514ff8
    bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) override;     // @ 0x100514b3c
    void SetCaption(const cIGZString& caption) override;              // @ 0x100515078 (a no-op)

    // cIGZWinTextTicker
    std::uint32_t AddMessage(cIGZString* text, std::uint32_t color, std::uint32_t background, std::uint32_t flags,
                             MessageCallback callback, void* context) override;              // @ 0x100514bca
    std::uint32_t AddMessage(cIGZString* text, std::uint32_t color, std::uint32_t background, std::uint32_t flags,
                             const cGZMessage* message) override;                            // @ 0x100514cb8
    bool RemoveMessage(std::uint32_t id) override;                    // @ 0x100514ef4
    bool ClearQueue() override;                                       // @ 0x100514490
    bool SetFont(cIGZFont* font) override;                            // @ 0x100514f62
    void SetSpeed(std::uint32_t pixelsPerTick) override;              // @ 0x100515048
    void SetFrequency(std::uint32_t milliseconds) override;           // @ 0x100515054
    std::uint32_t GetSpeed() const override;                          // @ 0x100515060
    std::uint32_t GetFrequency() const override;                      // @ 0x10051506c
    cIGZWin* AsIGZWin() override;                                     // @ 0x100514f9e
    void GetNextMessage() override;                                   // @ 0x100514e98
    void AddMessageInternal(TickerMessage* message) override;         // @ 0x100514d9a

    // internals (public for the tests)
    void DoDraw();                                  // @ 0x10051459a
    void shiftSpans();                              // @ 0x1005148fe
    void advanceMessage();                          // @ 0x10051499a
    std::size_t ActiveCount() const { return active_.size(); }
    std::size_t QueueCount() const { return queue_.size(); }
    TickerMessage* CurrentMessage() const { return current_ == active_.end() ? nullptr : *current_; }
    std::int32_t ScrollPosition() const { return scrollX_; }
    const char* TextCursor() const { return textCursor_; }
    std::uint32_t CurrentFlags() const { return flags_; }

private:
    cRZTimer timer_;                        // +0x158 (constructed with millisecond units)
    std::int32_t scrollX_ = 0;              // +0x170
    cIGZBuffer* buffer_ = nullptr;          // +0x178 counted; the strip the text is drawn into
    std::uint32_t flags_ = 0;               // +0x180 current message word0c (bit 0 = underline)
    int codePage_ = 0;                      // +0x184
    std::list<TickerMessage*>::iterator current_;   // +0x188 current active message (node pointer, like the original)
    const char* textCursor_ = nullptr;      // +0x190 next character of the current message to draw
    std::uint32_t nextId_ = 0;              // +0x198
    cIGZFont* font_ = nullptr;              // +0x1a0 counted
    cIGZFont* messageFont_ = nullptr;       // +0x1a8 borrowed from the current message
    std::uint32_t textColor_ = 0;           // +0x1b0 current message word30
    std::uint32_t backgroundColor_ = 0;     // +0x1b4 current message word34
    std::list<TickerMessage*> active_;      // +0x1b8 messages on screen, front = starts entering next
    std::list<TickerMessage*> queue_;       // +0x1d0 messages waiting; GetNextMessage pops the oldest
    std::int32_t speed_ = kTextTickerDefaultSpeed;             // +0x1e8
    std::uint32_t frequency_ = kTextTickerDefaultFrequency;    // +0x1ec
};

static_assert(sizeof(cGZWin) == 0x150, "cGZWin layout changed: the ticker field offsets assume 0x150 bytes");
static_assert(sizeof(cRZTimer) == 0x18, "cRZTimer layout changed: the ticker field offsets assume 0x18 bytes");

}  // namespace gz

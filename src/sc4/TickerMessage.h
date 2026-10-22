// TickerMessage — one queued entry of the news ticker (0x100513de0..0x100513f32, vtable 0x100ac1360, 0x70 bytes).
// Holds a reference to its text and font, the layout/colour words the ticker window uses, an optional click message
// and a completion callback. Fields the (unrecovered) cSC4NewsTickerWinProc interprets keep neutral names
// (see TICKER-1).
#pragma once
#include <cstdint>

#include "gz/Font.h"
#include "gz/WinInterfaces.h"
#include "gz/cIGZString.h"

class TickerMessage {
public:
    using Callback = void (*)(std::uint32_t id, void* context);

    // Takes a reference on `text` and `font` (either may be null). `message` (may be null) is copied as the click message.
    TickerMessage(std::uint32_t id, cIGZString* text, cIGZFont* font, std::uint32_t word30, std::uint32_t word34,
                  std::uint32_t word0c, std::uint32_t word38, std::uint32_t word3c, const cGZMessage* message,
                  Callback callback, void* context);
    virtual ~TickerMessage();   // releases text and font

    std::uint32_t id_;              // +0x08
    std::uint32_t word0c_;          // +0x0c
    cIGZString* text_;              // +0x10
    cIGZFont* font_;                // +0x18
    std::uint32_t state20_ = 0xfffe;   // +0x20
    std::uint32_t state28_ = 0xffff;   // +0x28
    std::uint32_t word30_, word34_, word38_, word3c_;   // +0x30..0x3c
    cGZMessage message_;            // +0x40 (type, three words)
    Callback callback_;             // +0x60
    void* context_;                 // +0x68
};

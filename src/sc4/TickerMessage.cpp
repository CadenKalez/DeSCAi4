#include "sc4/TickerMessage.h"

TickerMessage::TickerMessage(std::uint32_t id, cIGZString* text, cIGZFont* font, std::uint32_t word30, std::uint32_t word34,
                             std::uint32_t word0c, std::uint32_t word38, std::uint32_t word3c, const cGZMessage* message,
                             Callback callback, void* context)
    : id_(id), word0c_(word0c), text_(text), font_(font), word30_(word30), word34_(word34), word38_(word38),
      word3c_(word3c), callback_(callback), context_(context) {
    if (font_) font_->AddRef();
    if (text_) text_->AddRef();
    if (message) message_ = *message;   // the original zeroes the type and the first two words when null (already zero)
}

TickerMessage::~TickerMessage() {
    if (text_) { text_->Release(); text_ = nullptr; }
    if (font_) { font_->Release(); font_ = nullptr; }
}

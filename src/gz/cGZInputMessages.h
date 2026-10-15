// The three window-system input messages (0x10061bc6a..0x10061c0ee): key, mouse and canvas events carried as
// cIGZMessage2 objects. Layout: +8 refcount, +0xc message type, then the event payload.
// cGZKeyMessage (CLSID-less, iid 0x1a104742): event, key, flags, repeat count at +0x10..+0x1c.
// cGZMouseMessage (iid 0x1a0c615f): event +0x10, position +0x14, wheel delta +0x1c, key flags +0x20.
// cGZCanvasMessage (iid 0xda1062fa): event +0x10.
#pragma once
#include <cstdint>

#include "gz/cIGZMessage2.h"
#include "rz/cRZPoint.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZMessage2 = 0xa52294b4;
constexpr std::uint32_t kGZIID_cIGZKeyMessage = 0x1a104742, kGZIID_cIGZMouseMessage = 0x1a0c615f,
                        kGZIID_cIGZCanvasMessage = 0xda1062fa;
constexpr std::uint32_t kKeyMessageType = 0x7a104750, kMouseMessageType = 0xfa0c6390, kCanvasMessageType = 0x1a10630a;

class cIGZKeyMessage : public cIGZMessage2 {
public:
    using KeyEvent = std::uint32_t;
    virtual void Initialize(KeyEvent event, std::uint32_t key, std::uint32_t flags, std::uint32_t repeat) = 0;
    virtual KeyEvent EventType() const = 0;
    virtual std::uint32_t Key() const = 0;
    virtual std::uint32_t KeyFlags() const = 0;
    virtual std::uint32_t RepeatCount() const = 0;
};
class cIGZMouseMessage : public cIGZMessage2 {
public:
    using MouseEvent = std::uint32_t;
    virtual void Initialize(MouseEvent event, const cRZPoint& position, std::uint32_t keyFlags, std::int32_t wheelDelta) = 0;
    virtual MouseEvent EventType() const = 0;
    virtual cRZPoint Position() const = 0;
    virtual std::int32_t WheelDelta() const = 0;
    virtual std::uint32_t KeyFlags() const = 0;
};
class cIGZCanvasMessage : public cIGZMessage2 {
public:
    using CanvasEvent = std::uint32_t;
    virtual void Initialize(CanvasEvent event) = 0;
    virtual CanvasEvent EventType() const = 0;
};

// Shared message behaviour: refcount, type, ordering by type.
template <class Interface, std::uint32_t Iid, std::uint32_t InitialType>
class cGZInputMessage : public Interface {
public:
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != kGZIID_cIGZMessage2 && iid != 1 && iid != Iid) return false;
        *out = static_cast<Interface*>(this);
        this->AddRef();
        return true;
    }
    std::uint32_t AddRef() override { return ++refCount_; }
    std::uint32_t Release() override {
        if (refCount_ > 1) return --refCount_;
        delete this;
        return 0;
    }
    std::uint32_t GetType() const override { return type_; }
    void SetType(std::uint32_t type) override { type_ = type; }
    bool IsEqual(const cIGZMessage2& other) const override { return type_ == other.GetType(); }
    bool IsLess(const cIGZMessage2& other) const override { return type_ < other.GetType(); }

protected:
    virtual ~cGZInputMessage() = default;
    std::uint32_t refCount_ = 0;     // +8
    std::uint32_t type_ = InitialType;   // +0xc
};

class cGZKeyMessage final : public cGZInputMessage<cIGZKeyMessage, kGZIID_cIGZKeyMessage, kKeyMessageType> {
public:
    bool Create(std::uint32_t iid, void** out) const override;
    void Initialize(KeyEvent event, std::uint32_t key, std::uint32_t flags, std::uint32_t repeat) override {
        event_ = event; key_ = key; flags_ = flags; repeat_ = repeat;
    }
    KeyEvent EventType() const override { return event_; }
    std::uint32_t Key() const override { return key_; }
    std::uint32_t KeyFlags() const override { return flags_; }
    std::uint32_t RepeatCount() const override { return repeat_; }

private:
    KeyEvent event_ = 0xffffffff;        // +0x10
    std::uint32_t key_ = 0;              // +0x14
    std::uint32_t flags_ = 0;            // +0x18
    std::uint32_t repeat_ = 0;           // +0x1c (left uninitialised by the original constructor)
};

class cGZMouseMessage final : public cGZInputMessage<cIGZMouseMessage, kGZIID_cIGZMouseMessage, kMouseMessageType> {
public:
    bool Create(std::uint32_t iid, void** out) const override;
    void Initialize(MouseEvent event, const cRZPoint& position, std::uint32_t keyFlags, std::int32_t wheelDelta) override {
        event_ = event; position_ = position; wheelDelta_ = wheelDelta; keyFlags_ = keyFlags;
    }
    MouseEvent EventType() const override { return event_; }
    cRZPoint Position() const override { return position_; }
    std::int32_t WheelDelta() const override { return wheelDelta_; }
    std::uint32_t KeyFlags() const override { return keyFlags_; }

private:
    MouseEvent event_ = 0xffffffff;      // +0x10
    cRZPoint position_;                  // +0x14
    std::int32_t wheelDelta_ = 0;        // +0x1c
    std::uint32_t keyFlags_ = 0;         // +0x20
};

class cGZCanvasMessage final : public cGZInputMessage<cIGZCanvasMessage, kGZIID_cIGZCanvasMessage, kCanvasMessageType> {
public:
    bool Create(std::uint32_t iid, void** out) const override;
    void Initialize(CanvasEvent event) override { event_ = event; }
    CanvasEvent EventType() const override { return event_; }

private:
    CanvasEvent event_ = 0xffffffff;     // +0x10
};

}  // namespace gz

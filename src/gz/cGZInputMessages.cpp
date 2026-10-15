#include "gz/cGZInputMessages.h"

namespace gz {

namespace {
// Create(): build a fresh message and hand back the interface asked for; on failure the new object is dropped.
template <class T>
bool CreateMessage(std::uint32_t iid, void** out) {
    T* message = new T();
    if (message->QueryInterface(iid, out)) return true;
    delete message;
    return false;
}
}  // namespace

bool cGZKeyMessage::Create(std::uint32_t iid, void** out) const { return CreateMessage<cGZKeyMessage>(iid, out); }
bool cGZMouseMessage::Create(std::uint32_t iid, void** out) const { return CreateMessage<cGZMouseMessage>(iid, out); }
bool cGZCanvasMessage::Create(std::uint32_t iid, void** out) const { return CreateMessage<cGZCanvasMessage>(iid, out); }

}  // namespace gz

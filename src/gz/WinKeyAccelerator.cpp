#include "gz/WinKeyAccelerator.h"

namespace gz {

bool cRZWinKeyAccelerator::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cIGZWinKeyAccelerator) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cRZWinKeyAccelerator::Release() {
    if (refCount_ < 2) {
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cRZWinKeyAccelerator::Init() {
    if (!initialised_) {
        cRZSysServPtr<cIGZMessageServer, kGZSID_cGZMessageServer, kGZIID_cIGZMessageServer> server;
        server_ = server.get();
        if (server_) server_->AddRef();
        initialised_ = true;
    }
    return true;
}

bool cRZWinKeyAccelerator::Shutdown() {
    if (initialised_) {
        if (server_) {
            server_->Release();
            server_ = nullptr;
        }
        initialised_ = false;
    }
    return true;
}

bool cRZWinKeyAccelerator::RegisterAccelerator(std::uint32_t key, std::uint32_t command) {
    cGZMessage message;
    message.type = command;
    return RegisterAccelerator(key, message);
}

bool cRZWinKeyAccelerator::RegisterAccelerator(std::uint32_t key, const cGZMessage& message) {
    accelerators_.emplace(key, message);     // an existing key keeps its first message
    return true;
}

bool cRZWinKeyAccelerator::UnregisterAccelerator(std::uint32_t key) { return accelerators_.erase(key) != 0; }

bool cRZWinKeyAccelerator::IsAcceleratorPresent(std::uint32_t key, bool withModifiers, bool withDirection) {
    for (const auto& [stored, message] : accelerators_) {
        std::uint32_t candidate = withModifiers ? stored : (stored & 0xfff8ffffu);
        if (!withDirection) candidate &= 0xefffffffu;
        if (candidate == key) return true;
    }
    return false;
}

bool cRZWinKeyAccelerator::TestForAccelerator(const cGZMessage& keyMessage, std::uint32_t& command) {
    cGZMessage result;
    if (!TestForAccelerator(keyMessage, result)) return false;
    command = result.type;
    return true;
}

bool cRZWinKeyAccelerator::TestForAccelerator(const cGZMessage& keyMessage, cGZMessage& result) {
    if (keyMessage.type - 5u >= 2u) return false;     // only key down (5) and key up (6) messages
    return TestForAccelerator(static_cast<std::uint32_t>(keyMessage.param1), static_cast<std::uint32_t>(keyMessage.param2),
                              keyMessage.type == 5, result);
}

bool cRZWinKeyAccelerator::TestForAccelerator(std::uint32_t key, std::uint32_t modifiers, bool keyDown, std::uint32_t& command) {
    cGZMessage result;
    if (!TestForAccelerator(key, modifiers, keyDown, result)) return false;
    command = result.type;
    return true;
}

bool cRZWinKeyAccelerator::TestForAccelerator(std::uint32_t key, std::uint32_t modifiers, bool keyDown, cGZMessage& result) {
    if (modifiers == 0xffffffffu || key - 0x10u < 3u) return false;
    const std::uint32_t composite = (modifiers & 0xffffff8fu) << 16 | key | (keyDown ? 0u : 1u << 28);
    auto it = accelerators_.find(composite);
    if (it == accelerators_.end()) return false;
    result = it->second;
    return true;
}

int cRZWinKeyAccelerator::GetKeysForAccelerator(std::uint32_t command, std::uint32_t* keys, std::uint32_t& count) {
    const std::uint32_t capacity = count;
    count = 0;
    int matches = 0;
    for (const auto& [key, message] : accelerators_) {
        if (message.type != command) continue;
        ++matches;
        if (count < capacity) keys[count++] = key;
    }
    return matches;
}

bool cRZWinKeyAccelerator::ProcessAccelerator(const cGZMessage& keyMessage) {
    cGZMessage result;
    if (!TestForAccelerator(keyMessage, result)) return false;
    if (server_) server_->MessageSend(result);
    return true;
}

}  // namespace gz

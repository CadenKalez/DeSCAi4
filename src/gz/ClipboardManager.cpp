#include "gz/ClipboardManager.h"

static cIGZMessageServer* g_clipboardServer = nullptr;
void SetClipboardMessageServer(cIGZMessageServer* server) { g_clipboardServer = server; }

// ---- one clipboard -----------------------------------------------------------------------------------

bool cGZClipboardManagerClipboard::QueryInterface(std::uint32_t iid, void** out) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (iid != 1 && iid != kGZIID_cIGZClipboard) return false;
    *out = this;
    ++refCount_;
    return true;
}

std::uint32_t cGZClipboardManagerClipboard::AddRef() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return ++refCount_;
}

std::uint32_t cGZClipboardManagerClipboard::Release() {
    std::unique_lock<std::recursive_mutex> lock(lock_);
    if (refCount_ < 2) {
        lock.unlock();
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cGZClipboardManagerClipboard::Init() {
    std::unique_lock<std::recursive_mutex> lock(lock_);
    if (!initialised_) {
        initialised_ = true;
        lock.unlock();
        SendChangeNotification(true);
        lock.lock();
    }
    return true;
}

bool cGZClipboardManagerClipboard::Shutdown() {
    std::unique_lock<std::recursive_mutex> lock(lock_);
    if (initialised_) {
        lock.unlock();
        SendChangeNotification(false);
        lock.lock();
        Clear(kClipboardAll);
        initialised_ = false;
    }
    return true;
}

void cGZClipboardManagerClipboard::SendChangeNotification(bool initialised) {
    if (!notify_) return;
    cGZMessage message{kMessageClipboardChanged, reinterpret_cast<std::uint64_t>(this), id_, initialised ? 1u : 0u};
    if (g_clipboardServer) g_clipboardServer->MessageSend(message);
}

void cGZClipboardManagerClipboard::PostDataChangeNotification(std::uint32_t type) {
    if (!notify_ || type == 0 || nestedChange_ != 0) return;
    cGZMessage message{kMessageClipboardChanged, reinterpret_cast<std::uint64_t>(this), id_, type};
    if (g_clipboardServer) g_clipboardServer->MessagePost(message, false);
}

bool cGZClipboardManagerClipboard::IsClipboardDataTypeAvailable(std::uint32_t type) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return data_.count(type) != 0;
}

std::uint32_t cGZClipboardManagerClipboard::GetAvailableClipboardDataTypes(std::uint32_t* out, std::uint32_t& count) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const std::uint32_t total = static_cast<std::uint32_t>(data_.size());
    if (!out) {
        count = total;
        return total;
    }
    if (count > total) count = total;
    std::uint32_t written = 0;
    for (const auto& entry : data_) {
        if (written >= count) break;
        out[written++] = entry.first;
    }
    return count;
}

cIGZClipboardData* cGZClipboardManagerClipboard::GetClipboardData(std::uint32_t type) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto found = data_.find(type);
    return found == data_.end() ? nullptr : found->second;
}

bool cGZClipboardManagerClipboard::GetClipboardData(std::uint32_t type, std::uint32_t iid, void** out) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto found = data_.find(type);
    return found == data_.end() ? false : found->second->QueryInterface(iid, out);
}

bool cGZClipboardManagerClipboard::SetClipboardData(cIGZClipboardData* data, bool replaceAll) {
    std::unique_lock<std::recursive_mutex> lock(lock_);
    std::uint32_t type = 0;
    bool cleared = false;
    if (replaceAll && !data_.empty()) {
        ++nestedChange_;
        Clear(kClipboardAll);
        --nestedChange_;
        type = kClipboardAll;
        cleared = true;
    }
    if (!data) {
        if (!cleared) return true;
    } else {
        type = data->GetDataType();
        if (!data_.empty()) {
            ++nestedChange_;
            Clear(type);
            --nestedChange_;
        }
        data->AddRef();
        data_.emplace(type, data);
    }
    ++changeCount_;
    lock.unlock();
    PostDataChangeNotification(type);
    return true;
}

void cGZClipboardManagerClipboard::Clear(std::uint32_t type) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (data_.empty()) return;
    if (type == kClipboardAll) {
        for (auto& entry : data_) entry.second->Release();
        data_.clear();
        ++changeCount_;
        PostDataChangeNotification(kClipboardAll);
        return;
    }
    auto found = data_.find(type);
    if (found == data_.end()) return;
    found->second->Release();
    data_.erase(found);
    ++changeCount_;
    PostDataChangeNotification(type);
}

bool cGZClipboardManagerClipboard::GetClipboardText(cIGZString& out) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    cIGZClipboardData* data = GetClipboardData(kClipboardDataText);
    if (!data) return false;
    cIGZUnknown* held = data->GetData();
    if (!held) return false;
    void* raw = nullptr;
    if (!held->QueryInterface(kGZIID_cIGZString, &raw)) return false;
    auto* text = static_cast<cIGZString*>(raw);
    out.Copy(*text);
    text->Release();
    return true;
}

bool cGZClipboardManagerClipboard::SetClipboardText(const cIGZString& text, bool replaceAll) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    auto* copy = new cRZString(text);
    copy->AddRef();
    auto* data = new cGZClipboardManagerClipboardData(kClipboardDataText, copy);
    copy->AddRef();                      // the data object's own reference
    data->AddRef();
    const bool ok = SetClipboardData(data, replaceAll);
    data->Release();
    copy->Release();
    return ok;
}

// ---- the manager -------------------------------------------------------------------------------------

bool cGZClipboardManager::QueryInterface(std::uint32_t iid, void** out) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (iid == kGZIID_cIGZClipboardManager) {
        *out = static_cast<cIGZClipboardManager*>(this);
        ++refCount_;
        return true;
    }
    return cRZSystemService::QueryInterface(iid, out);
}

std::uint32_t cGZClipboardManager::AddRef() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return ++refCount_;
}

std::uint32_t cGZClipboardManager::Release() {
    std::unique_lock<std::recursive_mutex> lock(lock_);
    if (refCount_ < 2) {
        lock.unlock();
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cGZClipboardManager::Init() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (!initialised_) {
        initialised_ = true;
        auto* clipboard = new cGZClipboardManagerClipboard();
        clipboard->AddRef();
        clipboard->SetID(0);
        clipboard->Init();
        clipboards_.emplace(0, clipboard);
    }
    return true;
}

bool cGZClipboardManager::Shutdown() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (initialised_) {
        initialised_ = false;
        RemoveClipboard(kClipboardAll);
    }
    return true;
}

std::uint32_t cGZClipboardManager::GetClipboardCount() {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return static_cast<std::uint32_t>(clipboards_.size());
}

cIGZClipboard* cGZClipboardManager::Lookup(std::uint32_t id) {
    auto found = clipboards_.find(id);
    return found == clipboards_.end() ? nullptr : found->second;
}

cIGZClipboard* cGZClipboardManager::GetClipboard(std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    return Lookup(id);
}

std::uint32_t cGZClipboardManager::GetClipboards(cIGZClipboard** out, std::uint32_t& count) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const std::uint32_t total = static_cast<std::uint32_t>(clipboards_.size());
    if (!out) {
        count = total;
        return total;
    }
    if (count > total) count = total;
    std::uint32_t written = 0;
    for (auto& entry : clipboards_) {
        if (written >= count) break;
        entry.second->AddRef();
        out[written++] = entry.second;
    }
    return count;
}

bool cGZClipboardManager::AddClipboard(cIGZClipboard* clipboard) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    const std::uint32_t id = clipboard->GetID();
    RemoveClipboard(id);
    clipboard->AddRef();
    clipboard->Init();
    clipboards_.emplace(id, clipboard);
    return true;
}

bool cGZClipboardManager::RemoveClipboard(cIGZClipboard* clipboard) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto it = clipboards_.begin(); it != clipboards_.end(); ++it) {
        if (it->second == clipboard) {
            clipboard->Shutdown();
            clipboard->Release();
            clipboards_.erase(it);
            return true;
        }
    }
    return false;
}

bool cGZClipboardManager::RemoveClipboard(std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (id == kClipboardAll) {
        for (auto& entry : clipboards_) {
            entry.second->Shutdown();
            entry.second->Release();
        }
        clipboards_.clear();
        return true;
    }
    auto found = clipboards_.find(id);
    if (found == clipboards_.end()) return false;
    cIGZClipboard* clipboard = found->second;
    clipboard->Shutdown();
    clipboard->Release();
    clipboards_.erase(found);
    return true;
}

bool cGZClipboardManager::IsClipboardDataTypeAvailable(std::uint32_t type, std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (id == kClipboardAny) {
        for (auto& entry : clipboards_)
            if (entry.second->IsClipboardDataTypeAvailable(type)) return true;
        return false;
    }
    cIGZClipboard* clipboard = Lookup(id);
    return clipboard ? clipboard->IsClipboardDataTypeAvailable(type) : false;
}

std::uint32_t cGZClipboardManager::GetAvailableClipboardDataTypes(std::uint32_t* out, std::uint32_t& count, std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (id == kClipboardAll) {
        std::uint32_t written = 0;
        if (!clipboards_.empty() && count != 0) {
            const std::uint32_t capacity = count;
            for (auto& entry : clipboards_) {
                std::uint32_t room = capacity - written;
                written += entry.second->GetAvailableClipboardDataTypes(out + written, room);
                if (written >= capacity) break;
            }
        }
        count = written;
        return written;
    }
    cIGZClipboard* clipboard = Lookup(id);
    return clipboard ? clipboard->GetAvailableClipboardDataTypes(out, count) : count;
}

std::uint32_t cGZClipboardManager::GetClipboardWithDataTypeAvailable(std::uint32_t type) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    for (auto& entry : clipboards_)
        if (entry.second->IsClipboardDataTypeAvailable(type)) return entry.first;
    return kClipboardNone;
}

bool cGZClipboardManager::GetClipboardData(std::uint32_t type, std::uint32_t iid, void** out, std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (id == kClipboardAny) {
        for (auto& entry : clipboards_)
            if (entry.second->GetClipboardData(type, iid, out)) return true;
        return false;
    }
    cIGZClipboard* clipboard = Lookup(id);
    return clipboard ? clipboard->GetClipboardData(type, iid, out) : false;
}

bool cGZClipboardManager::SetClipboardData(cIGZClipboardData* data, bool replaceAll, std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (id == kClipboardAll) {
        for (auto& entry : clipboards_) entry.second->SetClipboardData(data, replaceAll);
        return false;
    }
    cIGZClipboard* clipboard = Lookup(id);
    return clipboard ? clipboard->SetClipboardData(data, replaceAll) : false;
}

bool cGZClipboardManager::ClearClipboard(std::uint32_t type, std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (id == kClipboardAll) {
        for (auto& entry : clipboards_) entry.second->Clear(type);
        return false;
    }
    cIGZClipboard* clipboard = Lookup(id);
    if (!clipboard) return false;
    clipboard->Clear(type);
    return true;
}

bool cGZClipboardManager::GetClipboardText(cIGZString& out, std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (id == kClipboardAny) {
        for (auto& entry : clipboards_)
            if (entry.second->GetClipboardText(out)) return true;
        return false;
    }
    cIGZClipboard* clipboard = Lookup(id);
    return clipboard ? clipboard->GetClipboardText(out) : false;
}

bool cGZClipboardManager::SetClipboardText(const cIGZString& text, bool replaceAll, std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (id == kClipboardAll) {
        for (auto& entry : clipboards_) entry.second->SetClipboardText(text, replaceAll);
        return false;
    }
    cIGZClipboard* clipboard = Lookup(id);
    return clipboard ? clipboard->SetClipboardText(text, replaceAll) : false;
}

bool cGZClipboardManager::GetClipboardText(void* buffer, std::uint32_t& size, std::uint32_t codePage, std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (id == kClipboardAny) {
        for (auto& entry : clipboards_)
            if (entry.second->GetClipboardText(buffer, size, codePage)) return true;
        return false;
    }
    cIGZClipboard* clipboard = Lookup(id);
    return clipboard ? clipboard->GetClipboardText(buffer, size, codePage) : false;
}

bool cGZClipboardManager::SetClipboardText(void* buffer, std::uint32_t size, std::uint32_t codePage, bool replaceAll,
                                           std::uint32_t id) {
    std::lock_guard<std::recursive_mutex> guard(lock_);
    if (id == kClipboardAll) {
        for (auto& entry : clipboards_) entry.second->SetClipboardText(buffer, size, codePage, true);
        return false;
    }
    cIGZClipboard* clipboard = Lookup(id);
    return clipboard ? clipboard->SetClipboardText(buffer, size, codePage, replaceAll) : false;
}

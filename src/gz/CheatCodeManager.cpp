#include "gz/CheatCodeManager.h"

#include <algorithm>
#include <cstring>

#include "rz/cRZDate.h"
#include "rz/cRZFastCompression3.h"
#include "rz/cRZMessage2Standard.h"

namespace {
std::string Lower(const cIGZString& text) {
    cRZString copy(text);
    copy.MakeLower();
    return std::string(copy.Data(), copy.Strlen());
}
}  // namespace

cGZCheatCodeManager::cGZCheatCodeManager() : cRZSystemService(kGZSID_cGZCheatCodeManager, 0x1312d0) {
    SetEnableDisableStrings(cRZString("enable"), cRZString("disable"));
}

cGZCheatCodeManager::~cGZCheatCodeManager() = default;

bool cGZCheatCodeManager::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cIGZCheatCodeManager) return cRZSystemService::QueryInterface(iid, out);
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cGZCheatCodeManager::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

// ---- registration and listeners ---------------------------------------------------------------------

bool cGZCheatCodeManager::RegisterCheatCode(std::uint32_t id, const cIGZString& text) {
    for (const StringAssociation& cheat : cheats_)
        if (text.IsEqualToStringRef(cheat.text, true)) return false;
    cheats_.push_back({cRZString(text), id, true});
    return true;
}

bool cGZCheatCodeManager::UnregisterCheatCode(std::uint32_t id) {
    for (auto it = cheats_.begin(); it != cheats_.end(); ++it) {
        if (it->id == id) {
            cheats_.erase(it);
            return true;
        }
    }
    return false;
}

bool cGZCheatCodeManager::AddNotification(cIGZMessageTarget* target, std::uint32_t id) {
    for (const Listener& l : listeners_)
        if (l.id == id && l.target == target) return false;
    listeners_.push_back({target, id});
    target->AddRef();
    return true;
}

bool cGZCheatCodeManager::RemoveNotification(cIGZMessageTarget* target, std::uint32_t id) {
    for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
        if (it->id == id && it->target == target) {
            listeners_.erase(it);
            target->Release();
            return true;
        }
    }
    return false;
}

bool cGZCheatCodeManager::AddNotification2(cIGZMessageTarget2* target, std::uint32_t id) {
    for (const Listener2& l : listeners2_)
        if (l.id == id && l.target == target) return false;
    listeners2_.push_back({target, id});
    target->AddRef();
    return true;
}

bool cGZCheatCodeManager::RemoveNotification2(cIGZMessageTarget2* target, std::uint32_t id) {
    for (auto it = listeners2_.begin(); it != listeners2_.end(); ++it) {
        if (it->id == id && it->target == target) {
            listeners2_.erase(it);
            target->Release();
            return true;
        }
    }
    return false;
}

// ---- history, states and persistence ----------------------------------------------------------------

void cGZCheatCodeManager::AddCheatCodeHistoryRecord(std::uint32_t id, std::uint32_t julianDay, std::uint32_t kind,
                                                    std::uint32_t extra) {
    if (historyEnabled_) history_.push_back({id, julianDay, kind, extra});
}

std::uint32_t cGZCheatCodeManager::GetListOfAllCheatStrings(const cIGZString** out, std::uint32_t& count) const {
    const std::uint32_t capacity = count;
    if (capacity == 0) return static_cast<std::uint32_t>(cheats_.size());
    count = 0;
    for (const StringAssociation& cheat : cheats_) {
        out[count++] = &cheat.text;
        if (count >= capacity) break;
    }
    return count;
}

int cGZCheatCodeManager::HasUserUsedCheat(std::uint32_t id) const {
    int uses = 0;
    for (const CheatCodeHistoryRecord& record : history_)
        if (record.cheatId == id && record.kind == 0) ++uses;
    return uses;
}

bool cGZCheatCodeManager::SetCheatState(std::uint32_t id, bool on) {
    const cRZDate today;
    std::uint32_t kind;
    if (on) {
        states_.insert(id);
        kind = 1;
    } else {
        states_.erase(id);
        kind = 2;
    }
    AddCheatCodeHistoryRecord(id, today.JulianDay(), kind, 0);
    return true;
}

std::uint32_t cGZCheatCodeManager::GetCheatsWithEnabledStates(std::uint32_t* out, std::uint32_t& count) const {
    if (!out) {
        count = static_cast<std::uint32_t>(states_.size());
        return count;
    }
    count = std::min<std::uint32_t>(count, static_cast<std::uint32_t>(states_.size()));
    std::uint32_t written = 0;
    for (std::uint32_t id : states_) {
        if (written >= count) break;
        out[written++] = id;
    }
    return count;
}

bool cGZCheatCodeManager::SetStateShouldBeSaved(std::uint32_t id, bool save) {
    for (StringAssociation& cheat : cheats_) {
        if (cheat.id == id) {
            cheat.shouldBeSaved = save;
            return true;
        }
    }
    return false;
}

bool cGZCheatCodeManager::GetCheatCodeSaveData(char* buffer, std::uint32_t& size) const {
    const std::uint32_t passwordSize = static_cast<std::uint32_t>(password_.size());
    const std::uint32_t padded = (passwordSize - 1u) | 3u;                    // size rounded up to 4, minus one
    const std::uint32_t historyBytes = static_cast<std::uint32_t>(history_.size() * 16);
    const std::uint32_t enabled = static_cast<std::uint32_t>(states_.size());
    size = padded + historyBytes + 0x15 + enabled * 4;
    if (!buffer) return true;
    auto put = [&](std::uint32_t offset, std::uint32_t value) { std::memcpy(buffer + offset, &value, 4); };
    put(0, size);
    put(4, padded + 1);
    put(8, passwordSize);
    std::memset(buffer + 12, 0, padded + 1);
    if (!password_.empty()) std::memcpy(buffer + 12, password_.data(), passwordSize);
    std::uint32_t at = 12 + padded + 1;
    put(at, historyBytes);
    at += 4;
    for (const CheatCodeHistoryRecord& record : history_) {
        std::memcpy(buffer + at, &record, 16);
        at += 16;
    }
    put(at, enabled * 4);
    at += 4;
    for (std::uint32_t id : states_) {
        put(at, id);
        at += 4;
    }
    return true;
}

bool cGZCheatCodeManager::SetCheatCodeSaveData(const char* data, std::uint32_t size) {
    if (!data || size <= 3) return true;
    auto get = [&](std::uint32_t offset) {
        std::uint32_t value = 0;
        if (offset + 4 <= size) std::memcpy(&value, data + offset, 4);
        return value;
    };
    const std::uint32_t padded = get(4), passwordSize = get(8);
    if (12ull + passwordSize > size) return true;     // truncated block
    password_.assign(reinterpret_cast<const std::uint8_t*>(data) + 12,
                     reinterpret_cast<const std::uint8_t*>(data) + 12 + passwordSize);
    std::uint32_t at = 12 + padded;
    const std::uint32_t historyCount = get(at) >> 4;
    at += 4;
    for (std::uint32_t i = 0; i < historyCount; ++i, at += 16) {
        CheatCodeHistoryRecord record{get(at), get(at + 4), get(at + 8), get(at + 12)};
        history_.push_back(record);
    }
    const std::uint32_t enabledBytes = get(at);
    at += 4;
    for (std::uint32_t i = 0; i < enabledBytes / 4 && at + 4 <= size; ++i, at += 4) states_.insert(get(at));
    return true;
}

// ---- notifications and matching ---------------------------------------------------------------------

bool cGZCheatCodeManager::SendCheatNotifications(const cIGZString& text, std::uint32_t id) {
    if (!cheatCodesEnabled_) return false;
    for (Listener& l : listeners_) {
        if (l.id == id || l.id == 0) {
            cGZMessage message{kMessageCheatCodeTyped, id, reinterpret_cast<std::uint64_t>(&text), 0};
            l.target->DoMessage(message);
        }
    }
    for (Listener2& l : listeners2_) {
        if (l.id == id || l.id == 0) {
            auto* message = new cRZMessage2Standard();
            message->AddRef();
            message->SetType(kMessageCheatCodeTyped);
            message->SetData1(id);
            message->SetVoid2(const_cast<cIGZString*>(&text));
            l.target->DoMessage(message);
            message->Release();
        }
    }
    return false;
}

int cGZCheatCodeManager::DoesCheatCodeMatch(const cIGZString& input, std::uint32_t& id) const {
    id = 0;
    if (!cheatCodesEnabled_ || input.Strlen() == 0) return 0;
    const std::string typed = Lower(input);
    for (const StringAssociation& cheat : cheats_) {
        const std::string word = Lower(cheat.text);
        const bool found = word.empty() || typed.compare(0, word.size(), word) == 0;
        if (found) {
            id = cheat.id;
            return typed.size() == word.size() ? 2 : 1;
        }
    }
    return 0;
}

bool cGZCheatCodeManager::DoDefaultCheatCodeProcessing(const cIGZString& input, std::uint32_t& id) {
    id = 0;
    if (cheatCodesEnabled_ && DoesCheatCodeMatch(input, id) != 0) {
        const cRZDate today;
        SendCheatNotifications(input, id);
        AddCheatCodeHistoryRecord(id, today.JulianDay(), 0, 0);
        return true;
    }
    if (enableWord_.Strlen() == 0 && disableWord_.Strlen() == 0) return false;
    cRZString typed(input);
    typed.Trim();
    typed.MakeLower();
    const std::string line(typed.Data(), typed.Strlen());
    // The word that follows the command is what the original passes on as the password argument, but its
    // substr() call is fed the wrong position, so the argument is always empty.
    auto handOver = [&](std::size_t wordLength, bool enable) {
        const std::string rest = line.substr(wordLength);
        const std::string argument = rest.substr(line.size() - wordLength);
        cRZString password(argument.c_str());
        password.LTrim();
        EnableCheatCodes(enable, password);
        return true;
    };
    const std::string enable(enableWord_.Data(), enableWord_.Strlen()), disable(disableWord_.Data(), disableWord_.Strlen());
    if (enable.empty() || line.rfind(enable, 0) == 0) return handOver(enable.size(), true);
    if (disable.empty() || line.rfind(disable, 0) == 0) return handOver(disable.size(), false);
    return false;
}

// ---- enabling / password ----------------------------------------------------------------------------

bool cGZCheatCodeManager::EnableCheatCodes(bool enable, const cIGZString& password) {
    if (password.Strlen() > 0x40) return false;
    cRZFastCompression3 codec;
    if (enable) {
        if (cheatCodesEnabled_) return false;
        if (!password_.empty()) {
            const std::uint32_t storedLength = codec.GetLengthOfDecompressedData(password_.data());
            if (password.Strlen() != storedLength) return false;
            if (storedLength <= 0x40) {
                std::uint8_t decoded[0x41];
                std::uint32_t capacity = sizeof(decoded);
                std::uint32_t total;
                std::memcpy(&total, password_.data(), 4);
                codec.DecompressData(password_.data(), total, decoded, capacity);
                if (capacity != 0 && std::memcmp(password.Data(), decoded, capacity) != 0) return false;
            }
            password_.clear();
        }
        cheatCodesEnabled_ = true;
        SendCheatNotifications(cRZString(), kMessageCheatCodesEnabled);
    } else {
        if (!cheatCodesEnabled_) return false;
        if (password.Strlen() != 0) {
            std::vector<std::uint8_t> block(codec.GetMaxLengthRequiredForCompressedData(password.Strlen()));
            std::uint32_t length = 0;
            codec.CompressData(password.Data(), password.Strlen(), block.data(), length);
            block.resize(length);
            password_ = std::move(block);
        }
        SendCheatNotifications(cRZString(), kMessageCheatCodesDisabled);
        cheatCodesEnabled_ = false;
    }
    return true;
}

bool cGZCheatCodeManager::GetCheatCodePassword(std::uint8_t* buffer, std::uint32_t& size) const {
    const std::uint32_t required = password_.empty() ? 0 : static_cast<std::uint32_t>(password_.size());
    bool ok = true;
    if (!password_.empty() && buffer) {
        if (size < required) ok = false;
        else std::memcpy(buffer, password_.data(), required);
    }
    size = required;
    return ok;
}

bool cGZCheatCodeManager::SetCheatCodePassword(const std::uint8_t* buffer, std::uint32_t size) {
    password_.clear();
    if (!buffer) return true;
    std::uint32_t total = 0;
    if (size >= 4) std::memcpy(&total, buffer, 4);
    if (total != size) return false;
    password_.assign(buffer, buffer + size);
    cheatCodesEnabled_ = true;
    return true;
}

bool cGZCheatCodeManager::SetEnableDisableStrings(const cIGZString& enableWord, const cIGZString& disableWord) {
    enableWord_.AssignFromIGZString(enableWord);
    enableWord_.MakeLower();
    disableWord_.AssignFromIGZString(disableWord);
    disableWord_.MakeLower();
    return true;
}

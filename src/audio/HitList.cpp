#include "audio/HitList.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>

#include "audio/HitManAccess.h"
#include "rz/RZConvert.h"
#include "rz/cRZString.h"

namespace {
std::atomic<std::int32_t> g_numHitLists{0};
}

// cHitList::cHitList() @ 0x100481d20 / 0x100481d7c (two identical copies in the binary)
cHitList::cHitList() { ++g_numHitLists; }

cHitList::cHitList(const cHitList& other)
    : cIHitList(), refCount_(other.refCount_), available_(other.available_), recent_(other.recent_),
      criterion_(other.criterion_), criterionExplicit_(other.criterionExplicit_) {
    ++g_numHitLists;
}

// cHitList::~cHitList() @ 0x100481dca / 0x100481e02 / 0x100481e3a
cHitList::~cHitList() { --g_numHitLists; }

std::int32_t cHitList::LiveListCount() noexcept { return g_numHitLists.load(); }

// cHitList::QueryInterface(unsigned int, void**) @ 0x100481ea4
bool cHitList::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIHitList || iid == kGZIID_cIGZUnknown) {
        *out = this;
        AddRef();
        return true;
    }
    return false;
}

// cHitList::Release() @ 0x100481e84
std::uint32_t cHitList::Release() {
    if (refCount_ < 2) {
        delete this;
        return 0;
    }
    return static_cast<std::uint32_t>(--refCount_);
}

// cHitList::Add(int) @ 0x100481fcc
bool cHitList::AddValue(std::int32_t value) {
    available_.push_back(value);
    RecomputeAutoCriterion();
    return true;
}

// cHitList::Remove(int) @ 0x100481ec6 — removes every copy from both lists.
bool cHitList::Remove(std::int32_t value) {
    available_.erase(std::remove(available_.begin(), available_.end(), value), available_.end());
    recent_.erase(std::remove(recent_.begin(), recent_.end(), value), recent_.end());
    RecomputeAutoCriterion();
    return true;
}

// cHitList::RemoveAll() @ 0x10048202a
bool cHitList::RemoveAll() {
    available_.clear();
    recent_.clear();
    return true;
}

// cHitList::SetCriterion(int) @ 0x100482934
bool cHitList::SetCriterion(std::int32_t historyLength) {
    criterion_ = historyLength;
    criterionExplicit_ = true;
    return true;
}

// cHitList::Random(int) @ 0x100482192 — goes through the Hit manager's RNG.
std::int32_t cHitList::PickRandomIndex(std::int32_t range) { return RZ::HM()->Random(range, 0); }

// cHitList::Choose() @ 0x100482052
std::int32_t cHitList::Choose() {
    if (available_.empty()) {
        if (recent_.empty()) return 0;  // original unlinks its own list head here; value read is 0
        const std::int32_t value = recent_.front();  // everything is "recent": recycle the oldest
        recent_.pop_front();
        recent_.push_back(value);
        return value;
    }
    const auto index = static_cast<std::size_t>(PickRandomIndex(static_cast<std::int32_t>(available_.size())));
    const std::int32_t value = available_[index];
    available_.erase(available_.begin() + static_cast<std::ptrdiff_t>(index));
    recent_.push_back(value);
    while (criterion_ < static_cast<std::int32_t>(recent_.size())) {  // age the oldest picks back in
        available_.push_back(recent_.front());
        recent_.pop_front();
    }
    return value;
}

const std::int32_t* cHitList::ElementAt(std::uint32_t index) const {
    if (index < available_.size()) return &available_[index];
    const std::size_t i = index - available_.size();
    return i < recent_.size() ? &recent_[i] : nullptr;
}
std::int32_t* cHitList::ElementAt(std::uint32_t index) {
    return const_cast<std::int32_t*>(static_cast<const cHitList*>(this)->ElementAt(index));
}

// cHitList::GetValueByIndex(unsigned int, int*) const @ 0x10048220a
bool cHitList::GetIntValueByIndex(std::uint32_t index, std::int32_t* out) const {
    const std::int32_t* e = ElementAt(index);
    if (!e) return false;
    *out = *e;
    return true;
}

// cHitList::SetValueByIndex(unsigned int, int) @ 0x10048226e
bool cHitList::SetIntValueByIndex(std::uint32_t index, std::int32_t value) {
    std::int32_t* e = ElementAt(index);
    if (!e) return false;
    *e = value;
    return true;
}

// cHitList::RemoveByIndex(unsigned int) @ 0x1004822d0 (does not touch the auto criterion)
bool cHitList::RemoveByIndex(std::uint32_t index) {
    if (index < available_.size()) {
        available_.erase(available_.begin() + index);
        return true;
    }
    const std::size_t i = index - available_.size();
    if (i >= recent_.size()) return false;
    recent_.erase(recent_.begin() + static_cast<std::ptrdiff_t>(i));
    return true;
}

// cHitList::ReadResource(cIGZIStream&, unsigned int, cIGZUnknown*) @ 0x100482366
bool cHitList::ReadResourceBytes(const std::uint8_t* data, std::uint32_t size) {
    auto dword = [&](std::uint32_t i) {
        std::uint32_t v = 0;
        if ((i + 1) * 4 <= size) std::memcpy(&v, data + i * 4, 4);
        return v;
    };
    std::vector<std::int32_t> values;
    const std::uint32_t first = size >= 4 ? dword(0) : 0;
    const std::uint32_t second = size >= 8 ? dword(1) : 0;

    if (size >= 4 && size == first + 4) {  // text: "id,lo-hi,..."
        std::string raw(reinterpret_cast<const char*>(data) + 4, size - 4);
        raw.resize(std::strlen(raw.c_str()));  // the original reads until the first NUL
        cRZString rest(raw.c_str());
        while (rest.Strlen() != 0) {
            cRZString token = rest.SplitToken(",");
            if (token.Strlen() == 0) {  // no comma left: the remainder is the last token
                token.str() = rest.str();
                rest.str().clear();
            }
            cRZString lo = token.SplitToken("-");
            if (lo.Strlen() == 0) {
                values.push_back(static_cast<std::int32_t>(RZ::ConvertToUint(token.ToChar())));
            } else {
                const std::uint32_t from = RZ::ConvertToUint(lo.ToChar());
                const std::uint32_t to = RZ::ConvertToUint(token.ToChar());
                for (std::uint32_t v = from; v <= to; ++v) {
                    values.push_back(static_cast<std::int32_t>(v));
                    if (v == 0xFFFFFFFFu) break;
                }
            }
        }
    } else if (size >= 8 && first == 1 && size == second * 4 + 8) {  // v1
        for (std::uint32_t i = 0; i < second; ++i) values.push_back(static_cast<std::int32_t>(dword(2 + i)));
    } else if (size >= 4 && size == first * 4 + 4) {  // v2
        for (std::uint32_t i = 0; i < first; ++i) values.push_back(static_cast<std::int32_t>(dword(1 + i)));
    } else {
        return false;
    }
    for (const std::int32_t v : values) available_.push_back(v);
    SetCriterion(static_cast<std::int32_t>(available_.size() / 3 + 1));
    return true;
}

// cHitList::WriteResource(cIGZOStream&, unsigned int, cIGZUnknown*) @ 0x100482812
void cHitList::WriteResourceDwords(std::vector<std::uint32_t>& out) const {
    out.push_back(1);
    out.push_back(static_cast<std::uint32_t>(available_.size() + recent_.size()));
    for (const std::int32_t v : available_) out.push_back(static_cast<std::uint32_t>(v));
    for (const std::int32_t v : recent_) out.push_back(static_cast<std::uint32_t>(v));
}

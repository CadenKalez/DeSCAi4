#include "sc4/WealthRequester.h"

namespace sc4 {

bool cSC4WealthRequester::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == 1 || iid == kGZIID_cISC4PropRequester) *out = static_cast<cISC4PropRequester*>(this);
    else if (iid == 0xe4fda3d4) *out = static_cast<cIGZSerializable*>(this);
    else return false;
    AddRef();
    return true;
}

std::uint32_t cSC4WealthRequester::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

std::uint32_t cSC4WealthRequester::SelectProp(const std::vector<std::uint32_t>& candidates, int x, int z) {
    int wealth;
    if (host_->HasLot(x, z)) {
        wealth = host_->LotWealth(x, z);
        const unsigned zone = host_->ZoneType(x, z);
        if (zone < 0x10 && ((0x9800u >> (zone & 0x1f)) & 1) != 0) wealth = 2;
    } else {
        wealth = host_->CityCellWealth(x, z);
    }
    std::vector<std::uint32_t> matches;
    matches.reserve(candidates.size());
    for (std::uint32_t prop : candidates)
        if (static_cast<char>(host_->PropWealth(prop)) == static_cast<char>(wealth)) matches.push_back(prop);
    if (matches.empty()) return 0;
    return matches[random_->RandomUint32Uniform(static_cast<std::uint32_t>(matches.size()))];
}

}  // namespace sc4

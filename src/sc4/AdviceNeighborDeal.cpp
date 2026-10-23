#include "sc4/AdviceNeighborDeal.h"

#include "sc4/TablePropertyReader.h"

namespace sc4 {

namespace {
constexpr std::uint16_t kNeighborDealStreamVersion = 2;
}

bool cSC4AdviceNeghborDeal::Init(int adviceId) {
    const bool baseOk = cSC4Advice::Init(adviceId);
    cTablePropertyReader reader(AdviceHost()->Lua(), "advices", adviceId_);
    if (!reader.IsValid()) return false;
    dealType_ = static_cast<int>(reader.GetNumber("nd_type"));
    return baseOk;
}

bool cSC4AdviceNeghborDeal::Clone(cISC4Advice** out) {
    if (!cSC4Advice::Clone(out)) return false;
    void* iface = nullptr;
    (*out)->QueryInterface(kGZIID_cISC4AdviceNeighborDeal, &iface);   // the interface is the primary pointer
    auto* clone = static_cast<cSC4AdviceNeghborDeal*>(static_cast<cISC4Advice*>(iface));
    clone->SetDealType(GetDealType());
    clone->Release();   // drops the reference taken by the query
    return true;
}

bool cSC4AdviceNeghborDeal::Write(cIGZOStream& stream) const {
    stream.SetUint16(kNeighborDealStreamVersion);
    cSC4Advice::Write(stream);
    stream.SetSint32(dealType_);
    return !stream.GetError();
}

bool cSC4AdviceNeghborDeal::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != kNeighborDealStreamVersion) return false;
    cSC4Advice::Read(stream);
    stream.GetSint32(dealType_);
    return !stream.GetError();
}

bool cSC4AdviceNeghborDeal::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISC4AdviceNeighborDeal) {
        *out = static_cast<cISC4Advice*>(this);
        AddRef();
        return true;
    }
    return cSC4Advice::QueryInterface(iid, out);
}

}  // namespace sc4

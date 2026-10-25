// GZCOM creator for sc4::cSC4CitySituationAdvice. In the original the class derives from cSC4Advice; the port keeps the
// advice as the object the situation reaches through ISituationAdviceBase, so the creator pairs one cSC4Advice with it.
#include "gz/GZCOM.h"
#include "sc4/Advice.h"
#include "sc4/CitySituationAdvice.h"

namespace gzcom {
namespace {

class AdviceBase final : public sc4::ISituationAdviceBase {
public:
    explicit AdviceBase(sc4::cSC4Advice* advice) : advice_(advice) { advice_->AddRef(); }
    ~AdviceBase() override { advice_->Release(); }
    bool Init(int adviceId) override { return advice_->Init(adviceId); }
    void Shutdown() override { advice_->Shutdown(); }
    int AdviceId() override { return advice_->GetGUID(); }
    int AdvisorId() override { return advice_->GetAdvisorID(); }
    void SetFlags(int flags, bool on) override { advice_->SetFlags(flags, on); }
    int Mood() override { return advice_->GetMood(); }
    int Priority() override { return advice_->GetPriority(); }
    std::uint32_t Trigger() override { return advice_->GetTrigger() ? 1u : 0u; }
    bool QueryBaseInterface(std::uint32_t iid, void** out) override { return advice_->QueryInterface(iid, out); }

private:
    sc4::cSC4Advice* advice_;
};

}  // namespace

void* GZCOM_CreatecSC4CitySituationAdvice() {
    auto* advice = new sc4::cSC4Advice();
    auto* base = new AdviceBase(advice);   // the adapter outlives the situation advice (it is never freed: PARTIAL ownership)
    return static_cast<void*>(new sc4::cSC4CitySituationAdvice(base));
}

}  // namespace gzcom

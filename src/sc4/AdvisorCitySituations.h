// cSC4AdvisorCitySituations - the advisor of the city situations (class id 0x4be372cd, object 0x148 bytes). A cSC4AdvisorBase
// whose only own behaviour is keeping its active advice set in step with the situations that are flagged event-driven
// (flag 0x20 on cISC4CitySituationAdvice 0x6beb1e8a). Recovered from SimCity 4 Deluxe Edition 1.2.1
// (0x1006a7cb4..0x1006a8042).
//
// PARTIAL: UpdateMySimAdvices first asks the city's sim-agent manager to refresh itself (SL::spCity vslot 0x238 -> 0x60 ->
// 0x220); the port reaches it through SetAdvisorCitySituationsRefresh.
#pragma once
#include <functional>

#include "sc4/AdvisorConcrete.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4AdvisorCitySituations = 0x4be372cd;
constexpr int kSituationAdviceActiveFlag = 0x20;

void SetAdvisorCitySituationsRefresh(std::function<void()> refresh);

class cSC4AdvisorCitySituations : public cSC4AdvisorBase {
public:
    ~cSC4AdvisorCitySituations() override = default;     // 0x1006a7cb4
    bool DoMessage(cIGZMessage2* message) override;      // 0x1006a7d58 (the base's)
    bool Init(int advisorType) override;                 // 0x1006a7d70
    bool Shutdown() override;                            // 0x1006a7d98
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4AdvisorCitySituations; }   // 0x1006a8042
    void UpdateMySimAdvices();                           // 0x1006a7db0
    bool RetireAdvice(cISC4Advice* advice);              // 0x1006a7f3c
};

}  // namespace sc4

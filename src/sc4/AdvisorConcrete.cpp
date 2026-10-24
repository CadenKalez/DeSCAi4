#include "sc4/AdvisorConcrete.h"

#include <cstddef>

#include "rz/cRZMessage2Standard.h"
#include "sc4/CommonIIDs.h"

namespace sc4 {

namespace {
IAdvisorConcreteHost* g_host = nullptr;
constexpr std::uint32_t kAdvisorMessageCityStarted = 0x26d31ec1;   // shared with cSC4AdvisorSystem
}  // namespace

IAdvisorConcreteHost* AdvisorConcreteHost() { return g_host; }
void SetAdvisorConcreteHost(IAdvisorConcreteHost* host) { g_host = host; }

// ---- cSC4AdvisorBase ----------------------------------------------------------------------------------------

// The original constructor runs the cSC4Advisor constructor, sets the four vtable groups, then marks the update
// pending (1) and clears the dirty mask; the subscribed flag is left false until Init.
cSC4AdvisorBase::cSC4AdvisorBase() = default;

cSC4AdvisorBase::~cSC4AdvisorBase() = default;

// Reads the advisor row and its advices (cSC4Advisor::Init), then subscribes to the five base messages once.
bool cSC4AdvisorBase::Init(int advisorType) {
    if (!cSC4Advisor::Init(advisorType)) return false;
    updatePending_ = true;
    if (!subscribed_) {
        subscribed_ = true;
        cIGZMessageServer2* messages = g_host ? g_host->Messages() : nullptr;
        if (messages) {
            messages->AddNotification(this, kAdvisorBaseMessageBuildingChanged);
            messages->AddNotification(this, kAdvisorBaseMessageBuildingChanged2);
            messages->AddNotification(this, kAdvisorMessageCityStarted);
            messages->AddNotification(this, kAdvisorBaseMessageRewardAdviceReeval);
            messages->AddNotification(this, kAdvisorBaseMessagePostUpdate2);
        }
    }
    return true;
}

bool cSC4AdvisorBase::Shutdown() {
    cSC4Advisor::Shutdown();
    if (subscribed_) {
        subscribed_ = false;
        cIGZMessageServer2* messages = g_host ? g_host->Messages() : nullptr;
        if (messages) {
            messages->RemoveNotification(this, kAdvisorBaseMessageBuildingChanged);
            messages->RemoveNotification(this, kAdvisorBaseMessageBuildingChanged2);
            messages->RemoveNotification(this, kAdvisorMessageCityStarted);
            messages->RemoveNotification(this, kAdvisorBaseMessageRewardAdviceReeval);
            messages->RemoveNotification(this, kAdvisorBaseMessagePostUpdate2);
        }
    }
    return true;
}

// The deferred self-update: a cRZMessage2Standard of type 0xca8f3db6 posted straight back to this advisor's
// message-target view (data1 = the update kind, data2 = an advice guid).
void cSC4AdvisorBase::post_self_update(std::uint32_t kind, std::uint32_t value) {
    IAdvisorConcreteHost* host = g_host;
    if (!host) return;
    cRZMessage2Standard* message = new cRZMessage2Standard();
    message->AddRef();
    message->SetType(kAdvisorBaseMessageSelfUpdate);
    message->SetData1(kind);
    message->SetData2(value);
    cIGZMessageServer2* messages = host->Messages();
    if (messages) messages->GeneralMessagePostToTarget(message, static_cast<cIGZMessageTarget2*>(this));
    message->Release();
}

// The base message routing: building-change messages re-run the building-development lookup, the deferred
// self-update (data1 1) forces the matching advice and clears its no-link flag unless the civic-building simulator
// marks it as a landmark, data1 2 re-triggers every advice carrying flag 0x1000, 0xea8ae29a re-evaluates every
// cSC4RewardAdvice, and the city-started message runs update_variables. Everything else falls through to cSC4Advisor.
bool cSC4AdvisorBase::DoMessage(cIGZMessage2* message) {
    auto* standard = static_cast<cIGZMessage2Standard*>(message);
    const std::uint32_t type = message->GetType();
    IAdvisorConcreteHost* host = g_host;
    if (type == kAdvisorBaseMessageBuildingChanged || type == kAdvisorBaseMessageBuildingChanged2) {
        if (host && host->MessageVoid1IsPropertyHolder(message)) {
            std::uint32_t guid = 0;
            if (host->BuildingDevelopmentLookup(message, &guid)) post_self_update(1, guid);
        }
    } else if (type == kAdvisorBaseMessagePostUpdate2) {
        post_self_update(2, 0);
    } else if (type == kAdvisorMessageCityStarted) {
        update_variables();
    } else if (type == kAdvisorBaseMessageRewardAdviceReeval) {
        for (const cAdviceInfo& info : AdviceInfos()) {
            if (!info.advice) continue;
            void* serializable = nullptr;
            if (info.advice->QueryInterface(kGZIID_cIGZSerializable, &serializable) && serializable) {
                if (static_cast<cIGZSerializable*>(serializable)->GetGZCLSID() == kGZCLSID_cSC4RewardAdvice) {
                    info.advice->GetTrigger();   // reward advices re-check their condition in GetTrigger
                }
                static_cast<cIGZUnknown*>(serializable)->Release();
            }
        }
    } else if (type == kAdvisorBaseMessageSelfUpdate) {
        const std::uint64_t data1 = standard->GetData1();
        if (data1 == 1) {
            const std::uint32_t data2 = static_cast<std::uint32_t>(standard->GetData2());
            cISC4Advice* advice = GetAdviceByGUID(static_cast<int>(data2));
            if (advice) {
                advice->GetTrigger();
                if (!host || host->CivicBuildingAllowsLinks(data2)) {
                    advice->SetFlags(kAdviceFlagNoLinks, true);
                    updatePending_ = true;
                }
            }
        } else if (data1 == 2) {
            for (const cAdviceInfo& info : AdviceInfos()) {
                if (info.advice && info.advice->CheckFlags(0x1000)) info.advice->GetTrigger();
            }
        }
    }
    return cSC4Advisor::DoMessage(message);
}

void cSC4AdvisorBase::update_variables() {}

// A pending update runs the (possibly overridden) update_variables once before the regular day simulation.
void cSC4AdvisorBase::simulate_day() {
    if (updatePending_) {
        update_variables();
        updatePending_ = false;
    }
    cSC4Advisor::simulate_day();
}

// Every month the variables are refreshed unconditionally.
void cSC4AdvisorBase::simulate_month() { update_variables(); }

// ---- cSC4AdvisorFinance --------------------------------------------------------------------------------------

bool cSC4AdvisorFinance::Init(int advisorType) {
    updatePending_ = true;   // the original sets the pending flag before the base Init
    return cSC4AdvisorBase::Init(advisorType);
}

bool cSC4AdvisorFinance::Shutdown() { return cSC4AdvisorBase::Shutdown(); }

bool cSC4AdvisorFinance::DoMessage(cIGZMessage2* message) {
    const std::uint32_t type = message->GetType();
    IAdvisorConcreteHost* host = g_host;
    if (type == kAdvisorBaseMessageBuildingChanged || type == kAdvisorBaseMessageBuildingChanged2) {
        if (host && host->MessageVoid1IsPropertyHolder(message)) {
            if (dirtyMask_ == 0) post_self_update(0, 0);
            dirtyMask_ |= 1;
        }
    } else if (type != kAdvisorBaseMessageSelfUpdate) {
        return cSC4AdvisorBase::DoMessage(message);
    } else {
        dirtyMask_ = 0;   // quirk kept: the set flag is never acted on, only cleared
    }
    return cSC4AdvisorBase::DoMessage(message);
}

std::uint32_t cSC4AdvisorFinance::GetGZCLSID() const { return kGZCLSID_cSC4AdvisorFinance; }

// The finance record: funds, the income/expense, the eleven tax rates, tax income, debt and the neighbour-deal
// income/expense, each read from the budget / neighbours simulators and stored as a Lua global.
void cSC4AdvisorFinance::update_variables() {
    IAdvisorConcreteHost* host = g_host;
    if (!host) return;
    host->SetGlobal("g_funds", static_cast<double>(host->BudgetInt(0x30)));
    host->SetGlobal("g_income_monthly", static_cast<double>(host->BudgetInt(0x80)));
    host->SetGlobal("g_expense_monthly", static_cast<double>(host->BudgetInt(0x70)));
    host->SetGlobal("g_tax_rate_r_low", static_cast<double>(host->BudgetTaxRate(0)));
    host->SetGlobal("g_tax_rate_r_med", static_cast<double>(host->BudgetTaxRate(1)));
    host->SetGlobal("g_tax_rate_r_high", static_cast<double>(host->BudgetTaxRate(2)));
    host->SetGlobal("g_tax_rate_cs_low", static_cast<double>(host->BudgetTaxRate(3)));
    host->SetGlobal("g_tax_rate_cs_med", static_cast<double>(host->BudgetTaxRate(4)));
    host->SetGlobal("g_tax_rate_cs_high", static_cast<double>(host->BudgetTaxRate(5)));
    host->SetGlobal("g_tax_rate_co_med", static_cast<double>(host->BudgetTaxRate(6)));
    host->SetGlobal("g_tax_rate_co_high", static_cast<double>(host->BudgetTaxRate(7)));
    host->SetGlobal("g_tax_rate_i_resource", static_cast<double>(host->BudgetTaxRate(8)));
    host->SetGlobal("g_tax_rate_i_dirty", static_cast<double>(host->BudgetTaxRate(9)));
    host->SetGlobal("g_tax_rate_i_manufacturing", static_cast<double>(host->BudgetTaxRate(10)));
    host->SetGlobal("g_tax_rate_i_hightech", static_cast<double>(host->BudgetTaxRate(11)));
    host->SetGlobal("g_tax_income", static_cast<double>(host->BudgetInt(0x90)));
    host->SetGlobal("g_borrowed", static_cast<double>(host->BudgetInt(0x148)));
    host->SetGlobal("g_borrowing_limit", static_cast<double>(host->BudgetInt(0x150)));
    host->SetGlobal("g_bond_payments_monthly", static_cast<double>(host->BudgetInt(0x168)));
    host->SetGlobal("g_nd_income", static_cast<double>(host->NeighborsInt(0x100)));
    host->SetGlobal("g_nd_expense", static_cast<double>(host->NeighborsInt(0x108)));
}

// ---- cSC4AdvisorHQ -------------------------------------------------------------------------------------------

bool cSC4AdvisorHQ::Init(int advisorType) {
    if (!cSC4AdvisorBase::Init(advisorType)) return false;
    updatePending_ = true;
    if (!groupsInitialized_) {
        groupsInitialized_ = true;
        // The original fills the file-scope occupant group vectors (_kElementarySchoolsGroupVector, ...) with the
        // occupant-type ids of each civic group here; those id arrays are unrecovered, so the vectors stay empty.
    }
    return true;
}

bool cSC4AdvisorHQ::Shutdown() {
    if (groupsInitialized_) {
        groupsInitialized_ = false;
        // The original clears the ten file-scope occupant group vectors here.
    }
    return cSC4AdvisorBase::Shutdown();
}

bool cSC4AdvisorHQ::DoMessage(cIGZMessage2* message) {
    const std::uint32_t type = message->GetType();
    IAdvisorConcreteHost* host = g_host;
    if (type == kAdvisorBaseMessageBuildingChanged || type == kAdvisorBaseMessageBuildingChanged2) {
        if (host && host->MessageVoid1IsPropertyHolder(message)) {
            if (dirtyMask_ == 0) post_self_update(0, 0);
            dirtyMask_ |= 1;
        }
    } else if (type != kAdvisorBaseMessageSelfUpdate) {
        return cSC4AdvisorBase::DoMessage(message);
    } else {
        if ((dirtyMask_ & 1) != 0) update_occupant_variables();
        dirtyMask_ = 0;
    }
    return cSC4AdvisorBase::DoMessage(message);
}

std::uint32_t cSC4AdvisorHQ::GetGZCLSID() const { return kGZCLSID_cSC4AdvisorHQ; }

// Health and education: funding from the budget, life expectancy / health / education from the residential
// simulator, the best/worst tract values as location-bearing locals, then the occupant walk.
void cSC4AdvisorHQ::update_variables() {
    IAdvisorConcreteHost* host = g_host;
    if (!host) return;
    host->SetGlobal("g_health_funding_p", static_cast<double>(host->BudgetLineItemFunding(0x9188f42) * 100.0f));
    host->SetGlobal("ga_life_exp", static_cast<double>(host->ResidentialInt(0x110)));
    host->SetGlobal("ga_health", static_cast<double>(host->ResidentialFloat(0x108)));
    host->SetGlobal("ga_education", static_cast<double>(host->ResidentialFloat(0x100)));
    float bestValue = 0, worstValue = 0;
    int bestX = 0, bestZ = 0, worstX = 0, worstZ = 0;
    host->ResidentialTracts(0x160, bestValue, bestX, bestZ, worstValue, worstX, worstZ);
    cISC4AdviceSubject* subject = nullptr;
    if (host->CreateAdviceSubject(&subject) && subject) {
        subject->SetLocation(static_cast<float>(bestX), static_cast<float>(bestZ));
        host->SetLocal("l_tract_life_exp_l", static_cast<double>(bestValue), subject);
        subject->Release();
    }
    subject = nullptr;
    if (host->CreateAdviceSubject(&subject) && subject) {
        subject->SetLocation(static_cast<float>(bestX), static_cast<float>(bestZ));
        host->SetLocal("l_tract_hq_l", static_cast<double>(bestValue), subject);
        subject->Release();
    }
    subject = nullptr;
    if (host->CreateAdviceSubject(&subject) && subject) {
        subject->SetLocation(static_cast<float>(worstX), static_cast<float>(worstZ));
        host->SetLocal("l_tract_life_exp_h", static_cast<double>(worstValue), subject);
        subject->Release();
    }
    subject = nullptr;
    if (host->CreateAdviceSubject(&subject) && subject) {
        subject->SetLocation(static_cast<float>(worstX), static_cast<float>(worstZ));
        host->SetLocal("l_tract_hq_h", static_cast<double>(worstValue), subject);
        subject->Release();
    }
    host->SetGlobal("g_num_treated_patients", 0.0);
    host->SetGlobal("g_health_capacity", 0.0);
    host->SetGlobal("g_education_funding_p", static_cast<double>(host->BudgetLineItemFunding(0x9188f4c) * 100.0f));
    bestValue = worstValue = 0;
    host->ResidentialTracts(0x158, bestValue, bestX, bestZ, worstValue, worstX, worstZ);
    subject = nullptr;
    if (host->CreateAdviceSubject(&subject) && subject) {
        subject->SetLocation(static_cast<float>(bestX), static_cast<float>(bestZ));
        host->SetLocal("l_tract_eq_l", static_cast<double>(bestValue), subject);
        subject->Release();
    }
    subject = nullptr;
    if (host->CreateAdviceSubject(&subject) && subject) {
        subject->SetLocation(static_cast<float>(worstX), static_cast<float>(worstZ));
        host->SetLocal("l_tract_eq_h", static_cast<double>(worstValue), subject);
        subject->Release();
    }
    update_occupant_variables();
}

void cSC4AdvisorHQ::update_occupant_variables() {
    // see ADVISOR-CONCRETE-1: the occupant walk over the hospitals / clinics / schools / museums / libraries group
    // lists (spResidentialSim vslot 0x178/0x188, spOccupantManager vslot 0x180) is unrecovered.
    if (g_host) g_host->UpdateSimulatorStatistics(kGZCLSID_cSC4AdvisorHQ);
}

// ---- cSC4AdvisorUtility --------------------------------------------------------------------------------------

bool cSC4AdvisorUtility::Init(int advisorType) {
    if (!cSC4AdvisorBase::Init(advisorType)) return false;
    updatePending_ = true;
    if (!subscribed_) {
        cIGZMessageServer2* messages = g_host ? g_host->Messages() : nullptr;
        if (messages) {
            messages->AddNotification(this, kAdvisorUtilityMessageBuilding);
            messages->AddNotification(this, kAdvisorUtilityMessagePowerStrikeOn);
            messages->AddNotification(this, kAdvisorUtilityMessagePowerStrikeOff);
        }
        subscribed_ = true;
    }
    return true;
}

bool cSC4AdvisorUtility::Shutdown() {
    if (subscribed_) {
        cIGZMessageServer2* messages = g_host ? g_host->Messages() : nullptr;
        if (messages) {
            messages->RemoveNotification(this, kAdvisorUtilityMessageBuilding);
            messages->RemoveNotification(this, kAdvisorUtilityMessagePowerStrikeOn);
            messages->RemoveNotification(this, kAdvisorUtilityMessagePowerStrikeOff);
        }
        subscribed_ = false;
    }
    return cSC4AdvisorBase::Shutdown();
}

std::uint32_t cSC4AdvisorUtility::GetGZCLSID() const { return kGZCLSID_cSC4AdvisorUtility; }

bool cSC4AdvisorUtility::DoMessage(cIGZMessage2* message) {
    auto* standard = static_cast<cIGZMessage2Standard*>(message);
    const std::uint32_t type = message->GetType();
    IAdvisorConcreteHost* host = g_host;
    if (type == kAdvisorBaseMessageBuildingChanged || type == kAdvisorBaseMessageBuildingChanged2) {
        if (host && host->MessageVoid1IsPropertyHolder(message)) {
            if (dirtyMask_ == 0) post_self_update(0, 0);
            dirtyMask_ |= 3;
        }
    } else if (type == kAdvisorUtilityMessageBuilding) {
        if (dirtyMask_ == 0) post_self_update(0, 0);
        dirtyMask_ |= 4;
        const std::uint64_t data1 = standard->GetData1();
        if (data1 >= 3 && data1 <= 6) {
            // The event id comes from a static table keyed by the data word (3..6).
            static const std::uint32_t kEventIds[4] = {0x3a383059, 0x0a383053, 0xaa383052, 0x4a383049};
            const std::uint32_t eventId = kEventIds[data1 - 3];
            cISC4AdviceSubject* subject = nullptr;
            if (host && host->CreateAdviceSubject(&subject) && subject) {
                subject->SetObject(static_cast<cIGZUnknown*>(standard->GetIGZUnknown()));
                host->PushEvent(static_cast<int>(eventId), subject);
                subject->Release();
            }
        }
    } else if (type == kAdvisorUtilityMessagePowerStrikeOff) {
        if (host) host->SetGlobal("g_power_strike", 0.0);
        if (host) host->PushEvent(0x2a31a30f, nullptr);
    } else if (type == kAdvisorUtilityMessagePowerStrikeOn) {
        if (host) host->SetGlobal("g_power_strike", 1.0);
        if (host) host->PushEvent(0xea31a30a, nullptr);
    } else if (type != kAdvisorBaseMessageSelfUpdate) {
        return cSC4AdvisorBase::DoMessage(message);
    } else {
        if ((dirtyMask_ & 3) != 0) update_occupant_variables();
        if ((dirtyMask_ & 4) != 0) update_neighbor_deal_variables();
        dirtyMask_ = 0;
    }
    return cSC4AdvisorBase::DoMessage(message);
}

void cSC4AdvisorUtility::simulate_day_events() {
    // see ADVISOR-CONCRETE-2: the utility event walk matches each queued event's subject against the advice list and
    // the event-advice map, creating location-bearing subjects for the utility advices. Unrecovered: the advice
    // QueryInterface / subject clone / advice trigger plumbing. The base event walk still runs for the other advices.
    cSC4Advisor::simulate_day_events();
}

void cSC4AdvisorUtility::update_variables() {
    // see ADVISOR-CONCRETE-3: the utility record (power / water production, capacity, usage) reads spPowerSim /
    // spWaterSim / spBudgetSim and is unrecovered.
    if (g_host) g_host->UpdateSimulatorStatistics(kGZCLSID_cSC4AdvisorUtility);
}

void cSC4AdvisorUtility::update_occupant_variables() {
    // see ADVISOR-CONCRETE-4: the utility occupant walk (power plants / pumps) is unrecovered.
    if (g_host) g_host->UpdateSimulatorStatistics(kGZCLSID_cSC4AdvisorUtility);
}

void cSC4AdvisorUtility::update_neighbor_deal_variables() {
    // see ADVISOR-CONCRETE-5: the neighbour-deal reads (spNeighborsSim deals) are unrecovered.
    if (g_host) g_host->UpdateSimulatorStatistics(kGZCLSID_cSC4AdvisorUtility);
}

// ---- cSC4AdvisorSafety ---------------------------------------------------------------------------------------

bool cSC4AdvisorSafety::Init(int advisorType) {
    if (!cSC4AdvisorBase::Init(advisorType)) return false;
    updatePending_ = true;
    if (!subscribed_) {
        cIGZMessageServer2* messages = g_host ? g_host->Messages() : nullptr;
        if (messages) {
            static const std::uint32_t kMessages[16] = {
                0xea1f0dcd, 0xa8daf511, 0xa8daf512, 0x2a614891, 0x2a614892, 0xaa667ce2, 0x68f54ce1, 0x68f54ce2,
                0xca1b1e64, 0xaa1b2002, 0x69247dc4, 0x69247dc5, 0x077accd0, 0x077accd2, 0xca8dcdb4, 0xaa552f77};
            for (std::uint32_t type : kMessages) messages->AddNotification(this, type);
        }
        subscribed_ = true;
    }
    return true;
}

bool cSC4AdvisorSafety::Shutdown() {
    if (subscribed_) {
        cIGZMessageServer2* messages = g_host ? g_host->Messages() : nullptr;
        if (messages) {
            static const std::uint32_t kMessages[16] = {
                0xea1f0dcd, 0xa8daf511, 0xa8daf512, 0x2a614891, 0x2a614892, 0xaa667ce2, 0x68f54ce1, 0x68f54ce2,
                0xca1b1e64, 0xaa1b2002, 0x69247dc4, 0x69247dc5, 0x077accd0, 0x077accd2, 0xca8dcdb4, 0xaa552f77};
            for (std::uint32_t type : kMessages) messages->RemoveNotification(this, type);
        }
        subscribed_ = false;
    }
    return cSC4AdvisorBase::Shutdown();
}

std::uint32_t cSC4AdvisorSafety::GetGZCLSID() const { return kGZCLSID_cSC4AdvisorSafety; }

bool cSC4AdvisorSafety::DoMessage(cIGZMessage2* message) {
    // see ADVISOR-CONCRETE-6: the safety messages (crime / fire / health coverage) update the safety variables; the
    // property-holder branch and the deferred self-update follow the shared pattern.
    const std::uint32_t type = message->GetType();
    IAdvisorConcreteHost* host = g_host;
    if (type == kAdvisorBaseMessageBuildingChanged || type == kAdvisorBaseMessageBuildingChanged2) {
        if (host && host->MessageVoid1IsPropertyHolder(message)) {
            if (dirtyMask_ == 0) post_self_update(0, 0);
            dirtyMask_ |= 1;
        }
    } else if (type == kAdvisorBaseMessageSelfUpdate) {
        if ((dirtyMask_ & 1) != 0) update_occupant_variables();
        dirtyMask_ = 0;
    }
    return cSC4AdvisorBase::DoMessage(message);
}

void cSC4AdvisorSafety::update_variables() {
    // see ADVISOR-CONCRETE-7: the safety record (crime / fire / health) reads spCrimeSim / spFireProtectionSim and is
    // unrecovered.
    if (g_host) g_host->UpdateSimulatorStatistics(kGZCLSID_cSC4AdvisorSafety);
}

void cSC4AdvisorSafety::update_occupant_variables() {
    // see ADVISOR-CONCRETE-8: the safety occupant walk (police / fire stations) is unrecovered.
    if (g_host) g_host->UpdateSimulatorStatistics(kGZCLSID_cSC4AdvisorSafety);
}

// ---- cSC4AdvisorTraffic --------------------------------------------------------------------------------------

cSC4AdvisorTraffic::~cSC4AdvisorTraffic() = default;

bool cSC4AdvisorTraffic::Init(int advisorType) {
    if (!subscribed_) {
        cIGZMessageServer2* messages = g_host ? g_host->Messages() : nullptr;
        if (messages) messages->AddNotification(this, kAdvisorTrafficMessageCarZot);
        subscribed_ = true;
    }
    updatePending_ = true;
    return cSC4AdvisorBase::Init(advisorType);
}

bool cSC4AdvisorTraffic::Shutdown() {
    if (subscribed_) {
        cIGZMessageServer2* messages = g_host ? g_host->Messages() : nullptr;
        if (messages) messages->RemoveNotification(this, kAdvisorTrafficMessageCarZot);
        subscribed_ = false;
    }
    return cSC4AdvisorBase::Shutdown();
}

std::uint32_t cSC4AdvisorTraffic::GetGZCLSID() const { return kGZCLSID_cSC4AdvisorTraffic; }

bool cSC4AdvisorTraffic::DoMessage(cIGZMessage2* message) {
    auto* standard = static_cast<cIGZMessage2Standard*>(message);
    const std::uint32_t type = message->GetType();
    IAdvisorConcreteHost* host = g_host;
    if (type == kAdvisorBaseMessageBuildingChanged || type == kAdvisorBaseMessageBuildingChanged2) {
        if (host && host->MessageVoid1IsPropertyHolder(message)) {
            if (dirtyMask_ == 0) post_self_update(0, 0);
            dirtyMask_ |= 3;
        }
    } else if (type == kAdvisorTrafficMessageCarZot) {
        const std::uint64_t data1 = standard->GetData1();
        void* occupant = static_cast<void*>(standard->GetIGZUnknown());
        if (data1 == 2) {
            carZots_.erase(occupant);
        } else if (data1 == 1) {
            carZots_.insert(occupant);
        }
        if (host) host->SetGlobal("g_car_zot_count", static_cast<double>(carZots_.size()));
        return true;
    } else if (type != kAdvisorBaseMessageSelfUpdate) {
        return cSC4AdvisorBase::DoMessage(message);
    } else {
        if ((dirtyMask_ & 3) != 0) update_occupant_variables();
        dirtyMask_ = 0;
    }
    return cSC4AdvisorBase::DoMessage(message);
}

void cSC4AdvisorTraffic::update_variables() {
    // see ADVISOR-CONCRETE-9: the traffic record (congestion / funding / station utilization) reads spTrafficSim and
    // the city's airport / seaport managers and is unrecovered.
    if (g_host) g_host->UpdateSimulatorStatistics(kGZCLSID_cSC4AdvisorTraffic);
}

void cSC4AdvisorTraffic::update_occupant_variables() {
    // see ADVISOR-CONCRETE-10: the traffic occupant walk (station / depot counts) reads spTrafficSim and is
    // unrecovered.
    if (g_host) g_host->UpdateSimulatorStatistics(kGZCLSID_cSC4AdvisorTraffic);
}

// ---- cSC4AdvisorCityPlanning ---------------------------------------------------------------------------------

// The primary interface id is the class id itself; anything else defers to cSC4Advisor::QueryInterface.
bool cSC4AdvisorCityPlanning::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZCLSID_cSC4AdvisorCityPlanning) {
        *out = this;
        AddRef();
        return true;
    }
    return cSC4Advisor::QueryInterface(iid, out);
}

bool cSC4AdvisorCityPlanning::Init(int advisorType) {
    if (!cSC4AdvisorBase::Init(advisorType)) return false;
    updatePending_ = true;
    if (!subscribed_) {
        subscribed_ = true;
        cIGZMessageServer2* messages = g_host ? g_host->Messages() : nullptr;
        if (messages) {
            static const std::uint32_t kMessages[8] = {
                kAdvisorCityPlanningMessagePlop,      kAdvisorCityPlanningMessageBulldoze,
                kAdvisorCityPlanningMessageBulldoze2, kAdvisorCityPlanningMessageRezone,
                kAdvisorCityPlanningMessageRezone2,   kAdvisorCityPlanningMessageDevelop,
                kAdvisorCityPlanningMessageDevelop2,  kAdvisorCityPlanningMessageDevelop3};
            for (std::uint32_t type : kMessages) messages->AddNotification(this, type);
        }
    }
    return true;
}

bool cSC4AdvisorCityPlanning::Shutdown() {
    if (subscribed_) {
        subscribed_ = false;
        cIGZMessageServer2* messages = g_host ? g_host->Messages() : nullptr;
        if (messages) {
            static const std::uint32_t kMessages[8] = {
                kAdvisorCityPlanningMessagePlop,      kAdvisorCityPlanningMessageBulldoze,
                kAdvisorCityPlanningMessageBulldoze2, kAdvisorCityPlanningMessageRezone,
                kAdvisorCityPlanningMessageRezone2,   kAdvisorCityPlanningMessageDevelop,
                kAdvisorCityPlanningMessageDevelop2,  kAdvisorCityPlanningMessageDevelop3};
            for (std::uint32_t type : kMessages) messages->RemoveNotification(this, type);
        }
    }
    return cSC4AdvisorBase::Shutdown();
}

std::uint32_t cSC4AdvisorCityPlanning::GetGZCLSID() const { return kGZCLSID_cSC4AdvisorCityPlanning; }

// A building was plopped / bulldozed: the direction (+1/-1) moves the building / park / recreation counts, a
// landmark (property 0x150a) also moves the landmark map, and the change is deferred with a self-update.
bool cSC4AdvisorCityPlanning::DoMessage(cIGZMessage2* message) {
    const std::uint32_t type = message->GetType();
    IAdvisorConcreteHost* host = g_host;
    if (type == kAdvisorBaseMessageBuildingChanged || type == kAdvisorBaseMessageBuildingChanged2) {
        if (host && host->MessageVoid1IsPropertyHolder(message)) {
            const int direction = type == kAdvisorBaseMessageBuildingChanged ? 1 : -1;
            buildingCount_ += direction;
            if (host->MessageVoid1HasProperty(message, 0x1006)) parkCount_ += direction;
            if (host->MessageVoid1HasProperty(message, 0x1006)) recreationCount_ += direction;   // quirk: 0x1006 twice
            if (host->MessageVoid1HasProperty(message, 0x150a)) {
                std::uint32_t landmarkType = 0;
                if (host->BuildingDevelopmentLookup(message, &landmarkType)) {
                    if (type == kAdvisorBaseMessageBuildingChanged) {
                        landmarks_[static_cast<int>(landmarkType)] += 1;
                    } else if (landmarks_.find(static_cast<int>(landmarkType)) != landmarks_.end()) {
                        landmarks_[static_cast<int>(landmarkType)] -= 1;   // never erased (kept quirk)
                    }
                }
            }
            if (dirtyMask_ == 0) post_self_update(0, 0);
            dirtyMask_ |= 1;
        }
    } else if (type == kAdvisorBaseMessageSelfUpdate) {
        if ((dirtyMask_ & 1) != 0) update_occupant_variables();
        dirtyMask_ = 0;
    } else {
        return cSC4AdvisorBase::DoMessage(message);
    }
    return cSC4AdvisorBase::DoMessage(message);
}

int cSC4AdvisorCityPlanning::GetLandmarkCount(int landmarkType) {
    auto it = landmarks_.find(landmarkType);
    return it == landmarks_.end() ? 0 : it->second;
}

bool cSC4AdvisorCityPlanning::HasLandmark(int landmarkType) {
    return landmarks_.find(landmarkType) != landmarks_.end();
}

bool cSC4AdvisorCityPlanning::Write(cIGZOStream& stream) const {
    stream.SetUint16(3);   // the same version number the advisor record uses
    if (!cSC4Advisor::Write(stream)) return false;
    stream.SetSint32(buildingCount_);
    stream.SetSint32(parkCount_);
    stream.SetSint32(recreationCount_);
    stream.SetSint32(static_cast<std::int32_t>(landmarks_.size()));
    for (const auto& entry : landmarks_) {
        stream.SetSint32(entry.first);
        stream.SetSint32(entry.second);
    }
    return !stream.GetError();
}

bool cSC4AdvisorCityPlanning::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (!cSC4Advisor::Read(stream)) return false;
    stream.GetSint32(buildingCount_);
    stream.GetSint32(parkCount_);
    stream.GetSint32(recreationCount_);
    if (version >= 3) {
        landmarks_.clear();
        std::int32_t count = 0;
        stream.GetSint32(count);
        for (; count != 0; --count) {
            std::int32_t type = 0, value = 0;
            stream.GetSint32(type);
            stream.GetSint32(value);
            landmarks_[type] = value;
        }
    }
    return !stream.GetError();
}

// Parks / landmarks funding and the three member counts; the building / park / recreation sweep is the large
// update_variables walk (unrecovered).
void cSC4AdvisorCityPlanning::update_occupant_variables() {
    IAdvisorConcreteHost* host = g_host;
    if (!host) return;
    host->SetGlobal("g_parks_funding_p", static_cast<double>(host->BudgetLineItemFunding(0x8a4260a6) * 100.0f));
    host->SetGlobal("g_landmarks_funding_p", static_cast<double>(host->BudgetLineItemFunding(0x8a416368) * 100.0f));
    host->SetGlobal("g_num_buildings", static_cast<double>(buildingCount_));
    host->SetGlobal("g_num_parks", static_cast<double>(parkCount_));
    host->SetGlobal("g_num_recreation", static_cast<double>(recreationCount_));
}

void cSC4AdvisorCityPlanning::update_variables() {
    // see ADVISOR-CONCRETE-11: the building / park / recreation sweep reads spBuildingDevelopmentSim / spCity and is
    // unrecovered; the member-derived counts are refreshed by update_occupant_variables.
    if (g_host) g_host->UpdateSimulatorStatistics(kGZCLSID_cSC4AdvisorCityPlanning);
    update_occupant_variables();
}

}  // namespace sc4

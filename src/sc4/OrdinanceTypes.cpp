#include "sc4/OrdinanceTypes.h"

namespace sc4 {

static IOrdinanceSimHost* g_host = nullptr;
IOrdinanceSimHost* OrdinanceSimHost() { return g_host; }
void SetOrdinanceSimHost(IOrdinanceSimHost* host) { g_host = host; }

namespace {
std::int64_t Income(float factor, std::int64_t constant, std::int64_t base) {
    return static_cast<std::int64_t>(static_cast<float>(base) * factor + static_cast<float>(constant));
}
}  // namespace

std::int64_t cSC4OrdinanceFreeClinics::GetCurrentMonthlyIncome() {
    std::int64_t value = 0;
    if (!g_host->BudgetLineItem(0x9188f42, 0x3110000, value)) value = 0;
    return Income(incomeFactor_, constantIncome_, value);
}

std::int64_t cSC4OrdinanceJuniorSportsLeague::GetCurrentMonthlyIncome() {
    std::int64_t total = 0;
    g_host->BudgetDepartmentTotal(0x8a4260a6, total);
    return Income(incomeFactor_, constantIncome_, total);
}

bool cSC4OrdinanceJuniorSportsLeague::CheckConditions() {
    if (!cSC4OrdinanceSimple::CheckConditions()) return false;
    std::int64_t total = 0;
    g_host->BudgetDepartmentTotal(0x8a4260a6, total);
    return total != 0;
}

bool cSC4OrdinanceLandfillGasRecovery::Simulate() {
    adjustedIncome_ = GetCurrentMonthlyIncome();
    return true;
}

std::int64_t cSC4OrdinanceLandfillGasRecovery::GetCurrentMonthlyIncome() {
    std::uint32_t total = 0, used = 0;
    g_host->LandfillCapacity(total, used);
    return Income(incomeFactor_, constantIncome_, used);
}

bool cSC4OrdinanceLandfillGasRecovery::CheckConditions() {
    std::uint32_t total = 0, used = 0;
    g_host->LandfillCapacity(total, used);
    if (used == 0) return false;
    if (g_host->PowerPlantsOfType(3) < 1) return false;
    return cSC4OrdinanceSimple::CheckConditions();
}

bool cSC4OrdinanceNeighborhoodWatch::CheckConditions() {
    return cSC4OrdinanceSimple::CheckConditions() && g_host->PoliceStationCount() != 0;
}

std::int64_t cSC4OrdinanceProReadingCampaign::GetCurrentMonthlyIncome() {
    std::int64_t sum = 0, value = 0;
    if (g_host->BudgetLineItem(0x9188f4c, 0x3190000, value)) sum += value;
    value = 0;
    if (g_host->BudgetLineItem(0x9188f4c, 0x34a000, value)) sum += value;
    return Income(incomeFactor_, constantIncome_, sum);
}

bool cSC4OrdinanceTouristPromotion::Simulate() {
    adjustedIncome_ = GetCurrentMonthlyIncome();
    return true;
}

std::int64_t cSC4OrdinanceTouristPromotion::GetCurrentMonthlyIncome() {
    std::int64_t total = 0;
    if (!g_host->BudgetDepartmentTotal(0x8a416368, total)) return constantIncome_;
    return constantIncome_ + static_cast<std::int64_t>(static_cast<float>(total) * incomeFactor_);
}

bool cSC4OrdinanceTrashPresort::Simulate() {
    adjustedIncome_ = GetCurrentMonthlyIncome();
    return true;
}

std::int64_t cSC4OrdinanceTrashPresort::GetCurrentMonthlyIncome() {
    std::int64_t total = 0;
    if (!g_host->BudgetDepartmentTotal(0x6a42a8f5, total)) return constantIncome_;
    return constantIncome_ + static_cast<std::int64_t>(static_cast<float>(total) * incomeFactor_);
}

bool cSC4OrdinanceTrashPresort::CheckConditions() {
    return cSC4OrdinanceSimple::CheckConditions() && 0.0f < g_host->PollutionAtLevel() * 100.0f;
}

}  // namespace sc4

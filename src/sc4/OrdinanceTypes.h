// The 20 concrete ordinances derived from cSC4OrdinanceSimple (0x100795ca2..0x10079b0xx). Most only supply a
// class id; the ones that differ override the income formula and/or the availability conditions. The original
// caches pointers to budget / police / pollution / power / landfill services at +0xe8/+0xf0; here those lookups
// are behind IOrdinanceSimHost. cSC4OrdinanceLegalizeGambling::SetOn (demolishes casinos, 0x100796e96) is not
// recovered.
#pragma once
#include <cstdint>

#include "sc4/Ordinance.h"

namespace sc4 {

class IOrdinanceSimHost {
public:
    virtual ~IOrdinanceSimHost() = default;
    // Budget department `dept`, line item `item` (budget sim vslot 0xd8 then dept vslot 0xa0, item vslot 0x50).
    // Returns false when the department or the item does not exist.
    virtual bool BudgetLineItem(std::uint32_t dept, std::uint32_t item, std::int64_t& value) = 0;
    // Budget department `dept` total (dept vslot 0x78). Returns false when the department does not exist.
    virtual bool BudgetDepartmentTotal(std::uint32_t dept, std::int64_t& value) = 0;
    virtual bool LandfillCapacity(std::uint32_t& total, std::uint32_t& used) = 0;  // landfill developer vslot 0x50
    virtual int PowerPlantsOfType(int type) = 0;                                    // power sim vslot 0xc0
    virtual int PoliceStationCount() = 0;                                           // police sim vslot 0xc0
    virtual float PollutionAtLevel() = 0;                                           // pollution sim vslot 0x108 (fraction)
};
IOrdinanceSimHost* OrdinanceSimHost();
void SetOrdinanceSimHost(IOrdinanceSimHost* host);

#define SC4_ORDINANCE_CLASS(Name, Clsid) \
    constexpr std::uint32_t kGZCLSID_##Name = Clsid; \
    class Name : public cSC4OrdinanceSimple { \
    public: \
        std::uint32_t GetGZCLSID() const override { return kGZCLSID_##Name; }

SC4_ORDINANCE_CLASS(cSC4OrdinanceCarpoolIncentive, 0xa2bf1ddc) };
SC4_ORDINANCE_CLASS(cSC4OrdinanceCleanAir, 0xa2bf1e43) };
SC4_ORDINANCE_CLASS(cSC4OrdinanceCommunityCPRTraining, 0x00d0723d) };
SC4_ORDINANCE_CLASS(cSC4OrdinanceMandatorySmokeDetectors, 0x62bf1daa) };
SC4_ORDINANCE_CLASS(cSC4OrdinanceNuclearFreeZone, 0xe0d0722e) };
SC4_ORDINANCE_CLASS(cSC4OrdinancePaperReductionAct, 0x22f6e81b) };
SC4_ORDINANCE_CLASS(cSC4OrdinancePowerConservation, 0x82b9999b) };
SC4_ORDINANCE_CLASS(cSC4OrdinanceShuttleService, 0xa2bf1de5) };
SC4_ORDINANCE_CLASS(cSC4OrdinanceTireRecycling, 0xc2f6e81f) };
SC4_ORDINANCE_CLASS(cSC4OrdinanceVehicleEmissionStandard, 0xc2bf1e04) };
SC4_ORDINANCE_CLASS(cSC4OrdinanceWaterConservation, 0x02bf1dfa) };
SC4_ORDINANCE_CLASS(cSC4OrdinanceYouthCurfew, 0xc2bf1dc5) };

// Income = factor * (line item 0x3110000 of department 0x9188f42) + constant.
SC4_ORDINANCE_CLASS(cSC4OrdinanceFreeClinics, 0x40d07236)
    std::int64_t GetCurrentMonthlyIncome() override;
};
// Income = factor * (total of department 0x8a4260a6) + constant; needs that total to be non-zero.
SC4_ORDINANCE_CLASS(cSC4OrdinanceJuniorSportsLeague, 0x815b4cef)
    std::int64_t GetCurrentMonthlyIncome() override;
    bool CheckConditions() override;
};
// Income = factor * (landfill used) + constant; needs landfill and at least one type-3 power plant.
SC4_ORDINANCE_CLASS(cSC4OrdinanceLandfillGasRecovery, 0x22f6e80c)
    bool Simulate() override;
    std::int64_t GetCurrentMonthlyIncome() override;
    bool CheckConditions() override;
};
SC4_ORDINANCE_CLASS(cSC4OrdinanceLegalizeGambling, 0xa0d07129)
    std::int64_t GetCurrentMonthlyIncome() override { return static_cast<std::int64_t>(constantIncome_); }
};
SC4_ORDINANCE_CLASS(cSC4OrdinanceNeighborhoodWatch, 0x62bf1db9)
    bool CheckConditions() override;
};
// Income = factor * (items 0x3190000 + 0x34a000 of department 0x9188f4c) + constant.
SC4_ORDINANCE_CLASS(cSC4OrdinanceProReadingCampaign, 0xe0d07233)
    std::int64_t GetCurrentMonthlyIncome() override;
};
// Income = constant + factor * (total of department 0x8a416368).
SC4_ORDINANCE_CLASS(cSC4OrdinanceTouristPromotion, 0x62f6e7cf)
    bool Simulate() override;
    std::int64_t GetCurrentMonthlyIncome() override;
};
// Income = constant + factor * (total of department 0x6a42a8f5); needs positive pollution.
SC4_ORDINANCE_CLASS(cSC4OrdinanceTrashPresort, 0x42bf1e18)
    bool Simulate() override;
    std::int64_t GetCurrentMonthlyIncome() override;
    bool CheckConditions() override;
};
#undef SC4_ORDINANCE_CLASS

}  // namespace sc4

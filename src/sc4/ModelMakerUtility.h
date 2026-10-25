// cSC4ModelMakerUtility (0x1002c0280..0x1002c1132): the lookups that turn an occupant's model exemplar properties into
// S3D model instance ids. The exemplar lists the model(s) in one of six property forms (0x27812820..0x27812825, +0x100
// for the lit variant): a single instance id, a base id the zoom / rotation / state offsets are added to, a table per
// zoom and rotation, a table per zoom, a list of models per orientation with placement offsets (0x27812824), or a base id
// plus an orientation in the high word (0x27812825). The detail level offset (nModelLODOffset) picks the row of the
// zoom -> model LOD table; the first row is the identity.
#pragma once
#include "sc4/ModelMaker.h"

namespace sc4 {

class cSC4ModelMakerUtility final : public IModelMakerUtility {
public:
    int GetOccupantModelClass(cISC4Occupant* occupant) override;                            // @ 0x1002c0e82
    void GetOccupantOrientationAndState(cISC4Occupant* occupant, std::uint32_t& orientation, int& state) override;   // @ 0x1002c0d70
    void GetAllModelInstanceIDs(std::vector<std::uint32_t>& ids, std::vector<float>* transforms, cISC4Occupant* occupant,
                                int zoom, int rotation, int state, bool lit, std::uint32_t orientation,
                                std::uint32_t* outA, std::uint32_t* outB) override;         // @ 0x1002c04ac
    bool OccupantUsesBuildingStyleModels(cISC4Occupant* occupant) override;                  // @ 0x1002c0ce0
    bool FoundationOccupantUsesBuildingStyleModels(cISC4Occupant* occupant) override;        // @ 0x1002c0ca4
    std::uint32_t GetModelProperty1Instance(std::uint32_t baseId, int rotation, int index) override { return rotation * 0x100 + baseId + index * 0x10; }   // @ 0x1002c03c8
    bool GetAnimatedModelFromResourceKey(const cGZPersistResourceKey&, s3d::cIS3DModelInstanceAnimated**) override { return false; }

    // The property-holder form (@ 0x1002c053c).
    static void GetAllModelInstanceIDs(std::vector<std::uint32_t>& ids, std::vector<float>* transforms, cISCPropertyHolder* holder,
                                       bool nightVariant, int zoom, int rotation, int state, bool lit, std::uint32_t orientation,
                                       std::uint32_t* outA, std::uint32_t* outB);
    // One model out of a property value (@ 0x1002c0280); `at` is the index the model's fields start at.
    static std::uint32_t GetModelInstanceID(const std::vector<std::uint32_t>& value, std::uint32_t property, std::uint32_t at,
                                            int zoom, int rotation, int state, bool lit, std::uint32_t* outA, std::uint32_t* outB);
};

}  // namespace sc4

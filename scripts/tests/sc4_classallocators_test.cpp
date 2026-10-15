#include <cstdio>
#include <cstdlib>

#include "sc4/ClassAllocators.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4alloc;

int main() {
    { void* p = SC4LotBaseTextureOccupantOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    SC4LotBaseTextureOccupantOperatorDelete(nullptr);
    { void* p = S3DModelInstanceOperatorNew(0xd0); CHECK(p); S3DModelInstanceOperatorDelete(p, 0xd0); void* q = S3DModelInstanceOperatorNew(0xd0 + 16); CHECK(q); S3DModelInstanceOperatorDelete(q, 0xd0 + 16); S3DModelInstanceOperatorDelete(nullptr, 0xd0); }
    { void* p = SC4VehicleOperatorNew(0x428); CHECK(p); SC4VehicleOperatorDelete(p, 0x428); void* q = SC4VehicleOperatorNew(0x428 + 16); CHECK(q); SC4VehicleOperatorDelete(q, 0x428 + 16); SC4VehicleOperatorDelete(nullptr, 0x428); }
    { void* p = SC4PedestrianOperatorNew(0x3b8); CHECK(p); SC4PedestrianOperatorDelete(p, 0x3b8); void* q = SC4PedestrianOperatorNew(0x3b8 + 16); CHECK(q); SC4PedestrianOperatorDelete(q, 0x3b8 + 16); SC4PedestrianOperatorDelete(nullptr, 0x3b8); }
    SC43DPlaceableObjectOperatorDelete(nullptr);
    { void* p = SC43DPlaceableObjectOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    { void* p = SC4PropOccupantTerrainDecalOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    SC4PropOccupantTerrainDecalOperatorDelete(nullptr);
    { void* p = SC4PropOccupantVisualEffectOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    SC4PropOccupantVisualEffectOperatorDelete(nullptr);
    { void* p = SC4PropOccupantBurntBuildingOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    SC4PropOccupantBurntBuildingOperatorDelete(nullptr);
    { void* p = SC4PropOccupantAutomataControllerOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    SC4PropOccupantAutomataControllerOperatorDelete(nullptr);
    SC4LotRetainingWallModelOperatorDelete(nullptr);
    { void* p = SC4LotRetainingWallModelOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    { void* p = SC4ConstructionOccupantOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    SC4ConstructionOccupantOperatorDelete(nullptr);
    { void* p = SC4PipeOccupantOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    SC4PipeOccupantOperatorDelete(nullptr);
    { void* p = SC4SubwayOccupantOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    SC4SubwayOccupantOperatorDelete(nullptr);
    { void* p = SC4NetworkTunnelOccupantOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    SC4NetworkTunnelOccupantOperatorDelete(nullptr);
    { void* p = S3DModelInstanceAnimatedSpriteOperatorNew(0x1a0); CHECK(p); S3DModelInstanceAnimatedSpriteOperatorDelete(p, 0x1a0); void* q = S3DModelInstanceAnimatedSpriteOperatorNew(0x1a0 + 16); CHECK(q); S3DModelInstanceAnimatedSpriteOperatorDelete(q, 0x1a0 + 16); S3DModelInstanceAnimatedSpriteOperatorDelete(nullptr, 0x1a0); }
    { void* p = S3DVertexArrayOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    { void* p = S3DIndexArrayOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    { void* p = S3DPrimitiveArrayOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    S3DVertexArrayOperatorDelete(nullptr);
    S3DIndexArrayOperatorDelete(nullptr);
    S3DPrimitiveArrayOperatorDelete(nullptr);
    { void* p = SC4NetworkOccupantWithPreBuiltModelOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    SC4NetworkOccupantWithPreBuiltModelOperatorDelete(nullptr);
    { void* p = SC4NetworkBridgeOccupantOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    SC4NetworkBridgeOccupantOperatorDelete(nullptr);
    SC4FoundationModelOperatorDelete(nullptr);
    { void* p = SC4FoundationModelOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    { void* p = SC4TrainCarOperatorNew(0x430); CHECK(p); SC4TrainCarOperatorDelete(p, 0x430); void* q = SC4TrainCarOperatorNew(0x430 + 16); CHECK(q); SC4TrainCarOperatorDelete(q, 0x430 + 16); SC4TrainCarOperatorDelete(nullptr, 0x430); }
    { void* p = SC4SignpostOccupantOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    SC4SignpostOccupantOperatorDelete(nullptr);
    { void* p = SC4TextDecalOccupantOperatorNew(32); CHECK(p != nullptr); std::free(p); }
    SC4TextDecalOccupantOperatorDelete(nullptr);
    {
        static int created = 0, destroyed = 0;
        SetCodaAllocator([]() -> void* { ++created; return std::malloc(8); }, [](void* p) { ++destroyed; std::free(p); });
        void* p = CEAXABLKDecfOperatorNew(); CEAXABLKDecfOperatorDelete(p); CHECK(created == 1 && destroyed == 1);
    }
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}

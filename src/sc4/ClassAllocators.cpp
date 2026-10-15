#include "sc4/ClassAllocators.h"

#include <cstdlib>
#include <new>

namespace sc4alloc {

namespace {
CodaNewFn g_codaNew = nullptr;
CodaDeleteFn g_codaDelete = nullptr;
}  // namespace

void SetCodaAllocator(CodaNewFn allocate, CodaDeleteFn release) {
    g_codaNew = allocate;
    g_codaDelete = release;
}

void* SC4LotBaseTextureOccupantOperatorNew(std::size_t size) { return std::malloc(size); }

void SC4LotBaseTextureOccupantOperatorDelete(void* object) {
    if (object) std::free(object);
}

void S3DModelInstanceOperatorDelete(void* object, std::size_t size) {
    if (!object) return;
    if (size == 0xd0) std::free(object);
    else ::operator delete(object);
}

void* S3DModelInstanceOperatorNew(std::size_t size) {
    if (size == 0xd0) return std::malloc(size);
    return ::operator new(size);
}

void* SC4VehicleOperatorNew(std::size_t size) {
    if (size == 0x428) return std::malloc(size);
    return ::operator new(size);
}

void SC4VehicleOperatorDelete(void* object, std::size_t size) {
    if (!object) return;
    if (size == 0x428) std::free(object);
    else ::operator delete(object);
}

void* SC4PedestrianOperatorNew(std::size_t size) {
    if (size == 0x3b8) return std::malloc(size);
    return ::operator new(size);
}

void SC4PedestrianOperatorDelete(void* object, std::size_t size) {
    if (!object) return;
    if (size == 0x3b8) std::free(object);
    else ::operator delete(object);
}

void SC43DPlaceableObjectOperatorDelete(void* object) {
    if (object) std::free(object);
}

void* SC43DPlaceableObjectOperatorNew(std::size_t size) { return std::malloc(size); }

void* SC4PropOccupantTerrainDecalOperatorNew(std::size_t size) { return std::malloc(size); }

void SC4PropOccupantTerrainDecalOperatorDelete(void* object) {
    if (object) std::free(object);
}

void* SC4PropOccupantVisualEffectOperatorNew(std::size_t size) { return std::malloc(size); }

void SC4PropOccupantVisualEffectOperatorDelete(void* object) {
    if (object) std::free(object);
}

void* SC4PropOccupantBurntBuildingOperatorNew(std::size_t size) { return std::malloc(size); }

void SC4PropOccupantBurntBuildingOperatorDelete(void* object) {
    if (object) std::free(object);
}

void* SC4PropOccupantAutomataControllerOperatorNew(std::size_t size) { return std::malloc(size); }

void SC4PropOccupantAutomataControllerOperatorDelete(void* object) {
    if (object) std::free(object);
}

void SC4LotRetainingWallModelOperatorDelete(void* object) {
    if (object) std::free(object);
}

void* SC4LotRetainingWallModelOperatorNew(std::size_t size) { return std::malloc(size); }

void* SC4ConstructionOccupantOperatorNew(std::size_t size) { return std::malloc(size); }

void SC4ConstructionOccupantOperatorDelete(void* object) {
    if (object) std::free(object);
}

void* SC4PipeOccupantOperatorNew(std::size_t size) { return std::malloc(size); }

void SC4PipeOccupantOperatorDelete(void* object) {
    if (object) std::free(object);
}

void* SC4SubwayOccupantOperatorNew(std::size_t size) { return std::malloc(size); }

void SC4SubwayOccupantOperatorDelete(void* object) {
    if (object) std::free(object);
}

void* SC4NetworkTunnelOccupantOperatorNew(std::size_t size) { return std::malloc(size); }

void SC4NetworkTunnelOccupantOperatorDelete(void* object) {
    if (object) std::free(object);
}

void S3DModelInstanceAnimatedSpriteOperatorDelete(void* object, std::size_t size) {
    if (!object) return;
    if (size == 0x1a0) std::free(object);
    else ::operator delete(object);
}

void* S3DModelInstanceAnimatedSpriteOperatorNew(std::size_t size) {
    if (size == 0x1a0) return std::malloc(size);
    return ::operator new(size);
}

void* S3DVertexArrayOperatorNew(std::size_t size) { return std::malloc(static_cast<std::uint32_t>(size)); }

void* S3DIndexArrayOperatorNew(std::size_t size) { return std::malloc(static_cast<std::uint32_t>(size)); }

void* S3DPrimitiveArrayOperatorNew(std::size_t size) { return std::malloc(static_cast<std::uint32_t>(size)); }

void S3DVertexArrayOperatorDelete(void* object) {
    if (object) std::free(object);
}

void S3DIndexArrayOperatorDelete(void* object) {
    if (object) std::free(object);
}

void S3DPrimitiveArrayOperatorDelete(void* object) {
    if (object) std::free(object);
}

void* SC4NetworkOccupantWithPreBuiltModelOperatorNew(std::size_t size) { return std::malloc(size); }

void SC4NetworkOccupantWithPreBuiltModelOperatorDelete(void* object) {
    if (object) std::free(object);
}

void* SC4NetworkBridgeOccupantOperatorNew(std::size_t size) { return std::malloc(size); }

void SC4NetworkBridgeOccupantOperatorDelete(void* object) {
    if (object) std::free(object);
}

void SC4FoundationModelOperatorDelete(void* object) {
    if (object) std::free(object);
}

void* SC4FoundationModelOperatorNew(std::size_t size) { return std::malloc(size); }

void* SC4TrainCarOperatorNew(std::size_t size) {
    if (size == 0x430) return std::malloc(size);
    return ::operator new(size);
}

void SC4TrainCarOperatorDelete(void* object, std::size_t size) {
    if (!object) return;
    if (size == 0x430) std::free(object);
    else ::operator delete(object);
}

void* SC4SignpostOccupantOperatorNew(std::size_t size) { return std::malloc(size); }

void SC4SignpostOccupantOperatorDelete(void* object) {
    if (object) std::free(object);
}

void* SC4TextDecalOccupantOperatorNew(std::size_t size) { return std::malloc(size); }

void SC4TextDecalOccupantOperatorDelete(void* object) {
    if (object) std::free(object);
}

void* CEAXABLKDecfOperatorNew() { return g_codaNew ? g_codaNew() : nullptr; }

void CEAXABLKDecfOperatorDelete(void* object) {
    if (g_codaDelete) g_codaDelete(object);
}

}  // namespace sc4alloc

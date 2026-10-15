// Class-specific allocation functions (operator new / operator delete) of the game's pooled classes
// (50 functions). All of them are thin wrappers over the C allocator: the plain forms call malloc / free
// directly (so the object never goes through the C++ global allocator), the sized forms (classes whose size is
// known) only take that path when the requested size is exactly the class size and fall back to the global
// operator new / delete for derived classes. The SND codec class forwards to the codec library's own
// allocation callbacks.
//
// The original functions are members of their classes; here they are free functions named after the class.
#pragma once
#include <cstddef>
#include <cstdint>

namespace sc4alloc {

// callbacks of the SND codec library (CODANew / CODADelete)
using CodaNewFn = void* (*)();
using CodaDeleteFn = void (*)(void*);
void SetCodaAllocator(CodaNewFn allocate, CodaDeleteFn release);

void* SC4LotBaseTextureOccupantOperatorNew(std::size_t size);   // 0x1000b36d0 malloc(size)
void SC4LotBaseTextureOccupantOperatorDelete(void* object);   // 0x1000b3776 free(object) when non-null
void S3DModelInstanceOperatorDelete(void* object, std::size_t size);   // 0x1000d8c1e class size 0xd0
void* S3DModelInstanceOperatorNew(std::size_t size);   // 0x1000d8c3e class size 0xd0
void* SC4VehicleOperatorNew(std::size_t size);   // 0x1000dabf0 class size 0x428
void SC4VehicleOperatorDelete(void* object, std::size_t size);   // 0x1000dac1a class size 0x428
void* SC4PedestrianOperatorNew(std::size_t size);   // 0x1000f13ea class size 0x3b8
void SC4PedestrianOperatorDelete(void* object, std::size_t size);   // 0x1000f1414 class size 0x3b8
void SC43DPlaceableObjectOperatorDelete(void* object);   // 0x10012067c free(object) when non-null
void* SC43DPlaceableObjectOperatorNew(std::size_t size);   // 0x10012068e malloc(size)
void* SC4PropOccupantTerrainDecalOperatorNew(std::size_t size);   // 0x10015f8f4 malloc(size)
void SC4PropOccupantTerrainDecalOperatorDelete(void* object);   // 0x10015f90e free(object) when non-null
void* SC4PropOccupantVisualEffectOperatorNew(std::size_t size);   // 0x1001606e2 malloc(size)
void SC4PropOccupantVisualEffectOperatorDelete(void* object);   // 0x1001607c6 free(object) when non-null
void* SC4PropOccupantBurntBuildingOperatorNew(std::size_t size);   // 0x100161750 malloc(size)
void SC4PropOccupantBurntBuildingOperatorDelete(void* object);   // 0x100161886 free(object) when non-null
void* SC4PropOccupantAutomataControllerOperatorNew(std::size_t size);   // 0x1001624b0 malloc(size)
void SC4PropOccupantAutomataControllerOperatorDelete(void* object);   // 0x100162586 free(object) when non-null
void SC4LotRetainingWallModelOperatorDelete(void* object);   // 0x1001673a4 free(object) when non-null
void* SC4LotRetainingWallModelOperatorNew(std::size_t size);   // 0x1001673c4 malloc(size)
void* SC4ConstructionOccupantOperatorNew(std::size_t size);   // 0x100177318 malloc(size)
void SC4ConstructionOccupantOperatorDelete(void* object);   // 0x1001775f2 free(object) when non-null
void* SC4PipeOccupantOperatorNew(std::size_t size);   // 0x1001a0eb8 malloc(size)
void SC4PipeOccupantOperatorDelete(void* object);   // 0x1001a0f50 free(object) when non-null
void* SC4SubwayOccupantOperatorNew(std::size_t size);   // 0x1001a1598 malloc(size)
void SC4SubwayOccupantOperatorDelete(void* object);   // 0x1001a1626 free(object) when non-null
void* SC4NetworkTunnelOccupantOperatorNew(std::size_t size);   // 0x1001a22ce malloc(size)
void SC4NetworkTunnelOccupantOperatorDelete(void* object);   // 0x1001a237e free(object) when non-null
void S3DModelInstanceAnimatedSpriteOperatorDelete(void* object, std::size_t size);   // 0x1001a4944 class size 0x1a0
void* S3DModelInstanceAnimatedSpriteOperatorNew(std::size_t size);   // 0x1001a4972 class size 0x1a0
void* S3DVertexArrayOperatorNew(std::size_t size);   // 0x1001aa456 malloc((uint32)size)
void* S3DIndexArrayOperatorNew(std::size_t size);   // 0x1001aa462 malloc((uint32)size)
void* S3DPrimitiveArrayOperatorNew(std::size_t size);   // 0x1001aa46e malloc((uint32)size)
void S3DVertexArrayOperatorDelete(void* object);   // 0x1001aa47a free(object) when non-null
void S3DIndexArrayOperatorDelete(void* object);   // 0x1001aa48c free(object) when non-null
void S3DPrimitiveArrayOperatorDelete(void* object);   // 0x1001aa49e free(object) when non-null
void* SC4NetworkOccupantWithPreBuiltModelOperatorNew(std::size_t size);   // 0x1001b9fc6 malloc(size)
void SC4NetworkOccupantWithPreBuiltModelOperatorDelete(void* object);   // 0x1001ba0ba free(object) when non-null
void* SC4NetworkBridgeOccupantOperatorNew(std::size_t size);   // 0x1001d25d6 malloc(size)
void SC4NetworkBridgeOccupantOperatorDelete(void* object);   // 0x1001d2698 free(object) when non-null
void SC4FoundationModelOperatorDelete(void* object);   // 0x10035f284 free(object) when non-null
void* SC4FoundationModelOperatorNew(std::size_t size);   // 0x10035f296 malloc(size)
void* SC4TrainCarOperatorNew(std::size_t size);   // 0x100509586 class size 0x430
void SC4TrainCarOperatorDelete(void* object, std::size_t size);   // 0x1005095a6 class size 0x430
void* SC4SignpostOccupantOperatorNew(std::size_t size);   // 0x10070e69e malloc(size)
void SC4SignpostOccupantOperatorDelete(void* object);   // 0x10070e912 free(object) when non-null
void* SC4TextDecalOccupantOperatorNew(std::size_t size);   // 0x10071147e malloc(size)
void SC4TextDecalOccupantOperatorDelete(void* object);   // 0x100711498 free(object) when non-null
void* CEAXABLKDecfOperatorNew();   // 0x1008f56c8 codec allocator callback
void CEAXABLKDecfOperatorDelete(void* object);   // 0x1008f56d6 codec release callback

}  // namespace sc4alloc

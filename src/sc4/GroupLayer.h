// cGroupLayer / cGroup_SC4RH / cInstance — the clustering layer the 3D view uses to merge nearby instances (trees,
// props) into radius-bounded groups (functions at 0x10040fcca..0x1004105d6).
// Layouts: cInstance +0 id, +8 group, +0x10 position, +0x1c radius, +0x20 owning id->instance map.
//          cGroup_SC4RH +0 instance list, +0x18 count, +0x1c peak count, +0x20 centre, +0x2c radius, +0x30 mark.
//          cGroupLayer +8 refcount, +0x10 id->instance map, +0x38 group list, +0x50 group count, +0x54 group radius.
// Every list in the original is pushed at the front, which fixes iteration order; this copy does the same.
#pragma once
#include <cstdint>
#include <list>
#include <unordered_map>

struct cS3DVector3;
struct SC4RectFloat;

struct cInstance;
using InstanceMap = std::unordered_map<std::uint32_t, cInstance*>;

struct cInstance {
    std::uint32_t id = 0;               // +0
    struct cGroup_SC4RH* group = nullptr;   // +8
    float x = 0, y = 0, z = 0;          // +0x10
    float radius = 0;                   // +0x1c
    InstanceMap* owner = nullptr;       // +0x20
};

struct cGroup_SC4RH {
    std::list<cInstance*> instances;    // +0
    std::uint32_t count = 0;            // +0x18
    std::uint32_t peak = 0;             // +0x1c
    float x = 0, y = 0, z = 0;          // +0x20
    float radius = 0;                   // +0x2c
    bool mark = false;                  // +0x30

    void Clear();                           // @ 0x10040fdaa  (frees the instances, peak = count, count = 0)
    void RemoveInstance(cInstance* inst);   // @ 0x10041027e
    void AddInstance(cInstance* inst);      // @ 0x10041062e
};

class cGroupLayer {
public:
    explicit cGroupLayer(float groupRadius);                       // @ 0x10040fcca
    virtual bool QueryInterface(std::uint32_t iid, void** out);    // @ 0x1004105ae (only iid 1)
    virtual void AddRef();                                         // @ 0x1004105c8
    virtual std::int32_t Release();                                // @ 0x1004105d6 (group objects are leaked, as in the original)
    virtual ~cGroupLayer() = default;

    bool Init() { return true; }                                   // @ 0x10040fd0e
    bool Shutdown();                                               // @ 0x10040fd16
    void Clear(bool keepGroups);                                   // @ 0x10040fd26
    void ClearEmptyGroups();                                       // @ 0x10040fe2e
    bool InsertInstance(const cS3DVector3& pos, float radius);     // @ 0x10040fe9e
    bool InsertInstance(std::uint32_t id, const cS3DVector3& pos, float radius);   // @ 0x10040ffe0
    bool RemoveInstance(std::uint32_t id);                         // @ 0x10041021c
    void UpdateInstance(std::uint32_t id, const cS3DVector3& pos, float radius);   // @ 0x1004102ce
    void Intersect(cGroupLayer* a, cGroupLayer* b, bool swap);     // @ 0x100410308
    void GetPositions(const SC4RectFloat& rect, std::list<cS3DVector3>& out, int& groups, int& total, bool clip) const;   // @ 0x1004104ca

    std::int32_t refCount = 0;          // +8
    InstanceMap instances;              // +0x10
    std::list<cGroup_SC4RH*> groups;    // +0x38
    std::int32_t groupCount = 0;        // +0x50
    float groupRadius = 0;              // +0x54

private:
    void AddPositionToGroup(const cS3DVector3& pos, float radius);   // @ 0x10040feac
    void AddInstanceToGroup(cInstance* inst);                        // @ 0x1004100de
};

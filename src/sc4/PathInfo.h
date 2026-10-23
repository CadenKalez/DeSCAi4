// cSC4PathInfo — the per-network-piece table of vehicle / pedestrian paths (class id iface 0xa9667793, 55 functions,
// 0x100458240..0x10045d9c6; created through cSC4PathInfoFactory). Every network occupant (road, rail, subway ...)
// owns one; the traffic simulator and the automata walk it to find where a car can enter, leave and stop.
//
// Original layout (the object is allocated from sSC4PathInfoAllocator): +0 cISC4PathInfo vtable, +8 cGZPersistResource
// sub-object (its own vtable, refcount and key), +0x28 std::unordered_map<uint32_t, tPath> (libc++; the iteration order of
// that table is observable - random picks index into it - so this port uses the same std::unordered_map), +0x50 flags byte
// (bit 0 "use elevation", bit 1 "has stop points"), +0x51..0x58 one byte per path type (1 when stop points of that type exist).
//
// Path key (uint32_t): bits 0..7 exit edge, 8..15 entry edge (edges 0..3 are the cell sides, 0xff is a wildcard in queries),
// 16..23 lane index, 24..27 path type (0 = any in queries), bit 28 = stop point (a one-point "path" marking where a
// vehicle stops). tPath = {std::vector<cS3DVector3> points, float length (+0x18), uint8 flags (+0x1c)}; flag bit 0 marks a
// junction path, bit 1 a path whose end points are not on the cell border (VerifyPathEdges).
//
// Everything the original reached through globals or the network occupant interface goes through IPathInfoServices (city /
// language / clock-seeded random) and IPathNetworkOccupant (the cISC4NetworkOccupant vtable slots it calls).
//
// Quirks kept (addresses in the .cpp): GetPathMidpoint keeps looping after finding the half-way segment and overwrites the
// answer with later (stale-origin) segments; GetPathsNearStartPoint/EndPoint use a strict "<" only to decide the head, so
// equal distances append; GetBestPath breaks near ties (< 0.01) with a coin flip from a clock-seeded generator; NewStopPoint
// writes the per-type flag byte at index type nibble even when it is above 7; QueryInterface(1) fails (only 0xa9667793 and
// 0x456b8f1d answer); SnapPointToPath interpolates the height with the ratio of SQUARED lengths; GetDestinationPoint's
// direction test includes the vertical term only when elevation is used for the distance, not for the path direction;
// CreateTransformedCopy mirrors x for flipped pieces with a different formula than a true reflection; OffsetAndScissor seeds the
// entry/exit edge bytes with indeterminate stack values in the original (0 here).
#pragma once
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "gz/StreamOps.h"
#include "gz/cGZPersistResourceManager.h"
#include "rz/cRZRandomSuper.h"
#include "sc4/MiscTypes.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4PathInfo = 0xa9667793;
constexpr std::uint32_t kGZIID_PathInfoPersistResource = 0x456b8f1d;
constexpr std::uint32_t kPathKeyStopFlag = 0x10000000;
constexpr std::uint8_t kPathEdgeWildcard = 0xff;

// cISC4PathInfo::tPath
struct tPath {
    std::vector<cS3DVector3> points;   // +0
    float length = 0.0f;               // +0x18
    std::uint8_t flags = 0;            // +0x1c: bit 0 junction path, bit 1 bad end points
};

constexpr std::uint8_t kPathFlagJunction = 1, kPathFlagBadEdge = 2;

using PathMap = std::unordered_map<std::uint32_t, tPath>;

constexpr std::uint32_t MakePathKey(std::uint32_t type, std::uint32_t entry, std::uint32_t exit, std::uint32_t lane = 0) {
    return (type << 24) | ((lane & 0xff) << 16) | ((entry & 0xff) << 8) | (exit & 0xff);
}

// The cISC4NetworkOccupant calls cSC4PathInfo makes (vtable slots of the network occupant interface).
class cSC4PathInfo;
class IPathNetworkOccupant {
public:
    virtual ~IPathNetworkOccupant() = default;
    virtual std::uint32_t PieceId() const = 0;                          // slot 0xb8 (CreateTransformedCopy calls it and drops the result)
    virtual std::uint8_t GetRotation() const = 0;                       // slot 0xc0: quarter turns
    virtual bool GetFlip() const = 0;                                   // slot 0xc8
    virtual void GetPosition(float* xyz) const = 0;                     // AsOccupant() (slot 0x180) -> cISC4Occupant slot 0x40
    virtual void GetBoundingCityCells(SC4RectInt32& rect) const = 0;    // AsOccupant() -> cISC4Occupant slot 0x58 (inclusive cells)
    virtual void TransformNode(cS3DVector3& point) = 0;                 // slot 0x208 (empty in cSC4NetworkOccupant)
    virtual float GetAltitude(float x, float z) const = 0;              // slot 0x1c0
    virtual std::uint8_t GetOneWayDirections() const = 0;               // slot 0x228
    virtual std::uint8_t GetActiveEdgesForType(std::uint32_t networkType) const = 0;        // slot 0x148
    virtual std::uint8_t GetActiveEdgesForTypeNoGroup(std::uint32_t networkType) const = 0; // slot 0x150
    virtual void AddPathConnection(std::uint32_t pathType, std::uint8_t entry, std::uint8_t exit) = 0;   // slot 0x1b0
    virtual void GetOccupiedCell(std::uint32_t& x, std::uint32_t& z) const = 0;             // slot 0x1d8
    virtual const cSC4PathInfo* GetPathInfo() const = 0;                // slot 0x188
};

// City / framework services used by the path info.
class IPathInfoServices {
public:
    virtual ~IPathInfoServices() = default;
    virtual void CellCenterToPosition(int cellX, int cellZ, float& worldX, float& worldZ) = 0;   // SL::spCity slot 0x308
    virtual bool IsRightHandTraffic() = 0;     // RZ::LM() slot 0x128(0) -> slot 0xc8; true when there is no such object
    virtual std::uint32_t RandomRaw() = 0;                                  // fresh cRZRandom(0xffffffff).RandomUint32UniformRaw()
    virtual double RandomDoubleRange(double lo, double hi) = 0;            // fresh cRZRandom(0xffffffff).RandomDoubleRangeUniform
};

// The interface (vtable 0x100ab1900 slots 3..42).
class cISC4PathInfo : public cIGZUnknown {
public:
    virtual const tPath* GetPath(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, std::uint8_t lane, float* length) const = 0;   // 0x18
    virtual const tPath* GetPath(std::uint32_t key, float* length) const = 0;                                               // 0x20
    virtual const tPath* GetBestPath(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, const cS3DVector3& near,
                                     std::uint32_t* key, float* length) const = 0;                                          // 0x28
    virtual std::vector<std::uint32_t> GetPathsNearStartPoint(std::uint32_t type, const cS3DVector3& point, std::uint8_t entry,
                                                             std::uint8_t exit, float radius) const = 0;                    // 0x30
    virtual std::vector<std::uint32_t> GetPathsNearEndPoint(std::uint32_t type, const cS3DVector3& point, std::uint8_t entry,
                                                           std::uint8_t exit, float radius) const = 0;                      // 0x38
    virtual const tPath* GetRandomPath(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, cRZRandomSuper* random,
                                       float* length) const = 0;                                                            // 0x40
    virtual const tPath* GetRandomPathWildcard(std::uint32_t type, std::uint8_t entry, std::uint8_t exit,
                                               cRZRandomSuper* random, std::uint32_t* key, float* length) const = 0;   // 0x48
    virtual float GetPathLength(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, std::uint8_t lane) const = 0;       // 0x50
    virtual float GetPathLength(std::uint32_t key) const = 0;                                                                // 0x58
    virtual int GetLaneCount(std::uint32_t type, std::uint8_t entry, std::uint8_t exit) const = 0;                          // 0x60
    virtual bool IsIntersection(std::uint32_t type) const = 0;                                                               // 0x68
    virtual int GetPathsFromEntryEdge(std::uint32_t type, std::uint8_t edge, std::vector<std::uint32_t>& keys) const = 0;   // 0x70
    virtual int GetPathsToExitEdge(std::uint32_t type, std::uint8_t edge, std::vector<std::uint32_t>& keys) const = 0;       // 0x78
    virtual bool UseElevation() const = 0;                                                                                   // 0x80
    virtual void SetUseElevationFlag(bool on) = 0;                                                                           // 0x88
    virtual bool GetDestinationPoint(const std::vector<cS3DVector3>* path, const cS3DVector3& from, cS3DVector3& out,
                                     int& index) const = 0;                                                                  // 0x90
    virtual int SnapPointToPath(cS3DVector3& point, const std::vector<cS3DVector3>* path) const = 0;                        // 0x98
    virtual int SnapPointToAnyPath(cS3DVector3& point, std::uint32_t type, std::uint32_t* key) const = 0;                    // 0xa0
    virtual bool GetDispatchDestination(std::uint32_t type, const cS3DVector3& from, cS3DVector3& out, std::uint32_t* key) const = 0;   // 0xa8
    virtual bool GetPathMidpoint(const std::vector<cS3DVector3>* path, cS3DVector3& out) const = 0;                         // 0xb0
    virtual bool GetRandomPathPoint(std::uint32_t key, cS3DVector3& out) const = 0;                                          // 0xb8
    virtual cISC4PathInfo* Clone() = 0;                                                                                      // 0xc0
    virtual cISC4PathInfo* CreateTransformedCopy(IPathNetworkOccupant* occupant) = 0;                                        // 0xc8
    virtual void RemoveWrongWayPaths(IPathNetworkOccupant* occupant) = 0;                                                    // 0xd0
    virtual void InitializeNetworkConnectionBitmap(IPathNetworkOccupant* occupant) = 0;                                      // 0xd8
    virtual void OffsetBy(float x, float y, float z) = 0;                                                                    // 0xe0
    virtual cISC4PathInfo* OffsetAndScissor(float x, float y, float z, const SC4RectFloat32& rect) = 0;                      // 0xe8
    virtual PathMap* GetPathMap() = 0;                                                                                       // 0xf0
    virtual int CopyPaths(std::uint8_t lane, std::uint32_t fromType, std::uint32_t toType, bool swapEdges) = 0;              // 0xf8
    virtual void MakeTunnelPaths(IPathNetworkOccupant* from, IPathNetworkOccupant* to) = 0;                                  // 0x100
    virtual int GetStopPointCount(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, std::uint8_t lane) const = 0;   // 0x108
    virtual int GetStopPointCount(std::uint32_t key) const = 0;                                                              // 0x110
    virtual bool GetStopPoint(cS3DVector3& out, std::uint32_t type, std::uint8_t entry, std::uint8_t exit, std::uint8_t lane) const = 0;   // 0x118
    virtual bool GetStopPoint(cS3DVector3& out, std::uint32_t key) const = 0;                                                // 0x120
    virtual std::uint32_t GetCenterLane(std::uint32_t type, std::uint8_t entry, std::uint8_t exit) const = 0;                // 0x128
    virtual bool GetJunctionKeys(std::uint32_t type, std::uint8_t entry, std::vector<std::uint32_t>& keys) const = 0;        // 0x130
    virtual bool GetPathIntersection(std::uint32_t key, const cS3DVector3& origin, const cS3DVector3& direction, float minDot,
                                     cS3DVector3& out, std::uint32_t& hitKey, int& segment) const = 0;                       // 0x138
    virtual bool VerifyPathEdges(IPathNetworkOccupant* occupant) = 0;                                                        // 0x140
};

class cSC4PathInfo final : public cGZPersistResource, public cISC4PathInfo {
public:
    cSC4PathInfo() = default;
    cSC4PathInfo(const cSC4PathInfo& other);   // 0x100458240

    // The services (city, language, random). Not owned; nullptr uses harmless defaults (right-hand traffic, clock-seeded cRZRandom).
    static void SetServices(IPathInfoServices* services);

    // ---- COM (0x1004583a0 / 0x10045d972 / 0x10045d980 and their non-virtual thunks)
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZPersistResource::AddRef(); }
    std::uint32_t Release() override { return cGZPersistResource::Release(); }

    // ---- queries
    const tPath* GetPath(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, std::uint8_t lane, float* length) const override;
    const tPath* GetPath(std::uint32_t key, float* length) const override;
    const tPath* GetBestPath(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, const cS3DVector3& near,
                             std::uint32_t* key, float* length) const override;
    std::vector<std::uint32_t> GetPathsNearStartPoint(std::uint32_t type, const cS3DVector3& point, std::uint8_t entry,
                                                     std::uint8_t exit, float radius) const override;
    std::vector<std::uint32_t> GetPathsNearEndPoint(std::uint32_t type, const cS3DVector3& point, std::uint8_t entry,
                                                   std::uint8_t exit, float radius) const override;
    const tPath* GetRandomPath(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, cRZRandomSuper* random,
                               float* length) const override;
    const tPath* GetRandomPathWildcard(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, cRZRandomSuper* random,
                                       std::uint32_t* key, float* length) const override;
    float GetPathLength(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, std::uint8_t lane) const override;
    float GetPathLength(std::uint32_t key) const override;
    int GetLaneCount(std::uint32_t type, std::uint8_t entry, std::uint8_t exit) const override;
    bool IsIntersection(std::uint32_t type) const override;
    int GetPathsFromEntryEdge(std::uint32_t type, std::uint8_t edge, std::vector<std::uint32_t>& keys) const override;
    int GetPathsToExitEdge(std::uint32_t type, std::uint8_t edge, std::vector<std::uint32_t>& keys) const override;
    bool UseElevation() const override { return (flags_ & 1) != 0; }
    void SetUseElevationFlag(bool on) override { flags_ = on ? (flags_ | 1) : (flags_ & 0xfe); }
    bool GetDestinationPoint(const std::vector<cS3DVector3>* path, const cS3DVector3& from, cS3DVector3& out, int& index) const override;
    int SnapPointToPath(cS3DVector3& point, const std::vector<cS3DVector3>* path) const override;
    int SnapPointToAnyPath(cS3DVector3& point, std::uint32_t type, std::uint32_t* key) const override;
    bool GetDispatchDestination(std::uint32_t type, const cS3DVector3& from, cS3DVector3& out, std::uint32_t* key) const override;
    bool GetPathMidpoint(const std::vector<cS3DVector3>* path, cS3DVector3& out) const override;
    bool GetRandomPathPoint(std::uint32_t key, cS3DVector3& out) const override;
    int GetStopPointCount(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, std::uint8_t lane) const override;
    int GetStopPointCount(std::uint32_t key) const override;
    bool GetStopPoint(cS3DVector3& out, std::uint32_t type, std::uint8_t entry, std::uint8_t exit, std::uint8_t lane) const override;
    bool GetStopPoint(cS3DVector3& out, std::uint32_t key) const override;
    std::uint32_t GetCenterLane(std::uint32_t type, std::uint8_t entry, std::uint8_t exit) const override;
    bool GetJunctionKeys(std::uint32_t type, std::uint8_t entry, std::vector<std::uint32_t>& keys) const override;
    bool GetPathIntersection(std::uint32_t key, const cS3DVector3& origin, const cS3DVector3& direction, float minDot,
                             cS3DVector3& out, std::uint32_t& hitKey, int& segment) const override;

    // ---- building and editing
    tPath* NewPath(std::uint32_t key);                                   // 0x100458cc2: nullptr when the key exists
    bool NewStopPoint(const cS3DVector3& point, std::uint32_t key);      // 0x100458fbc
    std::uint32_t GetUniquePathKey(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, int lane) const;   // 0x10045a8f0
    cISC4PathInfo* Clone() override;
    cISC4PathInfo* CreateTransformedCopy(IPathNetworkOccupant* occupant) override;
    void RemoveWrongWayPaths(IPathNetworkOccupant* occupant) override;
    void InitializeNetworkConnectionBitmap(IPathNetworkOccupant* occupant) override;
    void OffsetBy(float x, float y, float z) override;
    cISC4PathInfo* OffsetAndScissor(float x, float y, float z, const SC4RectFloat32& rect) override;
    void RemoveDuplicatePaths();                                         // 0x10045b4f2
    PathMap* GetPathMap() override { return &paths_; }
    int CopyPaths(std::uint8_t lane, std::uint32_t fromType, std::uint32_t toType, bool swapEdges) override;
    void MakeTunnelPaths(IPathNetworkOccupant* from, IPathNetworkOccupant* to) override;
    void MakeBidirectionalPaths();                                       // 0x10045c232
    bool VerifyPathEdges(IPathNetworkOccupant* occupant) override;

    const PathMap& Paths() const { return paths_; }
    std::uint8_t RawFlags() const { return flags_; }
    bool HasStopPointsOfType(std::uint32_t nibble) const { return nibble < 8 && typeFlags_[nibble] != 0; }

    // Public so tests can keep one on the stack; the game only Release()s heap objects.
    ~cSC4PathInfo() override = default;

private:
    PathMap paths_;                                   // +0x28
    std::uint8_t flags_ = 0;                          // +0x50
    std::uint8_t typeFlags_[8] = {0, 0, 0, 0, 0, 0, 0, 0};   // +0x51
};

}  // namespace sc4

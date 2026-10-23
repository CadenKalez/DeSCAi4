#include "sc4/PathInfo.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_map>

#include "rz/cRZRandom.h"
#include "sc4/GeometryUtilities.h"

namespace sc4 {

namespace {

IPathInfoServices* g_services = nullptr;

bool RightHandTraffic() { return g_services ? g_services->IsRightHandTraffic() : true; }

std::uint32_t ClockRandomRaw() {
    if (g_services) return g_services->RandomRaw();
    cRZRandom random(0xffffffffu);
    return random.RandomUint32Uniform();
}

double ClockRandomDouble(double lo, double hi) {
    if (g_services) return g_services->RandomDoubleRange(lo, hi);
    cRZRandom random(0xffffffffu);
    return random.RandomDoubleRangeUniform(lo, hi);
}

float Dist3(const cS3DVector3& a, const cS3DVector3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dz * dz + dy * dy + dx * dx);
}

// Mask of the key bits a (type, entry, exit) query compares: the lane byte is always ignored, an edge of 0xff is a wildcard.
std::uint32_t QueryMask(std::uint8_t entry, std::uint8_t exit) {
    std::uint32_t mask = 0xff00ffffu;
    if (entry == kPathEdgeWildcard) mask &= ~0xff00u;
    if (exit == kPathEdgeWildcard) mask &= ~0xffu;
    return mask;
}
std::uint32_t QueryKey(std::uint32_t type, std::uint8_t entry, std::uint8_t exit) {
    return (type << 24) | (std::uint32_t(entry) << 8) | exit;
}

// ---- the file-local helpers of the original (anonymous namespace) -----------------------------------------------------
// 0x10045b303: which side of the rectangle a point lies on (0.1 tolerance); 0xff when none.
std::uint8_t GetEdgeFromPoint(const SC4RectFloat32& r, const cS3DVector3& p) {
    if (std::fabs(p.x - r.x0) <= 0.1f) return 0;
    if (!(0.1f < std::fabs(p.z - r.z0))) return 1;
    if (!(0.1f < std::fabs(p.x - r.x1))) return 2;
    if (std::fabs(p.z - r.z1) <= 0.1f) return 3;
    return 0xff;
}

// 0x10045b372: the point where the segment from `a` towards `b` crosses the rectangle sides named in `mask` (bit 0 = x0,
// 1 = z0, 2 = x1, 3 = z1). The last side whose parameter is not smaller than the running maximum wins and `edge` records it.
// (The x0 test additionally requires a non-negative parameter.)
cS3DVector3 ComputeLineToRectIntersection(const SC4RectFloat32& r, std::uint32_t mask, const cS3DVector3& a, const cS3DVector3& b,
                                          std::uint8_t& edge) {
    float t = 0.0f;
    if (mask & 1) {
        const float u = (r.x0 - a.x) / (b.x - a.x);
        t = 0.0f;
        if (0.0f <= u) { edge = 0; t = u; }
    }
    if (mask & 2) {
        const float u = (r.z0 - a.z) / (b.z - a.z);
        if (t <= u) { edge = 1; t = u; }
    }
    if (mask & 4) {
        const float u = (r.x1 - a.x) / (b.x - a.x);
        if (t <= u) { edge = 2; t = u; }
    }
    if (mask & 8) {
        const float u = (r.z1 - a.z) / (b.z - a.z);
        if (t <= u) { edge = 3; t = u; }
    }
    cS3DVector3 out;
    out.x = (b.x - a.x) * t + a.x;
    out.y = (b.y - a.y) * t + a.y;
    out.z = (b.z - a.z) * t + a.z;
    return out;
}

// 0x10045b481: the length of a path = the summed 3D segment lengths.
void ComputePathLength(tPath& path) {
    float length = 0.0f;
    for (std::size_t i = 1; i < path.points.size(); ++i) length += Dist3(path.points[i], path.points[i - 1]);
    path.length = length;
}

// 0x10045cd4e: is a path end point within 1.0 of the cell border `edge` (0 = min x, 1 = min z, 2 = max x, 3 = max z)? Edges
// above 3 (including the 0xff wildcard) always pass.
bool VerifyPathPoint(std::uint8_t edge, const cS3DVector3& p, const float* rectMin, const float* rectMax) {
    float diff;
    switch (edge) {
        case 0: diff = p.x - rectMin[0]; break;
        case 1: diff = p.z - rectMin[1]; break;
        case 2: diff = p.x - rectMax[0]; break;
        case 3: diff = p.z - rectMax[1]; break;
        default: return true;
    }
    return std::fabs(diff) < 1.0f;
}

bool SamePoint(const cS3DVector3& a, const cS3DVector3& b) {
    return !(0.001f <= std::fabs(a.x - b.x)) && !(0.001f <= std::fabs(a.y - b.y)) && !(0.001f <= std::fabs(a.z - b.z));
}

// RemoveWrongWayPaths: per entry / exit edge, the one-way direction bit that forbids entering / leaving there
// (cSC4PathInfo::RemoveWrongWayPaths::kForbiddenEntryBit 0x100a21350, kForbiddenExitBit 0x100a21354).
constexpr std::uint8_t kForbiddenEntryBit[4] = {0x02, 0x08, 0x10, 0x40};
constexpr std::uint8_t kForbiddenExitBit[4] = {0x01, 0x04, 0x20, 0x80};

}  // namespace

void cSC4PathInfo::SetServices(IPathInfoServices* services) { g_services = services; }

// ---- construction / COM -----------------------------------------------------------------------------------------------

// 0x100458240: copies the table and the flag bytes (the resource sub-object starts fresh).
cSC4PathInfo::cSC4PathInfo(const cSC4PathInfo& other)
    : cGZPersistResource(), paths_(other.paths_), flags_(other.flags_) {
    for (int i = 0; i < 8; ++i) typeFlags_[i] = other.typeFlags_[i];
}

// 0x1004583a0 / 0x1004583e4: only the path info interface and the persist-resource interface answer (not iid 1).
bool cSC4PathInfo::QueryInterface(std::uint32_t iid, void** out) {
    if (out) *out = nullptr;
    void* found = nullptr;
    if (iid == kGZIID_PathInfoPersistResource) found = static_cast<cGZPersistResource*>(this);
    else if (iid == kGZIID_cISC4PathInfo) found = static_cast<cISC4PathInfo*>(this);
    else return false;
    if (out) *out = found;
    AddRef();
    return true;
}

// 0x100458424
cISC4PathInfo* cSC4PathInfo::Clone() {
    cSC4PathInfo* copy = new cSC4PathInfo(*this);
    copy->AddRef();
    return copy;
}

// ---- lookups ------------------------------------------------------------------------------------------------------------

// 0x100458470: builds the key (lane in bits 16..23) and forwards to GetPath(key).
const tPath* cSC4PathInfo::GetPath(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, std::uint8_t lane, float* length) const {
    return GetPath((type << 24) | (std::uint32_t(entry) << 8) | exit | (std::uint32_t(lane) << 16), length);
}

// 0x100458492
const tPath* cSC4PathInfo::GetPath(std::uint32_t key, float* length) const {
    auto it = paths_.find(key);
    if (it == paths_.end()) return nullptr;
    if (length) *length = it->second.length;
    return &it->second;
}

// 0x100458c66 / 0x100458c86
float cSC4PathInfo::GetPathLength(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, std::uint8_t lane) const {
    return GetPathLength((type << 24) | (std::uint32_t(entry) << 8) | exit | (std::uint32_t(lane) << 16));
}
float cSC4PathInfo::GetPathLength(std::uint32_t key) const {
    auto it = paths_.find(key);
    return it == paths_.end() ? 0.0f : it->second.length;
}

// 0x1004584ce: the path whose nearest point is closest to `near` (distance ignores the height unless elevation is used);
// distances within 0.01 of the best are taken or skipped by a coin flip from a clock-seeded generator.
const tPath* cSC4PathInfo::GetBestPath(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, const cS3DVector3& near,
                                       std::uint32_t* key, float* length) const {
    const std::uint32_t mask = QueryMask(entry, exit);
    const std::uint32_t query = QueryKey(type, entry, exit);
    const tPath* best = nullptr;
    float bestDistance = FLT_MAX;
    for (const auto& [k, path] : paths_) {
        if ((mask & (k ^ query)) != 0) continue;
        for (const cS3DVector3& p : path.points) {
            const float dx = near.x - p.x, dz = near.z - p.z;
            float dy = near.y - p.y;
            if (!UseElevation()) dy = 0.0f;
            const float d = std::sqrt(dz * dz + dy * dy + dx * dx);
            bool take = false;
            if (d < bestDistance) take = true;
            else if (std::fabs(d - bestDistance) < 0.01f) take = ClockRandomRaw() < 0x7fffffffu;   // near tie: coin flip
            if (take) {
                if (key) *key = k;
                if (length) *length = path.length;
                best = &path;
                bestDistance = d;
            }
        }
    }
    return best;
}

// 0x100458936: keys of paths whose FIRST point lies within `radius` of `point`, nearest first (a strictly nearer path goes to the
// head, others are appended).
std::vector<std::uint32_t> cSC4PathInfo::GetPathsNearStartPoint(std::uint32_t type, const cS3DVector3& point, std::uint8_t entry,
                                                              std::uint8_t exit, float radius) const {
    std::vector<std::uint32_t> keys;
    const std::uint32_t mask = QueryMask(entry, exit), query = QueryKey(type, entry, exit);
    float best = FLT_MAX;
    for (const auto& [k, path] : paths_) {
        if (((k ^ query) & mask) != 0 || path.points.empty()) continue;
        const float d = Dist3(path.points.front(), point);
        if (d <= radius) {
            if (best <= d) keys.push_back(k);
            else { best = d; keys.insert(keys.begin(), k); }
        }
    }
    return keys;
}

// 0x100458ace: the same for the LAST point of each path.
std::vector<std::uint32_t> cSC4PathInfo::GetPathsNearEndPoint(std::uint32_t type, const cS3DVector3& point, std::uint8_t entry,
                                                            std::uint8_t exit, float radius) const {
    std::vector<std::uint32_t> keys;
    const std::uint32_t mask = QueryMask(entry, exit), query = QueryKey(type, entry, exit);
    float best = FLT_MAX;
    for (const auto& [k, path] : paths_) {
        if (((k ^ query) & mask) != 0 || path.points.empty()) continue;
        const float d = Dist3(path.points.back(), point);
        if (d <= radius) {
            if (best <= d) keys.push_back(k);
            else { best = d; keys.insert(keys.begin(), k); }
        }
    }
    return keys;
}

// 0x1004586a8: a uniformly random path of the type (lane ignored, edges exact).
const tPath* cSC4PathInfo::GetRandomPath(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, cRZRandomSuper* random,
                                         float* length) const {
    const std::uint32_t query = QueryKey(type, entry, exit);
    std::vector<const tPath*> matches;
    for (const auto& [k, path] : paths_)
        if ((k & 0xff00ffffu) == query) matches.push_back(&path);
    if (matches.empty()) return nullptr;
    const tPath* chosen = matches[random->RandomUint32Uniform(std::uint32_t(matches.size()))];
    if (length) *length = chosen->length;
    return chosen;
}

// 0x1004587a8: the same with wildcards: type 0 matches every type, edge 0xff matches any edge.
const tPath* cSC4PathInfo::GetRandomPathWildcard(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, cRZRandomSuper* random,
                                                 std::uint32_t* key, float* length) const {
    std::uint32_t mask = type == 0 ? 0xf000ffffu : 0xff00ffffu;
    if (entry == kPathEdgeWildcard) mask &= ~0xff00u;
    if (exit == kPathEdgeWildcard) mask &= ~0xffu;
    const std::uint32_t query = QueryKey(type, entry, exit);
    std::vector<const tPath*> matches;
    std::vector<std::uint32_t> keys;
    for (const auto& [k, path] : paths_) {
        if (((k ^ query) & mask) != 0) continue;
        matches.push_back(&path);
        keys.push_back(k);
    }
    if (matches.empty()) return nullptr;
    const std::uint32_t index = random->RandomUint32Uniform(std::uint32_t(matches.size()));
    if (length) *length = matches[index]->length;
    if (key) *key = keys[index];
    return matches[index];
}

// 0x100459670: how many lanes (paths differing only in the lane byte) run between the two edges.
int cSC4PathInfo::GetLaneCount(std::uint32_t type, std::uint8_t entry, std::uint8_t exit) const {
    const std::uint32_t query = QueryKey(type, entry, exit);
    int count = 0;
    for (const auto& kv : paths_) count += (kv.first & 0xff00ffffu) == query;
    return count;
}

// 0x10045954a: type 0 reads the "has stop points" bit, other types their flag byte (indices above 7 read past the table in the
// original; false here).
bool cSC4PathInfo::IsIntersection(std::uint32_t type) const {
    if (type == 0) return (flags_ >> 1) & 1;
    return type < 8 && typeFlags_[type] != 0;
}

// 0x100459564 / 0x1004595ec: keys of the real (non stop point) paths of a type entering at / leaving by an edge.
int cSC4PathInfo::GetPathsFromEntryEdge(std::uint32_t type, std::uint8_t edge, std::vector<std::uint32_t>& keys) const {
    keys.clear();
    int count = 0;
    for (const auto& kv : paths_) {
        const std::uint32_t k = kv.first;
        if (k < kPathKeyStopFlag && ((k >> 24) & 0xf) == type && std::uint8_t(k >> 8) == edge) { keys.push_back(k); ++count; }
    }
    return count;
}
int cSC4PathInfo::GetPathsToExitEdge(std::uint32_t type, std::uint8_t edge, std::vector<std::uint32_t>& keys) const {
    keys.clear();
    int count = 0;
    for (const auto& kv : paths_) {
        const std::uint32_t k = kv.first;
        if (k < kPathKeyStopFlag && std::uint8_t(k) == edge && ((k >> 24) & 0xf) == type) { keys.push_back(k); ++count; }
    }
    return count;
}

// ---- stop points --------------------------------------------------------------------------------------------------------

// 0x10045930c: the mask includes the type nibble only for the "any type" query; wildcard bytes of either the query or the stored
// key match anything (the stop flag bit is forced into the query, so a type-0 query also counts ordinary paths).
int cSC4PathInfo::GetStopPointCount(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, std::uint8_t lane) const {
    std::uint32_t mask = type != 0 ? 0u : 0x0f000000u;
    if (entry == 0xff) mask |= 0xff00;
    if (exit == 0xff) mask |= 0xff;
    if (lane == 0xff) mask |= 0xff0000;
    const std::uint32_t query = (type << 24) | (std::uint32_t(entry) << 8) | exit | (std::uint32_t(lane) << 16);
    int count = 0;
    for (const auto& kv : paths_) {
        const std::uint32_t k = kv.first;
        std::uint32_t wild = (k & 0xff00) == 0xff00 ? 0xff00u : 0u;
        wild |= std::uint8_t(k) == 0xff ? 0xffu : 0u;
        count += (k | mask | wild) == (wild | query | mask | kPathKeyStopFlag);
    }
    return count;
}

// 0x1004593c2: by key; the lane wildcard is 0xff in bits 16..23 of the query only.
int cSC4PathInfo::GetStopPointCount(std::uint32_t key) const {
    const std::uint32_t laneWild = (key & 0xff0000) == 0xff0000 ? 0xff0000u : 0u;
    const std::uint32_t query = (key & 0x0fffffffu) | kPathKeyStopFlag;
    int count = 0;
    for (const auto& kv : paths_) {
        const std::uint32_t k = kv.first;
        std::uint32_t wild = (k & 0xff00) == 0xff00 ? 0xff00u : 0u;
        wild |= std::uint8_t(k) == 0xff ? 0xffu : 0u;
        if ((k | laneWild) == (laneWild | query)) ++count;
        else count += (k | wild) == (wild | query);
    }
    return count;
}

// 0x10045944e
bool cSC4PathInfo::GetStopPoint(cS3DVector3& out, std::uint32_t type, std::uint8_t entry, std::uint8_t exit, std::uint8_t lane) const {
    return GetStopPoint(out, (type << 24) | (std::uint32_t(entry) << 8) | exit | (std::uint32_t(lane) << 16) | kPathKeyStopFlag);
}

// 0x100459478: an exact key first, then the first stop point whose key matches with either side's 0xff wildcards.
bool cSC4PathInfo::GetStopPoint(cS3DVector3& out, std::uint32_t key) const {
    auto it = paths_.find(key);
    if (it == paths_.end()) {
        for (it = paths_.begin(); it != paths_.end(); ++it) {
            const std::uint32_t k = it->first;
            std::uint32_t m = (k & 0xff00) != 0xff00 ? 0xffffffffu : 0xffff00ffu;
            if (std::uint8_t(k) == 0xff) m &= 0xffffff00u;
            if ((key & 0xff00) == 0xff00) m &= 0xffff00ffu;
            if (std::uint8_t(key) == 0xff) m &= 0xffffff00u;
            if ((m & (k ^ ((key & 0x0fffffffu) | kPathKeyStopFlag))) == 0) break;
        }
        if (it == paths_.end()) return false;
    }
    if (it->second.points.empty()) return false;
    out = it->second.points.front();
    return true;
}

// ---- building -----------------------------------------------------------------------------------------------------------

// 0x100458cc2: a new empty path under `key`; nullptr (and nothing changed) when the key is taken.
tPath* cSC4PathInfo::NewPath(std::uint32_t key) {
    auto [it, inserted] = paths_.try_emplace(key);
    return inserted ? &it->second : nullptr;
}

// 0x100458fbc: a one-point "path" for a stop position. Also flags that stop points exist (bit 1 of the flag byte and the byte of
// the key's type nibble; the original writes that byte unchecked for nibbles above 7).
bool cSC4PathInfo::NewStopPoint(const cS3DVector3& point, std::uint32_t key) {
    auto [it, inserted] = paths_.try_emplace(key);
    if (!inserted) return false;
    it->second.length = 0.0f;
    it->second.flags = 0;
    it->second.points.assign(1, point);
    const std::uint32_t nibble = (key >> 24) & 0xf;
    if (nibble < 8) typeFlags_[nibble] = 1;
    flags_ |= 2;
    return true;
}

// 0x10045a8f0: the first unused lane (from `lane` up to 26) for a (type, entry, exit); lane 26 is returned even if taken.
std::uint32_t cSC4PathInfo::GetUniquePathKey(std::uint32_t type, std::uint8_t entry, std::uint8_t exit, int lane) const {
    std::uint32_t key;
    for (;; ++lane) {
        key = (std::uint32_t(lane << 16) & 0xff0000u) | (std::uint32_t(entry) << 8) | (type << 24) | exit;
        const bool taken = paths_.find(key) != paths_.end();
        if (lane > 0x19) break;
        if (!taken) break;
    }
    return key;
}

// 0x10045aa24
void cSC4PathInfo::OffsetBy(float x, float y, float z) {
    for (auto& kv : paths_)
        for (cS3DVector3& p : kv.second.points) { p.x += x; p.y += y; p.z += z; }
}

// 0x100459 InitializeNetworkConnectionBitmap (0x10045a9e2): tells the occupant every real path's type and edges.
void cSC4PathInfo::InitializeNetworkConnectionBitmap(IPathNetworkOccupant* occupant) {
    for (const auto& kv : paths_) {
        const std::uint32_t k = kv.first;
        if (k < kPathKeyStopFlag) occupant->AddPathConnection((k >> 24) & 0xf, (k >> 8) & 0xff, k & 0xff);
    }
}

// ---- point snapping and geometry queries ----------------------------------------------------------------------------------

// 0x10045b608: from `index` onwards, the closest path point ahead of the travel direction (the dot of the unit vector to the
// point with the path's overall direction must exceed -0.2). `index` is updated to the chosen point.
bool cSC4PathInfo::GetDestinationPoint(const std::vector<cS3DVector3>* pathPtr, const cS3DVector3& from, cS3DVector3& out,
                                       int& index) const {
    const std::vector<cS3DVector3>& path = *pathPtr;
    const std::int64_t n = std::int64_t(path.size());
    if (!(index >= 0 && index < n)) index = 0;
    if (path.empty()) return false;
    const cS3DVector3& first = path.front();
    const cS3DVector3& last = path.back();
    const float dirY = last.y - first.y;
    const float dirZ = last.z - first.z, dirX = last.x - first.x;
    const float dirInv = 1.0f / std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    float best = FLT_MAX;
    bool found = false;
    for (int i = 0; i < n; ++i) {
        if (!(index <= i)) continue;
        const cS3DVector3& p = path[i];
        float vy = p.y - from.y;
        const float vx = p.x - from.x, vz = p.z - from.z;
        if (!UseElevation()) vy = 0.0f;
        const float d = std::sqrt(vz * vz + vy * vy + vx * vx);
        if (0.0f < d) {
            const float inv = 1.0f / d;
            const float termX = vx * inv * dirInv * dirX;
            const float termY = vy * inv * dirY * dirInv;
            const float termZ = vz * inv * dirInv * dirZ;
            if (-0.2f < termX + termY + termZ) {
                found = true;
                if (d < best) {
                    out = p;
                    index = i;
                    best = d;
                }
            }
        }
    }
    return found;
}

// 0x10045b80e: moves `point` onto the path: the intersection of the path segment with the 16-unit line through the point
// perpendicular to it; falls back to the nearer end point. Returns the segment index, -1 for a missing / empty path.
// Quirk kept: the height is interpolated with the ratio of the SQUARED 2D lengths.
int cSC4PathInfo::SnapPointToPath(cS3DVector3& point, const std::vector<cS3DVector3>* pathPtr) const {
    if (!pathPtr || pathPtr->empty()) return -1;
    const std::vector<cS3DVector3>& path = *pathPtr;
    const std::size_t n = path.size();
    if (n >= 2) {
        for (std::size_t i = 1; i < n; ++i) {
            const cS3DVector3& a = path[i - 1];
            const cS3DVector3& b = path[i];
            float dx = b.x - a.x;
            const float dy = b.y - a.y;
            float dz = b.z - a.z;
            const float lengthSquared = dz * dz + (dx * dx + dy * dy);
            if (lengthSquared > 0.0f) {
                const float scale = 8.0f / std::sqrt(lengthSquared);
                dx *= scale;
                dz *= scale;
            }
            const float p0[2] = {point.x + dz, point.z - dx};
            const float p1[2] = {point.x - dz, point.z + dx};
            const float s0[2] = {a.x, a.z};
            const float s1[2] = {b.x, b.z};
            float hit[2];
            if (SC4GeometryUtilities::LinesIntersect2D(p0, p1, s0, s1, hit)) {
                point.x = hit[0];
                point.z = hit[1];
                const float denominator = SC4GeometryUtilities::LengthOfLineSquared2D(s0, s1);
                const float numerator = SC4GeometryUtilities::LengthOfLineSquared2D(s0, hit);
                const float ratio = denominator < 0.0001f ? 0.0f : numerator / denominator;
                point.y = (b.y - a.y) * ratio + a.y;
                return int(i - 1);
            }
        }
    }
    const cS3DVector3& first = path.front();
    const cS3DVector3& last = path.back();
    if (Dist3(point, first) <= Dist3(point, last)) {
        point = first;
        return 0;
    }
    point = last;
    return int(n) - 2;
}

// 0x10045baa8: snaps onto the nearest real path of the type (0 = any); stops early at a distance of 0.001. Returns the segment
// index or -1.
int cSC4PathInfo::SnapPointToAnyPath(cS3DVector3& point, std::uint32_t type, std::uint32_t* key) const {
    int result = -1;
    float best = FLT_MAX;
    const cS3DVector3 original = point;
    for (const auto& [k, path] : paths_) {
        if (k >= kPathKeyStopFlag) continue;
        if (type != 0 && ((k >> 24) & 0xf) != type) continue;
        cS3DVector3 candidate = original;
        const int index = SnapPointToPath(candidate, &path.points);
        if (index == -1) continue;
        const float d = Dist3(candidate, original);
        if (d < best) {
            point = candidate;
            if (key) *key = k;
            best = d;
            result = index;
            if (d <= 0.001f) return index;
        }
    }
    return result;
}

// 0x10045bd4e: the point half way along the path. Quirk kept: after the half-way segment is found the loop does not stop and
// does not advance its origin, so every later point re-evaluates against the stale origin and overwrites the answer whenever
// the half-way length is still reached - normally the result ends up on the line from that origin towards the LAST point.
bool cSC4PathInfo::GetPathMidpoint(const std::vector<cS3DVector3>* pathPtr, cS3DVector3& out) const {
    if (!pathPtr) return false;
    const std::vector<cS3DVector3>& path = *pathPtr;
    if (path.size() < 2) return false;
    float total = 0.0f;
    for (std::size_t i = 1; i < path.size(); ++i) {
        const float dx = path[i].x - path[i - 1].x, dy = path[i].y - path[i - 1].y, dz = path[i].z - path[i - 1].z;
        total += std::sqrt(dz * dz + dy * dy + dx * dx);
    }
    bool found = false;
    float walked = 0.0f;
    cS3DVector3 origin = path.front();
    const float half = total * 0.5f;
    for (std::size_t i = 1; i < path.size(); ++i) {
        const cS3DVector3& cur = path[i];
        const float dx = cur.x - origin.x, dy = cur.y - origin.y, dz = cur.z - origin.z;
        const float segment = std::sqrt(dz * dz + dy * dy + dx * dx);
        const float reached = walked + segment;
        if (half <= reached) {
            const float inv = 1.0f / segment;
            const float rest = half - walked;
            out.x = dx * inv * rest + origin.x;
            out.y = dy * inv * rest + origin.y;
            out.z = origin.z + rest * dz * inv;
            found = true;
        } else {
            origin = cur;
            walked = reached;
        }
    }
    return found;
}

// 0x10045bc0a: of the real paths of the type, the midpoint farthest from `from`.
bool cSC4PathInfo::GetDispatchDestination(std::uint32_t type, const cS3DVector3& from, cS3DVector3& out, std::uint32_t* key) const {
    cS3DVector3 mid;
    bool found = false;
    float farthest = 0.0f;
    for (const auto& [k, path] : paths_) {
        if (type != 0 && ((k >> 24) & 0xf) != type) continue;
        if (!GetPathMidpoint(&path.points, mid)) continue;
        const float dx = from.x - mid.x, dz = from.z - mid.z;
        float dy = from.y - mid.y;
        if (!UseElevation()) dy = 0.0f;
        const float d = std::sqrt(dz * dz + dy * dy + dx * dx);
        if (farthest < d) {
            out = mid;
            found = true;
            farthest = d;
            if (key) *key = k;
        }
    }
    return found;
}

// 0x10045bef4: a point at a random distance (clock-seeded generator) along the path.
bool cSC4PathInfo::GetRandomPathPoint(std::uint32_t key, cS3DVector3& out) const {
    float length = 0.0f;
    const tPath* path = GetPath(key, &length);
    if (!path) return false;
    float distance = float(ClockRandomDouble(0.0, double(length)));
    const auto& pts = path->points;
    if (pts.size() < 2) return false;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const cS3DVector3& a = pts[i - 1];
        const cS3DVector3& b = pts[i];
        const float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
        const float segment = std::sqrt(dz * dz + dy * dy + dx * dx);
        if (distance < segment) {
            const float inv = 1.0f / segment;
            out.x = a.x + dx * inv * distance;
            out.y = dy * inv * distance + a.y;
            out.z = a.z + dz * inv * distance;
            return true;
        }
        distance -= segment;
    }
    return false;
}

// 0x10045c028
bool cSC4PathInfo::GetJunctionKeys(std::uint32_t type, std::uint8_t entry, std::vector<std::uint32_t>& keys) const {
    keys.clear();
    for (const auto& [k, path] : paths_)
        if (std::uint8_t(k >> 8) == entry && ((k >> 24) & 0xf) == type && (path.flags & kPathFlagJunction)) keys.push_back(k);
    return !keys.empty();
}

// ---- copying paths ------------------------------------------------------------------------------------------------------

// 0x10045c3d0: copies the paths of `fromType` (0 = any) with the given lane (0xff = any) to type `toType`, swapping entry and exit
// and reversing the points when `swapEdges`. Existing keys are kept. Returns the number of paths created.
int cSC4PathInfo::CopyPaths(std::uint8_t lane, std::uint32_t fromType, std::uint32_t toType, bool swapEdges) {
    const PathMap snapshot = paths_;
    int created = 0;
    for (const auto& [k, src] : snapshot) {
        if (!(fromType == 0 || ((k >> 24) & 0xf) == fromType)) continue;
        if (!(lane == 0xff || std::uint8_t(k >> 16) == lane)) continue;
        std::uint32_t entryField = k >> 8, exitField = k;
        if (swapEdges) { entryField = k; exitField = k >> 8; }
        const std::uint32_t newKey = (exitField & 0xff) | (k & 0xff0000u) | (k & 0xf0000000u) | (toType << 24) | ((entryField & 0xff) << 8);
        if (paths_.find(newKey) != paths_.end()) continue;
        tPath* dest = NewPath(newKey);
        if (!dest) continue;
        dest->points.reserve(src.points.size());
        if (!swapEdges) dest->points.assign(src.points.begin(), src.points.end());
        else dest->points.assign(src.points.rbegin(), src.points.rend());
        dest->length = src.length;
        dest->flags = src.flags;
        ++created;
    }
    return created;
}

// 0x10045c232: for each type-2 path a reversed twin (entry and exit swapped) on the first free lane.
void cSC4PathInfo::MakeBidirectionalPaths() {
    const PathMap snapshot = paths_;
    for (const auto& [k, src] : snapshot) {
        if ((k & 0xff000000u) != 0x02000000u) continue;
        const std::uint32_t swapped = (((k & 0xff00u) << 8) | (k << 24)) >> 16;
        std::uint32_t lane = (k >> 16) & 0xff;
        std::uint32_t key = swapped + 0x2000000u + (k & 0xff0000u);
        bool taken = paths_.find(key) != paths_.end();
        if (std::uint8_t(k >> 16) < 0x1a && taken) {
            std::uint32_t laneShifted = lane << 16;
            do {
                ++lane;
                laneShifted += 0x10000;
                key = laneShifted | swapped | 0x2000000u;
                taken = paths_.find(key) != paths_.end();
                if (lane > 0x19) break;
            } while (taken);
        }
        if (taken) continue;
        tPath* dest = NewPath(key);
        if (!dest) continue;
        dest->points.reserve(src.points.size());
        dest->points.assign(src.points.rbegin(), src.points.rend());
        dest->length = src.length;
        dest->flags = src.flags;
    }
}

// 0x10045c0bc: appends to each of this piece's paths leaving towards `to` the first point of the neighbour's matching path, when
// it is more than 1.0 from the current last point.
void cSC4PathInfo::MakeTunnelPaths(IPathNetworkOccupant* from, IPathNetworkOccupant* to) {
    SC4RectInt32 a, b;
    from->GetBoundingCityCells(a);
    to->GetBoundingCityCells(b);
    std::uint32_t direction = 2;
    if (b.x0 <= a.x0) {
        direction = 3;
        if (b.z0 <= a.z0) direction = (b.z0 < a.z0 && a.x0 <= b.x0) ? 1 : 0;
    }
    const cSC4PathInfo* other = to->GetPathInfo();
    if (!other) return;
    for (auto& [k, path] : paths_) {
        if (direction != (k & 0xff)) continue;
        const tPath* match = other->GetPath(k, nullptr);
        if (!match || match->points.empty()) continue;
        const cS3DVector3 first = match->points.front();
        if (path.points.empty()) continue;
        if (1.0f < Dist3(path.points.back(), first)) path.points.push_back(first);
    }
}

// 0x10045c5e0: the lane of (type, entry, exit) whose first point is closest to the middle of the lanes' first points, measured
// along the axis (x or z) where they spread more. With no match the lane-0 key is returned.
std::uint32_t cSC4PathInfo::GetCenterLane(std::uint32_t type, std::uint8_t entry, std::uint8_t exit) const {
    std::uint32_t result = QueryKey(type, entry, exit);
    const std::uint32_t query = result;
    struct Item { std::uint32_t key; float x, z; };
    std::vector<Item> items;
    float minX = 0, maxX = 0, minZ = 0, maxZ = 0;
    for (const auto& [k, path] : paths_) {
        if ((k & 0xff00ffffu) != query || path.points.empty()) continue;
        const cS3DVector3& p = path.points.front();
        if (items.empty()) { minX = maxX = p.x; minZ = maxZ = p.z; }
        else {
            minX = std::min(p.x, minX);
            minZ = std::min(p.z, minZ);
            maxX = std::max(p.x, maxX);
            maxZ = std::max(p.z, maxZ);
        }
        items.push_back({k, p.x, p.z});
    }
    if (items.empty()) return result;
    const float rangeZ = std::fabs(maxZ - minZ), rangeX = std::fabs(maxX - minX);
    const bool useZ = rangeX < rangeZ;
    const float center = (useZ ? minZ : minX) + (useZ ? (maxZ - minZ) : (maxX - minX)) * 0.5f;
    float bestDelta = FLT_MAX;
    for (const Item& item : items) {
        const float delta = std::fabs((useZ ? item.z : item.x) - center);
        if (delta < bestDelta) {
            result = item.key;
            bestDelta = delta;
        }
    }
    return result;
}

// 0x10045c870: where the 48-unit ray (origin + direction * 48) first meets the paths of the same type as `key` that run roughly
// parallel to it (direction dot >= minDot); returns the hit key, the segment index and the hit point at the origin's height.
bool cSC4PathInfo::GetPathIntersection(std::uint32_t key, const cS3DVector3& origin, const cS3DVector3& direction, float minDot,
                                       cS3DVector3& out, std::uint32_t& hitKey, int& segment) const {
    auto reference = paths_.find(key);
    if (reference == paths_.end() || reference->second.points.empty()) return false;
    const float rayStart[2] = {origin.x, origin.z};
    const float rayEnd[2] = {direction.x * 16.0f * 3.0f + origin.x, direction.z * 16.0f * 3.0f + origin.z};
    const auto& refPts = reference->second.points;
    const float refDy = refPts.back().y - refPts.front().y;
    const float refDz = refPts.back().z - refPts.front().z, refDx = refPts.back().x - refPts.front().x;
    const float refInv = 1.0f / std::sqrt(refDx * refDx + refDy * refDy + refDz * refDz);
    const float refNy = refDy * refInv;
    float bestDistance = FLT_MAX;
    bool found = false;
    cS3DVector3 hit;
    for (const auto& [k, path] : paths_) {
        if (!((k ^ key) < 0x1000000u && k != key)) continue;
        const auto& pts = path.points;
        if (pts.empty()) continue;
        const float dy = pts.back().y - pts.front().y;
        const float dz = pts.back().z - pts.front().z, dx = pts.back().x - pts.front().x;
        const float inv = 1.0f / std::sqrt(dx * dx + dy * dy + dz * dz);
        const float dot = inv * dx * refInv * refDx + dy * inv * refNy + inv * dz * refInv * refDz;
        if (!(minDot <= dot) || pts.size() < 2) continue;
        int index = 0;
        float prevX = pts[0].x, prevZ = pts[0].z;
        for (std::size_t i = 1; i < pts.size(); ++i, ++index) {
            const float curX = pts[i].x, curZ = pts[i].z;
            const float s0[2] = {prevX, prevZ};
            const float s1[2] = {curX, curZ};
            float at[2];
            if (SC4GeometryUtilities::LinesIntersect2D(s0, s1, rayStart, rayEnd, at)) {
                const float distance = SC4GeometryUtilities::LengthOfLine2D(at, rayStart);
                if (distance < bestDistance) {
                    hit.x = at[0];
                    hit.z = at[1];
                    hit.y = origin.y;
                    segment = index;
                    found = true;
                    hitKey = k;
                    bestDistance = distance;
                }
            }
            prevX = curX;
            prevZ = curZ;
        }
    }
    if (!found) return false;
    out = hit;
    return true;
}

// 0x10045cbee: marks (flag bit 1) every real path whose end points are not within 1.0 of the border sides it claims, using the
// piece's cell as a 16x16 rectangle. Returns false when any path was marked.
bool cSC4PathInfo::VerifyPathEdges(IPathNetworkOccupant* occupant) {
    std::uint32_t cellX = 0, cellZ = 0;
    occupant->GetOccupiedCell(cellX, cellZ);
    float worldX = 0, worldZ = 0;
    if (g_services) g_services->CellCenterToPosition(int(cellX), int(cellZ), worldX, worldZ);
    const float rectMin[2] = {worldX - 8.0f, worldZ - 8.0f};
    const float rectMax[2] = {worldX + 8.0f, worldZ + 8.0f};
    bool ok = true;
    for (auto& [k, path] : paths_) {
        if (k >= kPathKeyStopFlag || path.points.empty()) continue;
        if (!VerifyPathPoint(std::uint8_t(k >> 8), path.points.front(), rectMin, rectMax)) { path.flags |= kPathFlagBadEdge; ok = false; }
        if (!VerifyPathPoint(std::uint8_t(k), path.points.back(), rectMin, rectMax)) { path.flags |= kPathFlagBadEdge; ok = false; }
    }
    return ok;
}

// 0x10045b4f2: erases (restarting from the first path each time) any path that has a same-edge twin on another lane whose first
// and last points both lie within 1.0 - the OUTER path of the pair is the one removed.
void cSC4PathInfo::RemoveDuplicatePaths() {
    auto outer = paths_.begin();
    while (outer != paths_.end()) {
        bool erased = false;
        for (auto inner = paths_.begin(); inner != paths_.end(); ++inner) {
            if (outer->first == inner->first) continue;
            if (((inner->first ^ outer->first) & 0xff00ffffu) != 0) continue;
            const auto& a = outer->second.points;
            const auto& b = inner->second.points;
            if (a.empty() || b.empty()) continue;
            if (!(Dist3(a.front(), b.front()) < 1.0f)) continue;
            if (Dist3(a.back(), b.back()) < 1.0f) {
                paths_.erase(outer);
                erased = true;
                break;
            }
        }
        outer = erased ? paths_.begin() : std::next(outer);
    }
}

// ---- RemoveWrongWayPaths ------------------------------------------------------------------------------------------------

// 0x10045a558: drops road paths that run against the piece's one-way directions or use an inactive edge; a real path that
// is only wrong in its direction (and whose reverse is allowed) is replaced by its reverse on a free lane.
void cSC4PathInfo::RemoveWrongWayPaths(IPathNetworkOccupant* occupant) {
    const std::uint8_t oneWay = occupant->GetOneWayDirections();
    const std::uint32_t activeOfType10 = occupant->GetActiveEdgesForType(10);
    std::uint32_t activeMask = 0;
    for (std::uint32_t type = 0; type <= 12; ++type)
        if ((0xc49u >> type) & 1) activeMask |= occupant->GetActiveEdgesForTypeNoGroup(type);

    std::unordered_multimap<std::uint32_t, tPath> reversed;
    auto it = paths_.begin();
    while (it != paths_.end()) {
        const std::uint32_t k = it->first;
        bool erase = false;
        if ((k & 0xf000000u) == 0x1000000u) {
            const std::uint32_t entryField = k >> 8;
            const std::uint8_t entry = std::uint8_t(entryField), exit = std::uint8_t(k);
            const bool entryActive = entry > 3 || ((activeMask >> (entryField & 0x1f)) & 1);
            const bool exitActive = exit > 3 || ((activeMask >> (k & 0x1f)) & 1);
            const bool entryForbidden = entry < 4 && (kForbiddenEntryBit[entry] & oneWay) != 0;
            const bool exitForbidden = exit < 4 && (kForbiddenExitBit[exit] & oneWay) != 0;
            if (entryActive && exitActive && (entryForbidden || exitForbidden)) {
                erase = true;
                const bool reverseAllowed =
                    k < kPathKeyStopFlag && (exit > 3 || (kForbiddenEntryBit[exit] & oneWay) == 0) &&
                    (entry > 3 || ((kForbiddenExitBit[entry] & oneWay) == 0 && ((activeOfType10 >> (entryField & 0xff & 0x1f)) & 1) != 0)) &&
                    (exit > 3 || ((activeOfType10 >> (k & 0x1f)) & 1) != 0);
                if (reverseAllowed) {
                    const std::uint32_t reverseKey = (entryField & 0xff) + 0x1000000u + ((k & 0xf0ff0000u) | (std::uint32_t(exit) << 8));
                    auto added = reversed.emplace(reverseKey, tPath{});
                    added->second.flags = it->second.flags;
                    added->second.length = it->second.length;
                    added->second.points.assign(it->second.points.rbegin(), it->second.points.rend());
                }
            }
        }
        if (erase) it = paths_.erase(it);
        else ++it;
    }
    for (const auto& [k, path] : reversed) {
        const std::uint32_t key = GetUniquePathKey((k >> 24) & 0xf, std::uint8_t(k >> 8), std::uint8_t(k), int((k >> 16) & 0xff));
        tPath& dest = paths_[key];
        if (&dest != &path) dest.points.assign(path.points.begin(), path.points.end());
        dest.flags = path.flags;
        dest.length = path.length;
    }
}

// ---- CreateTransformedCopy ----------------------------------------------------------------------------------------------

// 0x1004596ac: the path set of a piece placed with the occupant's rotation / flip: every point is rotated about the vertical axis
// by quarter turns, moved to the occupant's position, and (without elevation) put on the terrain via GetAltitude; edge numbers
// rotate with the piece; a flipped piece mirrors them, reverses lane numbers and (for stop points) keeps wildcards. Two-point
// paths get a mid point inserted when the terrain bends more than 0.2 under them.
cISC4PathInfo* cSC4PathInfo::CreateTransformedCopy(IPathNetworkOccupant* occupant) {
    cSC4PathInfo* result = new cSC4PathInfo();
    occupant->PieceId();
    const bool flip = occupant->GetFlip();
    const std::uint8_t rotation = occupant->GetRotation();
    const float angle = float(rotation) * 1.5707964f;
    const float sinA = std::sin(angle), cosA = std::cos(angle);
    const float zero = 0.0f;
    const float sinTimesZero = sinA * zero;
    const float F = sinTimesZero + zero;              // fVar29
    float A = cosA, B = F, C = zero - sinA;           // x, y, z factors of the new x
    if (flip) { A = -A; B = -B; C = -C; }
    const float D = -(cosA * zero);
    const float E = 1.0f - sinTimesZero * zero;
    float position[3] = {0, 0, 0};
    occupant->GetPosition(position);
    const bool rightHand = RightHandTraffic();
    const bool useElevation = UseElevation();

    auto transform = [&](cS3DVector3 p) {
        occupant->TransformNode(p);
        const float x = p.x, y = p.y, z = p.z;
        float t1 = B * y, t2 = A * x;
        float sumX = t1 + t2;
        float t3 = C * z;
        float newX = t3 + sumX;
        float u1 = E * y, u2 = x * D;
        float sumY = u1 + u2;
        float u3 = F * z;
        float newY = u3 + sumY;
        float v1 = sinA * x, v2 = y * D;
        float sumZ = v2 + v1;
        float v3 = z * cosA;
        float newZ = v3 + sumZ;
        cS3DVector3 out;
        out.x = newX + position[0];
        out.y = newY + position[1];
        out.z = newZ + position[2];
        if (!useElevation) out.y = occupant->GetAltitude(out.x, out.z);
        return out;
    };

    std::uint32_t maxLane = 0;
    for (const auto& kv : paths_) maxLane = std::max<std::uint32_t>(maxLane, std::uint8_t(kv.first >> 16));

    auto rotateEdge = [&](std::uint32_t field) -> std::uint32_t {
        const std::uint8_t edge = std::uint8_t(field);
        if (rotation == 0 || edge == 0xff) return edge;
        return std::uint8_t(edge + rotation) & 3u;
    };
    auto mirrorEdge = [](std::uint8_t e) -> std::uint8_t {
        if (e == 0xff) return 0xff;
        if (e == 0) return 2;
        if (e == 2) return 0;
        return e;
    };

    for (const auto& [k, src] : paths_) {
        const std::uint32_t type = (k >> 24) & 0xf;
        const std::uint32_t stopNibble = k >> 28;
        std::uint32_t entry = rotateEdge(k >> 8);
        std::uint32_t exit = rotateEdge(k);
        const std::uint32_t laneField = k >> 16;
        if (stopNibble == 1) {
            if (src.points.empty()) continue;
            std::uint32_t lane = laneField;
            if (flip) {
                lane = maxLane - (laneField & 0xff);
                if (std::uint8_t(laneField) == 0xff) lane = laneField & 0xff;
            }
            cS3DVector3 p = transform(src.points.front());
            const std::uint32_t newKey = exit | (type << 24) | (k & 0xf0000000u) | ((entry & 0xff) << 8) | ((lane & 0xff) << 16);
            result->NewStopPoint(p, newKey);
        } else if (stopNibble == 0) {
            std::uint8_t entryByte = std::uint8_t(entry), exitByte = std::uint8_t(exit);
            std::uint32_t lane = laneField & 0xff;
            bool reversePoints = false;
            std::uint8_t first = entryByte, second = exitByte;   // R9B (entry candidate) and CL (exit candidate)
            if (flip) {
                first = mirrorEdge(exitByte);
                second = mirrorEdge(entryByte);
                lane = (maxLane - lane) & 0xffffffffu;
                reversePoints = true;
            }
            const bool plainSides = ((type | 4) != 5) || rightHand;   // false: swap the two edges
            std::uint8_t keyEntry = first, keyExit = second;
            if (!plainSides) { keyEntry = second; keyExit = first; }
            const std::uint32_t newKey = (type << 24) | ((lane & 0xff) << 16) | (std::uint32_t(keyEntry) << 8) | keyExit;
            tPath* dest = result->NewPath(newKey);
            if (!dest) continue;
            dest->length = 0.0f;
            dest->flags = src.flags;
            dest->points.reserve(src.points.size());
            const bool swapSides = !plainSides;
            const bool forward = reversePoints == swapSides;
            auto addPoint = [&](const cS3DVector3& original, bool isFirst, cS3DVector3& previous) {
                cS3DVector3 p = transform(original);
                dest->points.push_back(p);
                if (!isFirst) dest->length = Dist3(p, previous) + dest->length;
                previous = p;
            };
            cS3DVector3 previous;
            bool isFirst = true;
            if (forward) {
                for (const cS3DVector3& original : src.points) { addPoint(original, isFirst, previous); isFirst = false; }
            } else {
                for (auto rit = src.points.rbegin(); rit != src.points.rend(); ++rit) { addPoint(*rit, isFirst, previous); isFirst = false; }
            }
            if (src.points.size() == 2 && !useElevation && dest->points.size() >= 2) {
                const cS3DVector3 p0 = dest->points.front(), p1 = dest->points.back();
                cS3DVector3 mid;
                mid.x = (p1.x - p0.x) * 0.5f + p0.x;
                mid.y = (p1.y - p0.y) * 0.5f + p0.y;
                mid.z = (p1.z - p0.z) * 0.5f + p0.z;
                const float altitude = occupant->GetAltitude(mid.x, mid.z);
                if (0.2f < std::fabs(altitude - mid.y)) {
                    mid.y = altitude;
                    dest->points.insert(dest->points.begin() + 1, mid);
                    dest->length = Dist3(p1, mid) + Dist3(mid, p0);
                }
            }
        }
    }

    if (flip) {
        // The junction flag follows the reversed direction: move it to the twin path (entry and exit swapped).
        std::vector<std::uint32_t> flagged;
        for (auto& [k, path] : result->paths_) {
            if (path.flags & kPathFlagJunction) {
                path.flags &= ~kPathFlagJunction;
                flagged.push_back(k);
            }
        }
        for (std::uint32_t k : flagged)
            for (auto& [nodeKey, path] : result->paths_)
                if (nodeKey < kPathKeyStopFlag && std::uint8_t(nodeKey) == std::uint8_t(k >> 8) && std::uint8_t(nodeKey >> 8) == std::uint8_t(k))
                    path.flags |= kPathFlagJunction;
    }
    result->SetUseElevationFlag(UseElevation());
    result->AddRef();
    return result;
}

// ---- OffsetAndScissor ---------------------------------------------------------------------------------------------------

// 0x10045aa70: a copy of the paths moved by (x, y, z) and clipped to `rect` (Cohen-Sutherland style): a path entering and leaving
// the rectangle becomes one clipped path per inside run, keyed by the border sides where it enters / leaves; paths wholly outside
// vanish unless a segment cuts through a corner. Duplicate twins are removed at the end.
cISC4PathInfo* cSC4PathInfo::OffsetAndScissor(float ox, float oy, float oz, const SC4RectFloat32& rect) {
    cSC4PathInfo* result = new cSC4PathInfo();
    result->AddRef();
    result->SetUseElevationFlag(UseElevation());

    auto outcode = [&](float x, float z) {
        std::uint32_t code = (x < rect.x0) ? 1u : 0u;
        if (!(rect.z0 <= z)) code += 2;
        if (!(x <= rect.x1)) code |= 4;
        if (!(z <= rect.z1)) code |= 8;
        return code;
    };

    for (const auto& [key, source] : paths_) {
        if (source.points.empty()) continue;
        const std::uint32_t type = (key >> 24) & 0xf;
        const std::uint32_t lane = (key >> 16) & 0xff;
        std::vector<cS3DVector3> current;
        cS3DVector3 prev;
        bool hasPrev = false;
        std::uint32_t prevCode = 0;
        std::uint8_t entryEdge = 0, exitEdge = 0;   // indeterminate in the original until assigned
        std::uint32_t exitByte = type;   // starts as the type nibble
        std::uint32_t code = 0;

        auto emit = [&](std::uint32_t entry, std::uint32_t exit) {
            const std::uint32_t newKey = result->GetUniquePathKey(type, std::uint8_t(entry), std::uint8_t(exit), int(lane));
            tPath* dest = result->NewPath(newKey);
            if (dest) {
                dest->points.swap(current);
                dest->flags = source.flags;
                ComputePathLength(*dest);
            }
            current.clear();
        };

        for (const cS3DVector3& original : source.points) {
            cS3DVector3 cur;
            cur.x = original.x + ox;
            cur.y = original.y + oy;
            cur.z = original.z + oz;
            code = outcode(cur.x, cur.z);
            if (code == 0) {
                if (hasPrev) {
                    if (prevCode != 0) {
                        cS3DVector3 hit = ComputeLineToRectIntersection(rect, prevCode, prev, cur, entryEdge);
                        current.push_back(hit);
                    }
                } else {
                    entryEdge = GetEdgeFromPoint(rect, cur);
                }
                if (current.empty() || !SamePoint(current.back(), cur)) current.push_back(cur);
            } else if (hasPrev) {
                const std::uint32_t diff = code ^ prevCode;
                if (prevCode == 0) {
                    // leaving the rectangle: the crossing point replaces trailing duplicates, then the run is emitted
                    cS3DVector3 hit = ComputeLineToRectIntersection(rect, diff, cur, prev, exitEdge);
                    while (current.size() >= 2 && SamePoint(current.back(), hit)) current.pop_back();
                    current.push_back(hit);
                    exitByte = exitEdge;
                    emit(entryEdge, exitEdge);
                } else if ((code & prevCode) == 0) {
                    // both ends outside: clip the segment against the rectangle sides that differ
                    const float dx = cur.x - prev.x;
                    float enter;          // fVar19: parameter where the segment enters the rectangle
                    float leave = 1.0f;   // fVar20: parameter where it leaves
                    if ((diff & 1) == 0) {
                        leave = 1.0f;
                        enter = 0.0f;
                    } else {
                        enter = (rect.x0 - prev.x) / dx;
                        if ((prevCode & 1) == 0) {
                            if (1.0f < enter) { leave = 1.0f; enter = 0.0f; }
                            else { exitEdge = 0; exitByte = 0; leave = enter; enter = 0.0f; }
                        } else {
                            if (enter < 0.0f) { leave = 1.0f; enter = 0.0f; }
                            else entryEdge = 0;
                        }
                    }
                    const float dz = cur.z - prev.z;
                    if (diff & 2) {
                        const float u = (rect.z0 - prev.z) / dz;
                        if ((prevCode & 2) == 0) { if (u <= leave) { exitEdge = 1; exitByte = 1; leave = u; } }
                        else if (enter <= u) { entryEdge = 1; enter = u; }
                    }
                    if (diff & 4) {
                        const float u = (rect.x1 - prev.x) / dx;
                        if ((prevCode & 4) == 0) { if (u <= leave) { exitEdge = 2; exitByte = 2; leave = u; } }
                        else if (enter <= u) { entryEdge = 2; enter = u; }
                    }
                    if (diff & 8) {
                        const float u = (rect.z1 - prev.z) / dz;
                        if ((prevCode & 8) == 0) { if (u <= leave) { exitEdge = 3; exitByte = 3; leave = u; } }
                        else if (enter <= u) { entryEdge = 3; enter = u; }
                    }
                    if (enter < leave) {
                        const std::uint32_t newKey = result->GetUniquePathKey(type, entryEdge, std::uint8_t(exitByte), int(lane));
                        tPath* dest = result->NewPath(newKey);
                        if (dest) {
                            const float dy = cur.y - prev.y;
                            cS3DVector3 a, b;
                            a.x = dx * enter + prev.x; a.y = dy * enter + prev.y; a.z = enter * dz + prev.z;
                            dest->points.push_back(a);
                            b.x = prev.x + dx * leave; b.y = prev.y + dy * leave; b.z = prev.z + dz * leave;
                            dest->points.push_back(b);
                            dest->flags = source.flags;
                            ComputePathLength(*dest);
                        }
                    }
                }
            }
            prev = cur;
            prevCode = code;
            hasPrev = true;
        }
        if (code == 0) {
            exitEdge = GetEdgeFromPoint(rect, prev);
            emit(entryEdge, exitEdge);
        }
    }
    result->RemoveDuplicatePaths();
    return result;
}

}  // namespace sc4

// cSTESurfaceWater — see sc4/SurfaceWater.h.
#include "sc4/SurfaceWater.h"

#include <cmath>

#include "platform/Unwired.h"
#include "sc4/AutoHelpers.h"
#include "sc4/CommonIIDs.h"
#include "sc4/DBSegment.h"

namespace sc4 {

namespace {
// The directions DoMessage looks at around a built network tile: bit i of the tile's connection mask names the cell
// (x + kDeltaX[i], z + kDeltaZ[i]) (the original's UpdateNeighborConnections()::outZ and DoMessage::deltaZ tables, 0x100a29b70 / 0x100a29ac0).
constexpr int kDeltaX[4] = {-1, 0, 1, 0};
constexpr int kDeltaZ[4] = {0, -1, 0, 1};
constexpr std::uint32_t kNetworkOccupantFlagBridge = 0x40000;
constexpr std::uint32_t kNetworkLookupMask = 0x1f4f;
}  // namespace

cSTESurfaceWater::~cSTESurfaceWater() {
    if (bodies_) { bodies_->Release(); bodies_ = nullptr; }
}

bool cSTESurfaceWater::QueryInterface(std::uint32_t iid, void** out) {
    if (!out) return false;
    if (iid == kGZIID_cIGZMessageTarget2) *out = static_cast<cIGZMessageTarget2*>(this);
    else if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cISTESurfaceWater) *out = static_cast<cIGZSerializable*>(this);
    // (port only) the original answers neither this nor any other serializable id; the port's component factory asks for it first.
    else if (iid == kGZIID_cIGZSerializable) *out = static_cast<cIGZSerializable*>(this);
    else return false;
    AddRef();
    return true;
}

// 0x10072e264: the last reference shuts the object down and deletes it.
std::uint32_t cSTESurfaceWater::Release() {
    if (refCount_ > 1) return --refCount_;
    Shutdown();
    delete this;
    return 0;
}

void cSTESurfaceWater::SyncTerrain() {
    if (!terrain_) return;
    std::vector<float>& depth = terrain_->WaterDepth();
    std::vector<float>& sediment = terrain_->Sediment();
    if (depth.size() != info_.size()) depth.assign(info_.size(), 0.0f);
    if (sediment.size() != info_.size()) sediment.assign(info_.size(), 0.0f);
    for (std::size_t i = 0; i < info_.size(); ++i) {
        depth[i] = info_[i].depth > 0.0f ? info_[i].depth : 0.0f;     // the terrain model keeps land at zero
        sediment[i] = info_[i].sediment;
    }
}

// 0x10072e312.
bool cSTESurfaceWater::Init() {
    if (initialized_) return true;
    terrain_ = host_ ? host_->Terrain() : nullptr;
    if (!terrain_) return false;
    const std::uint32_t vertexCount = terrain_->VertexCount();
    info_.resize(vertexCount);
    scratchFloats_.resize(vertexCount, 0.0f);
    if (flaggedCells_.empty()) flaggedCells_.assign(static_cast<std::size_t>(terrain_->VertexCountX()) * terrain_->VertexCountZ(), false);
    perVertex_ = false;
    channel_ = 0;
    if (!bodies_) {
        bodies_ = new cSC4SimGrid<std::uint16_t>();
        bodies_->AddRef();
        bodies_->Init();
        bodies_->SetTractSize(1);
        bodies_->SetInstanceID(kBodiesOfWaterGridInstance);
    }
    host_->AddNotifications(*this);
    host_->RegisterTicks(*this);
    ResetWaterToSeaLevel(TerrainRect{0, 0, static_cast<std::int32_t>(terrain_->VertexCountX()) - 1, static_cast<std::int32_t>(terrain_->VertexCountZ()) - 1});
    SyncTerrain();
    initialized_ = true;
    return true;
}

// 0x10072e6d8.
bool cSTESurfaceWater::Shutdown() {
    if (!initialized_) return true;
    initialized_ = false;
    if (host_) {
        host_->UnregisterTicks(*this);
        host_->RemoveNotifications(*this);
    }
    info_.clear();
    terrain_ = nullptr;
    if (bodies_) {
        bodies_->Shutdown();
        bodies_->Release();
        bodies_ = nullptr;
    }
    flaggedCells_.clear();
    return true;
}

// 0x10072e648: depth = sea level - altitude (negative on land), the flow channels and the sediment cleared.
void cSTESurfaceWater::ResetWaterToSeaLevel(const TerrainRect& rect) {
    if (!terrain_) return;
    const std::int32_t width = static_cast<std::int32_t>(terrain_->VertexCountX());
    for (std::int32_t z = rect.top; z <= rect.bottom; ++z) {
        for (std::int32_t x = rect.left; x <= rect.right; ++x) {
            const std::size_t index = static_cast<std::size_t>(width * z + x);
            tWaterInfo& info = info_[index];
            info.depth = terrain_->GetSeaLevel() - terrain_->Vertices()[index].altitude;
            info.flow[0] = 0.0f;
            info.flow[1] = 0.0f;
            info.flow[2] = 0.0f;
            info.sediment = 0.0f;
        }
    }
}

// 0x10072f6dc.
float cSTESurfaceWater::GetWaterDepthAtVertex(int vertex) const { return info_[static_cast<std::size_t>(vertex)].depth; }

// 0x10072f6f2: the shallowest of the four corners of the cell (min with the original's compare order), never below zero.
float cSTESurfaceWater::GetWaterDepthForCell(int x, int z) const {
    const std::int32_t width = static_cast<std::int32_t>(terrain_->VertexCountX());
    const std::size_t index = static_cast<std::size_t>(terrain_->GetVertIndex(x, z));
    const float a = info_[index].depth, b = info_[index + 1].depth;
    const float lower = (a <= b || std::isnan(a) || std::isnan(b)) ? a : b;                 // ucomiss + cmovbe
    const float corner = info_[index + static_cast<std::size_t>(width) + 1].depth;
    const float second = corner < lower ? corner : lower;                                    // minss
    const float below = info_[index + static_cast<std::size_t>(width)].depth;
    const float minimum = below < second ? below : second;                                  // minss
    return minimum > 0.0f ? minimum : 0.0f;                                                  // maxss with zero
}

// 0x10072f76e.
bool cSTESurfaceWater::HasDraftClearance(int x, int z) const { return GetWaterDepthForCell(x, z) > kDraftClearanceDepth; }

// 0x10072f784.
bool cSTESurfaceWater::IsNavigable(const cS3DVector3& position, int radius, const void* lot) const {
    int cellX = 0, cellZ = 0;
    host_->PositionToCell(position.x, position.z, cellX, cellZ);
    return IsNavigable(cellX, cellZ, radius, lot);
}

// 0x10072f7ec: every cell of the spiral around (x, z) must be inside the city and deep enough; with a lot given no other lot may
// stand there; a bridge on the cell decides alone (it must clear the water by 20); a network tile of the occupant group on the
// cell blocks it.
bool cSTESurfaceWater::IsNavigable(int x, int z, int radius, const void* lot) const {
    SpiralIterator spiral;
    spiral.x = x; spiral.z = z;
    spiral.targetX = x + 1; spiral.targetZ = z;
    spiral.minX = x - radius; spiral.minZ = z - radius;
    spiral.maxX = radius + x; spiral.maxZ = radius + z;
    if (spiral.minX > spiral.maxX) spiral.maxX = spiral.minX;
    if (spiral.maxZ < spiral.minZ) spiral.maxZ = spiral.minZ;
    spiral.mode = 1; spiral.direction = 1; spiral.state = 0; spiral.onZ = 1; spiral.edgesHit = 0; spiral.turns = 0;
    int cellX = 0, cellZ = 0;
    if (!spiral.GetNext(cellX, cellZ)) return true;
    do {
        if (!host_->CellInCity(cellX, cellZ)) return false;
        if (!HasDraftClearance(cellX, cellZ)) return false;
        if (lot != nullptr) {
            const void* other = host_->LotAtCell(cellX, cellZ);
            if (other != nullptr && other != lot) return false;
        }
        IWaterNetworkOccupant* network = host_->NetworkOccupantAt(cellX, cellZ, kNetworkLookupMask, true);
        if (network != nullptr && network->HasNetworkFlag(kNetworkOccupantFlagBridge)) {
            float height = 0.0f;
            if (!network->BridgeHeight(height)) return false;
            const std::size_t index = static_cast<std::size_t>(terrain_->GetVertIndex(cellX, cellZ));
            return kBridgeClearance <= height - (terrain_->Vertices()[index].altitude + info_[index].depth);
        }
        std::vector<IWaterCellOccupant*> occupants;
        host_->OccupantsInCell(cellX, cellZ, kWaterOccupantGroup, occupants);
        bool blocked = false;
        for (IWaterCellOccupant* occupant : occupants) {
            int ox = 0, oz = 0;
            occupant->GetCell(ox, oz);
            if (ox == cellX && oz == cellZ) blocked = true;
        }
        if (blocked) return false;
    } while (spiral.GetNext(cellX, cellZ));
    return true;
}

// 0x100730b40: the highest water surface among the vertex and its water-covered neighbours, never below the sea level.
float cSTESurfaceWater::AltitudeAtWhichFloodingWillOccur(int x, int z) const {
    const std::int32_t vertex = terrain_->GetVertIndex(x, z);
    float level = terrain_->GetSeaLevel();
    const float depth = info_[static_cast<std::size_t>(vertex)].depth;
    float surface = terrain_->Vertices()[static_cast<std::size_t>(vertex)].altitude + depth;
    if (surface <= level) surface = level;
    level = depth > 0.0f ? surface : level;
    int vx = 0, vz = 0;
    terrain_->GetVertex(vertex, vx, vz);
    const std::uint8_t neighbors = terrain_->GetNeighbors(vx, vz);
    std::uint8_t bit = 1;
    for (int i = 0; i < 8; ++i, bit = static_cast<std::uint8_t>(bit * 2)) {
        if ((bit & neighbors) == 0) continue;
        const std::size_t other = static_cast<std::size_t>(terrain_->NeighborOffset(i) + vertex);
        const float neighborDepth = info_[other].depth;
        float neighborSurface = neighborDepth + terrain_->Vertices()[other].altitude;
        if (neighborSurface <= level) neighborSurface = level;
        level = neighborDepth > 0.0f ? neighborSurface : level;
    }
    return level;
}

// 0x100730c48.
const cS3DVector3& cSTESurfaceWater::GetWaterVelocity(int x, int z) const {
    return info_[static_cast<std::size_t>(terrain_->GetVertIndex(x, z))].velocity;
}

// 0x100730c72: 0 dry, 3 water at or below the sea level, 2 water above it (lakes).
int cSTESurfaceWater::GetWaterType(int x, int z) const {
    const std::size_t index = static_cast<std::size_t>(terrain_->GetVertIndex(x, z));
    const float depth = info_[index].depth;
    if (!(0.0f < depth)) return 0;
    return 3 - (terrain_->GetSeaLevel() < depth + terrain_->Vertices()[index].altitude ? 1 : 0);
}

// 0x100730cd0: water that has no flow in one of the three velocity components.
bool cSTESurfaceWater::IsFlatWater(int vertex) const {
    const tWaterInfo& info = info_[static_cast<std::size_t>(vertex)];
    if (!(0.0f < info.depth)) return false;
    return info.velocity.x == 0.0f || info.velocity.y == 0.0f || info.velocity.z == 0.0f;
}

// 0x100730d26: the connected cells that are navigable and not yet numbered take the id (breadth first, left, right, up, down).
void cSTESurfaceWater::FloodFillBodyOfWater(int x, int z, std::uint16_t id) {
    std::list<std::pair<int, int>> queue;
    queue.push_back({x, z});
    const int width = bodies_->GetTractCountX() - 1, height = bodies_->GetTractCountZ() - 1;
    for (auto it = queue.begin(); it != queue.end(); ++it) {
        const int cx = it->first, cz = it->second;
        if (bodies_->GetTractValue(cx, cz) != 0) continue;
        if (!IsNavigable(cx, cz, 0, nullptr)) continue;
        bodies_->SetTractValue(cx, cz, id);
        if (cx > 0) queue.push_back({cx - 1, cz});
        if (cx < width) queue.push_back({cx + 1, cz});
        if (cz > 0) queue.push_back({cx, cz - 1});
        if (cz < height) queue.push_back({cx, cz + 1});
    }
}

// 0x10072f50a: clear the map, then number every navigable body from 1.
void cSTESurfaceWater::UpdateBodiesOfWater() {
    const int width = bodies_->GetTractCountX(), height = bodies_->GetTractCountZ();
    for (int x = 0; x < width; ++x)
        for (int z = 0; z < height; ++z) bodies_->SetTractValue(x, z, 0);
    std::uint16_t next = 0;
    for (int x = 0; x < width; ++x) {
        for (int z = 0; z < height; ++z) {
            if (bodies_->GetTractValue(x, z) == 0 && IsNavigable(x, z, 0, nullptr)) {
                ++next;
                FloodFillBodyOfWater(x, z, next);
            }
        }
    }
}

void cSTESurfaceWater::UpdateNeighborConnections() { SC4_UNWIRED("cSTESurfaceWater::UpdateNeighborConnections (0x10072efca) not recovered"); }
void cSTESurfaceWater::UpdateFloodWaters() { SC4_UNWIRED("cSTESurfaceWater::UpdateFloodWaters (0x10072fb5c) not recovered"); }

// 0x10072f5f0: one flood step, then the terrain redisplays the whole grid (the terrain model redisplays through its own cell callback).
void cSTESurfaceWater::AnimateWater() {
    UpdateFloodWaters();
    SyncTerrain();
}

// 0x10072f696: a finished scrub rebuilds the connections and the bodies once the city is up.
void cSTESurfaceWater::EndBrushScrub(std::uint32_t brushType, std::uint32_t arg) {
    (void)brushType; (void)arg;
    if (host_ && host_->CityInitialized()) {
        UpdateNeighborConnections();
        UpdateBodiesOfWater();
    }
}

// 0x10072ec5a.
bool cSTESurfaceWater::DoMessage(cIGZMessage2* base) {
    auto* message = static_cast<cIGZMessage2Standard*>(base);
    const std::uint32_t type = message->GetType();
    if (type == kWaterMsgRebuildBodies) {
        if (bodiesDirty_) {
            UpdateBodiesOfWater();
            bodiesDirty_ = false;
        }
    } else if (type == kWaterMsgCityCell4 || (type == kWaterMsgCityCell1 && host_->CityInitialized())) {
        UpdateNeighborConnections();
        UpdateBodiesOfWater();
    } else if (type == kWaterMsgOccupantAdded || type == kWaterMsgOccupantMoved) {
        if (message->GetData3() == kWaterOccupantGroup) {
            auto* object = static_cast<cIGZUnknown*>(message->GetVoid1());
            IWaterCellOccupant* occupant = object ? host_->AsCellOccupant(object) : nullptr;
            if (occupant && !occupant->IsRemoved()) {
                int cellX = 0, cellZ = 0;
                occupant->GetCell(cellX, cellZ);
                if (0.0f < GetWaterDepthForCell(cellX, cellZ)) bodiesDirty_ = true;
            }
        }
    } else if (type == kWaterMsgAnimate) {
        if (perVertex_) AnimateWater();
    } else if (type == kWaterMsgNetworkBuilt) {
        IWaterNetworkOccupant* built = host_->AsNetworkOccupant(message->GetIGZUnknown());
        if (message->GetData1() != 0 && built != nullptr && built->HasNetworkFlag(kNetworkOccupantFlagBridge)) {
            float position[3] = {0, 0, 0};
            built->GetPosition(position);
            int cellX = 0, cellZ = 0;
            host_->PositionToCell(position[0], position[2], cellX, cellZ);
            const std::uint8_t mask = built->ConnectionMask();
            bool joined = false;
            for (int i = 0; i < 4; ++i) {
                if (((mask >> i) & 1) == 0) continue;
                const int nx = kDeltaX[i] + cellX, nz = kDeltaZ[i] + cellZ;
                if ((nx | nz) < 0 || nx >= host_->CitySizeX() - 1 || nz >= host_->CitySizeZ() - 1) continue;
                IWaterNetworkOccupant* other = host_->NetworkOccupantAt(nx, nz, kNetworkLookupMask, true);
                if (other != nullptr && other->HasNetworkFlag(kNetworkOccupantFlagBridge) && !other->IsRemoved()) joined = true;
            }
            if (!joined) UpdateBodiesOfWater();
        }
    }
    return true;
}

// 0x10072e91c: version 1 record of the water: per vertex the depth, the three flow channels, the velocity and the sediment.
bool cSTESurfaceWater::SaveWaterInfo(cIGZPersistDBSegment* segment) const {
    cSC4AutoSaveStream out(segment, kWaterInfoType, kWaterInfoGroup, kWaterInfoInstance, 1);
    if (!out.IsOpen()) return false;
    WriteWaterRecord(*out.GetStream());
    return true;
}

void cSTESurfaceWater::WriteWaterRecord(cIGZOStream& stream) const {
    const std::uint32_t count = terrain_->VertexCount();
    stream.SetUint32(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const tWaterInfo& info = info_[i];
        stream.SetFloat32(info.depth);
        stream.SetFloat32(info.flow[0]);
        stream.SetFloat32(info.flow[1]);
        stream.SetFloat32(info.flow[2]);
        stream << info.velocity;
        stream.SetFloat32(info.sediment);
    }
}

// 0x10072eb28.
bool cSTESurfaceWater::LoadWaterInfo(cIGZPersistDBSegment* segment) {
    cSC4AutoLoadStream in(segment, kWaterInfoType, kWaterInfoGroup, kWaterInfoInstance);
    if (!in.IsOpen() || in.GetVersion() > 1) return false;
    ReadWaterRecord(*in.GetStream());
    return true;
}

void cSTESurfaceWater::ReadWaterRecord(cIGZIStream& stream) {
    std::uint32_t count = 0;
    stream.GetUint32(count);
    info_.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        tWaterInfo& info = info_[i];
        stream.GetFloat32(info.depth);
        stream.GetFloat32(info.flow[0]);
        stream.GetFloat32(info.flow[1]);
        stream.GetFloat32(info.flow[2]);
        stream >> info.velocity;
        stream.GetFloat32(info.sediment);
    }
    SyncTerrain();
}

namespace {
cIGZPersistDBSegment* SegmentOfStream(cIGZOStream& stream) {
    void* raw = nullptr;
    if (!stream.QueryInterface(kGZIID_cISC4DBSegmentOStream, &raw) || !raw) return nullptr;
    auto* segmentStream = static_cast<cISC4DBSegmentOStream*>(raw);
    cISC4DBSegment* segment = segmentStream->GetSegment();
    cIGZPersistDBSegment* result = segment ? segment->AsIGZPersistDBSegment() : nullptr;
    segmentStream->Release();
    return result;
}
cIGZPersistDBSegment* SegmentOfStream(cIGZIStream& stream) {
    void* raw = nullptr;
    if (!stream.QueryInterface(kGZIID_cISC4DBSegmentIStream, &raw) || !raw) return nullptr;
    auto* segmentStream = static_cast<cISC4DBSegmentIStream*>(raw);
    cISC4DBSegment* segment = segmentStream->GetSegment();
    cIGZPersistDBSegment* result = segment ? segment->AsIGZPersistDBSegment() : nullptr;
    segmentStream->Release();
    return result;
}
}  // namespace

// 0x10072e86a: version 1, the terrain, then the water record in the stream's segment.
bool cSTESurfaceWater::Write(cIGZOStream& stream) const {
    stream.SetUint16(1);
    SC4WriteGZSerializable(stream, terrain_);
    return SaveWaterInfo(SegmentOfStream(stream));
}

// 0x10072ea64: versions 0 and 1.
bool cSTESurfaceWater::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version > 1) return false;
    void* raw = nullptr;
    SC4ReadGZSerializable(stream, 0xc70bd37e, &raw, false);
    terrain_ = static_cast<cSTETerrain*>(raw);
    return LoadWaterInfo(SegmentOfStream(stream));
}

}  // namespace sc4

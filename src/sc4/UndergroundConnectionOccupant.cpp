#include "sc4/UndergroundConnectionOccupant.h"

#include "sc4/DBSegment.h"

namespace sc4 {

namespace {

void WriteVertices(cIGZOStream& stream, const std::vector<UndergroundVertex>& vertices) {
    stream.SetUint32(static_cast<std::uint32_t>(vertices.size()));
    for (const auto& vertex : vertices) {
        stream.SetFloat32(vertex.x);
        stream.SetFloat32(vertex.y);
        stream.SetFloat32(vertex.z);
        stream.SetFloat32(vertex.u);
        stream.SetFloat32(vertex.v);
        for (std::uint8_t channel : vertex.color) stream.SetUint8(channel);
    }
}

void ReadVertices(cIGZIStream& stream, std::vector<UndergroundVertex>& vertices) {
    std::uint32_t count = 0;
    stream.GetUint32(count);
    vertices.assign(count, UndergroundVertex{});
    for (auto& vertex : vertices) {
        stream.GetFloat32(vertex.x);
        stream.GetFloat32(vertex.y);
        stream.GetFloat32(vertex.z);
        stream.GetFloat32(vertex.u);
        stream.GetFloat32(vertex.v);
        for (std::uint8_t& channel : vertex.color) stream.GetUint8(channel);
    }
}

void ReleaseAndClear(cIGZUnknown*& held) {
    if (held) {
        cIGZUnknown* object = held;
        held = nullptr;
        object->Release();
    }
}

}  // namespace

cSC4UndergroundConnectionOccupant::cSC4UndergroundConnectionOccupant() { SetProperties(3); }

cSC4UndergroundConnectionOccupant::~cSC4UndergroundConnectionOccupant() {
    ReleaseAndClear(building_);
    ReleaseAndClear(network_);
}

bool cSC4UndergroundConnectionOccupant::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISC4PreBuiltUndergroundView || iid == kGZCLSID_cSC4UndergroundConnectionOccupant) {
        *out = static_cast<cSC4UndergroundConnectionOccupant*>(this);
        AddRef();
        return true;
    }
    if (cSC4Occupant::QueryInterface(iid, out)) return true;
    return s3d::cS3DModelInstanceBasic::QueryInterface(iid, out);
}

bool cSC4UndergroundConnectionOccupant::Init() {
    if (!IsInitialized()) {
        cSC4Occupant::Init();
        if (cIGZMessageServer2* server = DBSegmentMessageServer()) {
            for (std::uint32_t message : kUndergroundConnectionViewMessages)
                server->AddNotification(static_cast<cIGZMessageTarget2*>(this), message);
            subscribed_ = true;
        }
    }
    return true;
}

bool cSC4UndergroundConnectionOccupant::Init(std::uint32_t gziid, tCustomGeometryInfo* info) {
    if (IsInitialized()) return true;
    cSC4Occupant::Init();
    gziid_ = gziid;
    geometryInfo_ = info;
    if (!flag2fc_) ReleaseAndClear(building_), ReleaseAndClear(network_);
    return true;
}

bool cSC4UndergroundConnectionOccupant::Shutdown() {
    for (auto& wall : walls_) wall.clear();
    trench_.clear();
    highlight_.clear();
    piece_.clear();
    ReleaseAndClear(building_);
    ReleaseAndClear(network_);
    if (subscribed_) {
        if (cIGZMessageServer2* server = DBSegmentMessageServer())
            for (std::uint32_t message : kUndergroundConnectionViewMessages)
                server->RemoveNotification(static_cast<cIGZMessageTarget2*>(this), message);
        subscribed_ = false;
    }
    cSC4Occupant::Shutdown();
    return true;
}

void cSC4UndergroundConnectionOccupant::GetPosition(float* xyz) {
    xyz[0] = position_[0];
    xyz[1] = position_[1];
    xyz[2] = position_[2];
}

void cSC4UndergroundConnectionOccupant::GetBoundingBox(float* minXyz, float* maxXyz) {
    // PARTIAL: the original seeds the box from the position and widens it by the four corners (0x1002f1154)
    GetPosition(minXyz);
    GetPosition(maxXyz);
    for (int i = 0; i < 4; ++i) {
        if (cornerHeights_[i] > maxXyz[1]) maxXyz[1] = cornerHeights_[i];
    }
}

void cSC4UndergroundConnectionOccupant::GetWorldBounds(cS3DBoundingBox& out) { GetBoundingBox(out.min, out.max); }

bool cSC4UndergroundConnectionOccupant::DoMessage(cIGZMessage2*) { return true; }

bool cSC4UndergroundConnectionOccupant::Write(cIGZOStream& stream) const {
    stream.SetUint16(kUndergroundConnectionRecordVersion);
    if (cSC4Occupant::Write(stream)) {
        stream.SetUint32(wallCount_);
        for (const auto& wall : walls_) WriteVertices(stream, wall);
        WriteVertices(stream, trench_);
        WriteVertices(stream, highlight_);
        WriteVertices(stream, piece_);
        stream.SetSint32(cellX_);
        stream.SetSint32(cellZ_);
        for (float height : cornerHeights_) stream.SetFloat32(height);
        stream.SetFloat32(modelY_);
        stream.SetSint32(value228_);
        stream.SetUint8(flag22c_ ? 1 : 0);
        SC4WriteGZSerializable(stream, building_);
        SC4WriteGZSerializable(stream, network_);
        stream.SetUint32(gziid_);
        for (float value : position_) stream.SetFloat32(value);
    }
    return !stream.GetError();
}

bool cSC4UndergroundConnectionOccupant::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version != kUndergroundConnectionRecordVersion) return false;
    if (cSC4Occupant::Read(stream)) {
        stream.GetUint32(wallCount_);
        for (auto& wall : walls_) ReadVertices(stream, wall);
        ReadVertices(stream, trench_);
        ReadVertices(stream, highlight_);
        ReadVertices(stream, piece_);
        stream.GetSint32(cellX_);
        stream.GetSint32(cellZ_);
        for (float& height : cornerHeights_) stream.GetFloat32(height);
        stream.GetFloat32(modelY_);
        stream.GetSint32(value228_);
        std::uint8_t flag = 0;
        stream.GetUint8(flag);
        (void)flag;
        void* building = nullptr;
        SC4ReadGZSerializable(stream, kUndergroundConnectionOccupantIID, &building, false);
        building_ = static_cast<cIGZUnknown*>(building);
        void* network = nullptr;
        SC4ReadGZSerializable(stream, kUndergroundConnectionOccupantIID, &network, false);
        network_ = static_cast<cIGZUnknown*>(network);
        stream.GetUint32(gziid_);
        for (float& value : position_) stream.GetFloat32(value);
        flag22c_ = true;
        flag2fc_ = true;
    }
    return !stream.GetError();
}

}  // namespace sc4

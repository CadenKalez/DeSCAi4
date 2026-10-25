#include "sc4/PowerPoleOccupant.h"

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

void WriteCurves(cIGZOStream& stream, const std::vector<std::vector<cS3DVector3>>& curves) {
    stream.SetUint32(static_cast<std::uint32_t>(curves.size()));
    for (const auto& curve : curves) {
        stream.SetUint32(static_cast<std::uint32_t>(curve.size()));
        for (const cS3DVector3& point : curve) stream << point;
    }
}

void ReadCurves(cIGZIStream& stream, std::vector<std::vector<cS3DVector3>>& curves) {
    std::uint32_t count = 0;
    stream.GetUint32(count);
    curves.assign(count, {});
    for (auto& curve : curves) {
        std::uint32_t points = 0;
        stream.GetUint32(points);
        curve.assign(points, cS3DVector3{});
        for (cS3DVector3& point : curve) stream >> point;
    }
}

}  // namespace

cSC4PowerPoleOccupant::cSC4PowerPoleOccupant() {
    SetProperties(3);
    type_ = 0x2890d4de;   // the constructor stamps occupant type 0x2890d4de at +0x58
    viewCellX_ = viewCellZ_ = -1;
    cellX_ = cellZ_ = 0;
}

cSC4PowerPoleOccupant::~cSC4PowerPoleOccupant() {
    for (tConnection& connection : connections_) {
        if (connection.pole) connection.pole->Release();
        for (cSC4PowerLineOccupant* line : connection.lines) if (line) line->Release();
    }
    if (previewPole_) previewPole_->Release();
    if (field190_) field190_->Release();
}

bool cSC4PowerPoleOccupant::QueryInterface(std::uint32_t iid, void** out) {
    switch (iid) {
        case kGZCLSID_cSC4PowerPoleOccupantRecord:
        case kGZCLSID_cSC4PowerPoleOccupant:
        case 0xa9764c8c:
            *out = static_cast<cSC4PowerPoleOccupant*>(this);
            AddRef();
            return true;
        case kGZIID_PowerPoleMessageTarget:
            *out = static_cast<cIGZMessageTarget2*>(this);
            AddRef();
            return true;
        case 0x26a68448:
            if (previewPole_) return previewPole_->QueryInterface(iid, out);
            break;
        default:
            break;
    }
    if (cSC4Occupant::QueryInterface(iid, out)) return true;
    return s3d::cS3DModelInstanceBasic::QueryInterface(iid, out);
}

void cSC4PowerPoleOccupant::InitConnectionPoints() { connectionPoints_.fill(cS3DVector3{}); }

bool cSC4PowerPoleOccupant::Init() {
    cSC4Occupant::Init();
    if (!subscribed_) {
        if (cIGZMessageServer2* server = DBSegmentMessageServer()) {
            server->AddNotification(static_cast<cIGZMessageTarget2*>(this), kPowerPoleViewMessage);
            subscribed_ = true;
        }
    }
    if (previewPole_) {
        cIGZUnknown* preview = previewPole_;
        previewPole_ = nullptr;
        preview->Release();
    }
    return true;
}

bool cSC4PowerPoleOccupant::Shutdown() {
    boundsDirty_ = false;
    if (previewPole_) {
        cIGZUnknown* preview = previewPole_;
        previewPole_ = nullptr;
        preview->Release();
    }
    vertices0_.clear();
    vertices1_.clear();
    vertices2_.clear();
    if (subscribed_) {
        if (cIGZMessageServer2* server = DBSegmentMessageServer())
            server->RemoveNotification(static_cast<cIGZMessageTarget2*>(this), kPowerPoleViewMessage);
        subscribed_ = false;
    }
    if (field190_) {
        cIGZUnknown* held = field190_;
        field190_ = nullptr;
        held->Release();
    }
    cSC4Occupant::Shutdown();
    return true;
}

// A cell is 16 m wide; the original reads the city's cell size once in Init (sCellWidthX / Z).
void cSC4PowerPoleOccupant::GetBoundingBox(float* minXyz, float* maxXyz) {
    constexpr float kCellWidth = 16.0f;
    if (boundsDirty_) {
        boundsMinX_ = boundsMaxX_ = static_cast<int>(position_[0] / kCellWidth);
        boundsMinZ_ = boundsMaxZ_ = static_cast<int>(position_[2] / kCellWidth);
        for (const tConnection& connection : connections_) {
            if (connection.curvesA.empty()) continue;
            if (connection.valueA < boundsMinX_) boundsMinX_ = connection.valueA;
            else if (boundsMaxX_ < connection.valueA) boundsMaxX_ = connection.valueA;
            if (connection.valueB < boundsMinZ_) boundsMinZ_ = connection.valueB;
            else if (boundsMaxZ_ < connection.valueB) boundsMaxZ_ = connection.valueB;
        }
        boundsDirty_ = false;
    }
    minXyz[0] = static_cast<float>(boundsMinX_) * kCellWidth;
    minXyz[1] = field188_;
    minXyz[2] = static_cast<float>(boundsMinZ_) * kCellWidth;
    maxXyz[0] = static_cast<float>(boundsMaxX_ + 1) * kCellWidth;
    maxXyz[1] = field188_;   // PARTIAL: the original adds the pole height here
    maxXyz[2] = static_cast<float>(boundsMaxZ_ + 1) * kCellWidth;
}

void cSC4PowerPoleOccupant::GetBounds(cS3DBoundingBox&) {}   // PARTIAL: forwards to the preview pole (0x108) when there is one

void cSC4PowerPoleOccupant::GetWorldBounds(cS3DBoundingBox& out) { GetBoundingBox(out.min, out.max); }

bool cSC4PowerPoleOccupant::DoMessage(cIGZMessage2* message) {
    // PARTIAL: message 0x99ef1142 (an occupant was inserted) runs DoOccupantInserted, which relinks poles; not recovered.
    (void)message;
    return true;
}

void cSC4PowerPoleOccupant::GetPosition(float* xyz) {
    xyz[0] = position_[0];
    xyz[1] = position_[1];
    xyz[2] = position_[2];
}

bool cSC4PowerPoleOccupant::Write(cIGZOStream& stream) const {
    stream.SetUint16(kPowerPoleRecordVersion);
    if (cSC4Occupant::Write(stream)) {
        stream << transform_;
        for (float value : position_) stream.SetFloat32(value);
        stream.SetUint32(static_cast<std::uint32_t>(connections_.size()));
        for (const tConnection& connection : connections_) {
            SC4WriteGZSerializable(stream, connection.pole ? static_cast<const cIGZSerializable*>(connection.pole) : nullptr);
            stream.SetUint32(static_cast<std::uint32_t>(connection.lines.size()));
            for (const cSC4PowerLineOccupant* line : connection.lines)
                SC4WriteGZSerializable(stream, line ? static_cast<const cIGZSerializable*>(line) : nullptr);
            stream.SetUint8(connection.flagA ? 1 : 0);
            stream.SetUint8(connection.flagB ? 1 : 0);
            stream.SetSint32(connection.valueA);
            stream.SetSint32(connection.valueB);
            WriteCurves(stream, connection.curvesA);
            WriteCurves(stream, connection.curvesB);
        }
        stream.SetUint32(style_);
        stream.SetSint32(viewCellX_);
        stream.SetSint32(viewCellZ_);
        stream.SetSint32(cellX_);
        stream.SetSint32(cellZ_);
        for (const cS3DVector3& point : connectionPoints_) stream << point;
        WriteVertices(stream, vertices0_);
        WriteVertices(stream, vertices1_);
        WriteVertices(stream, vertices2_);
        stream.SetFloat32(field188_);
        stream.SetUint8(field18c_ ? 1 : 0);
        const s3d::SC4DrawAttributes attributes = GetDrawAttributes();
        stream.SetUint8(static_cast<std::uint8_t>(attributes.flags & 0xff));
        stream.SetUint8(static_cast<std::uint8_t>((attributes.flags >> 8) & 0xff));
        stream.SetFloat32(attributes.shade[3]);
        stream.SetUint8(preview_ ? 1 : 0);
    }
    return !stream.GetError();
}

bool cSC4PowerPoleOccupant::Read(cIGZIStream& stream) {
    std::uint16_t version = 0;
    stream.GetUint16(version);
    if (version < 5) return false;   // PARTIAL: the legacy transform of versions 3 and 4 is not recovered
    if (cSC4Occupant::Read(stream)) {
        stream >> transform_;
        for (float& value : position_) stream.GetFloat32(value);
        std::uint32_t count = 0;
        stream.GetUint32(count);
        connections_.assign(count, tConnection{});
        for (tConnection& connection : connections_) {
            void* pole = nullptr;
            SC4ReadGZSerializable(stream, kGZCLSID_cSC4PowerPoleOccupantRecord, &pole, false);
            connection.pole = static_cast<cSC4PowerPoleOccupant*>(pole);
            std::uint32_t lines = 0;
            stream.GetUint32(lines);
            connection.lines.assign(lines, nullptr);
            for (cSC4PowerLineOccupant*& line : connection.lines) {
                void* occupant = nullptr;
                SC4ReadGZSerializable(stream, kGZCLSID_cSC4PowerLineOccupant, &occupant, false);
                line = static_cast<cSC4PowerLineOccupant*>(occupant);
            }
            std::uint8_t flag = 0;
            stream.GetUint8(flag);
            connection.flagA = flag != 0;
            stream.GetUint8(flag);
            connection.flagB = flag != 0;
            stream.GetSint32(connection.valueA);
            stream.GetSint32(connection.valueB);
            ReadCurves(stream, connection.curvesA);
            ReadCurves(stream, connection.curvesB);
        }
        stream.GetUint32(style_);
        stream.GetSint32(viewCellX_);
        stream.GetSint32(viewCellZ_);
        stream.GetSint32(cellX_);
        stream.GetSint32(cellZ_);
        InitConnectionPoints();
        for (cS3DVector3& point : connectionPoints_) stream >> point;
        ReadVertices(stream, vertices0_);
        ReadVertices(stream, vertices1_);
        ReadVertices(stream, vertices2_);
        stream.GetFloat32(field188_);
        std::uint8_t flag = 0;
        stream.GetUint8(flag);
        field18c_ = flag != 0;
        s3d::SC4DrawAttributes attributes = GetDrawAttributes();
        std::uint8_t flags0 = 0, flags1 = 0;
        stream.GetUint8(flags0);
        stream.GetUint8(flags1);
        attributes.flags = (attributes.flags & ~0xffffu) | flags0 | (static_cast<std::uint32_t>(flags1) << 8);
        stream.GetFloat32(attributes.shade[3]);
        SetDrawAttributes(attributes);
        stream.GetUint8(flag);
        preview_ = flag != 0;
        if (previewPole_) {
            cIGZUnknown* preview = previewPole_;
            previewPole_ = nullptr;
            preview->Release();
        }
    }
    return !stream.GetError();
}

}  // namespace sc4

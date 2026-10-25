// cSC4PowerPoleOccupant - a power pole standing on the terrain (class id 0x09c05c6a; QueryInterface also answers 0xa8c86142
// and 0xa9764c8c, the message-target view 0x452294aa and the preview's 0x26a68448). A cSC4Occupant plus a cS3DModelInstanceBasic
// whose record is the pole's transform and position, its list of connections to other poles (each with the power-line
// occupants strung along it and two curves of control points), the pole style, its cell and the vertex data of its
// foundation. Recovered from SimCity 4 Deluxe Edition 1.2.1 (0x10031f406..0x100324abc, constructor 0x100320cf6).
//
// Record (version 5; version 4 differs only in the transform's legacy encoding): the occupant, the transform, the position
// (three floats), the connection count and each connection {pole, line count + lines, flag, flag, int, int, curve set A,
// curve set B}, the style, four ints (view cell sizes and the cell), the four connection points, the three vertex vectors,
// a float, a flag, the two draw-attribute flag bytes, the draw shade and a flag.
//
// PARTIAL: the geometry and wire behaviour (AddConnection / UpdateConnection / GetControlPoints / the Draw and Create*
// functions, LoadModel, StaticInit's texture bindings, DoMessage's view handling) are not recovered, and a version-4 record
// (legacy transform) is refused.
#pragma once
#include <array>
#include <list>
#include <vector>

#include "gz/StreamOps.h"
#include "gz/cIGZMessage2.h"
#include "sc4/OccupantManager.h"
#include "s3d/ModelInstance.h"
#include "s3d/S3DMath.h"
#include "sc4/Occupant.h"
#include "sc4/PowerLineOccupant.h"
#include "sc4/UndergroundOccupant.h"

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4PowerPoleOccupantRecord = 0x09c05c6a;   // GetGZCLSID @ 0x100324abc
constexpr std::uint32_t kGZIID_PowerPoleMessageTarget = 0x452294aa;
constexpr std::uint32_t kPowerPoleSortValue = 0xaa9da78e;
constexpr std::uint32_t kPowerPoleViewMessage = 0x99ef1142;
constexpr std::uint16_t kPowerPoleRecordVersion = 5;

class cSC4PowerPoleOccupant : public cSC4Occupant, public s3d::cS3DModelInstanceBasic, public cIGZMessageTarget2 {
public:
    // One wire between this pole and another (0x60 bytes in the original).
    struct tConnection {
        cSC4PowerPoleOccupant* pole = nullptr;                 // +0x00 (a held reference)
        std::list<cSC4PowerLineOccupant*> lines;               // +0x08 the line occupants strung along it (held)
        bool flagA = false;                                    // +0x20
        bool flagB = false;                                    // +0x21
        std::int32_t valueA = 0;                               // +0x24
        std::int32_t valueB = 0;                               // +0x28
        std::vector<std::vector<cS3DVector3>> curvesA;         // +0x30
        std::vector<std::vector<cS3DVector3>> curvesB;         // +0x48
    };

    cSC4PowerPoleOccupant();                                    // 0x100320cf6
    ~cSC4PowerPoleOccupant() override;                          // 0x100320efa

    bool QueryInterface(std::uint32_t iid, void** out) override;   // 0x10032392a
    std::uint32_t AddRef() override { return cSC4Occupant::AddRef(); }     // 0x100323a38
    std::uint32_t Release() override { return cSC4Occupant::Release(); }   // 0x100323a9c

    bool Init() override;                                       // 0x10032261a
    bool Shutdown() override;                                   // 0x1003229fa
    void GetPosition(float* xyz) override;                      // 0x100321ffc
    void GetBoundingBox(float* minXyz, float* maxXyz) override;     // 0x100321b08
    void GetBounds(cS3DBoundingBox& out) override;                  // 0x100322268
    void GetWorldBounds(cS3DBoundingBox& out) override;             // 0x1003222a4
    bool DoMessage(cIGZMessage2* message) override;                 // 0x100323dbe
    bool Write(cIGZOStream& stream) const override;             // 0x100322af6
    bool Read(cIGZIStream& stream) override;                    // 0x10032315c
    std::uint32_t GetGZCLSID() const override { return kGZCLSID_cSC4PowerPoleOccupantRecord; }   // 0x100324abc
    void GetSortValue(std::uint32_t& out) const override { out = kPowerPoleSortValue; }          // 0x100323b48

    void GetCell(int& x, int& z) const { x = cellX_; z = cellZ_; }   // 0x100324012
    int GetNumberOfConnections() const { return static_cast<int>(connections_.size()); }   // 0x100321fa8
    const std::vector<tConnection>& Connections() const { return connections_; }
    std::uint32_t Style() const { return style_; }
    bool IsPreview() const { return preview_; }                 // 0x100324332
    void InitConnectionPoints();                                // 0x100321c76 (PARTIAL: zeroes the four points)

private:
    cS3DTransform transform_;                       // +0xa4
    float position_[3] = {0, 0, 0};                 // +0xdc
    std::vector<tConnection> connections_;          // +0xe8
    std::uint32_t style_ = 0;                       // +0x100
    cIGZUnknown* previewPole_ = nullptr;            // +0x108 (released by Init / Shutdown / Read)
    std::int32_t viewCellX_ = 0, viewCellZ_ = 0;    // +0x110 / +0x114
    std::int32_t cellX_ = -1, cellZ_ = 0;           // +0x118 / +0x11c (the constructor stores -1 across +0x110)
    std::int32_t boundsMinX_ = 0, boundsMaxX_ = 0, boundsMinZ_ = 0, boundsMaxZ_ = 0;   // +0x124 / +0x12c / +0x128 / +0x130 the cells the pole and its wires cover
    bool boundsDirty_ = true;                       // +0x120
    std::array<cS3DVector3, 4> connectionPoints_{}; // +0x138
    std::vector<UndergroundVertex> vertices0_;      // +0x140
    std::vector<UndergroundVertex> vertices1_;      // +0x158
    std::vector<UndergroundVertex> vertices2_;      // +0x170
    float field188_ = 0.0f;                         // +0x188
    bool field18c_ = false;                         // +0x18c
    bool subscribed_ = false;                       // +0x18d
    bool preview_ = false;                          // +0x18e
    cIGZUnknown* field190_ = nullptr;               // +0x190 (released by Shutdown)
};

}  // namespace sc4

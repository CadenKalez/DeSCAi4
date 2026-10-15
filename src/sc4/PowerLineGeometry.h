// Geometry helpers of cSC4PowerPoleOccupant: the sagging-wire Bezier control points between two poles and the
// grid cells a wire crosses (0x10031fb24 GetControlPoints, 0x100322088 GetWireCells, 0x100323e08
// InitPowerLineControlVariables). Single precision with the original operation order; build without FMA contraction.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/StreamOps.h"
#include "sc4/SCPropertyHelp.h"

namespace sc4 {

struct PowerLineTunables {
    float maxHorizontalControlPointDistance = 16.0f;   // sfMaxHorizontalControlPointDistance (0xe9a18d3f)
    float horizontalControlPointPercent = 10.0f;       // sfHorizontalControlPointPercent (0x49a1e8c4)
    float maxVerticalControlPointDistance = 10.0f;     // sfMaxVerticalControlPointDistance (0x89a18fc4)
    float verticalControlPointPercent = 0.15f;         // sfVerticalControlPointPercent (0xe9a1e8d4)
    float curveDistanceBetweenPoints = 8.0f;           // sfCurveDistanceBetweenPoints (0x49a195c0)
    std::uint32_t minBezierPoints = 5;                 // sMinNumBezierPoints (0x89a1e8fb)

    // exemplar {0x6534284a, 0xe7e2c2db, 0xc911e35b}; null keeps the defaults
    void Read(const cISCPropertyHolder* exemplar);
};

// Two inner control points of the cubic curve from `a` to `b` (the wire hangs lower in the middle) and the
// number of segments to tessellate it into.
void GetPowerLineControlPoints(const PowerLineTunables& tunables, const cS3DVector3& a, const cS3DVector3& b,
                               cS3DVector3& control0, cS3DVector3& control1, int& segmentCount);

struct WireCell { int x, z; };
// Cells visited by a straight wire from cell position (startX, startZ) (floats, in cells) to the target cell:
// a digital line walk along the longer axis, one entry per step including both ends.
void GetPowerWireCells(float startX, float startZ, std::uint32_t targetX, std::uint32_t targetZ, std::vector<WireCell>& out);

}  // namespace sc4

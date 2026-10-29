// Plain float arithmetic, written in the machine code's order.
#pragma STDC FP_CONTRACT OFF
#include "sc4/Blimp.h"

#include <cmath>

namespace sc4 {

bool cSC4Blimp::Init() {
    if (!IsInitialized()) {
        cSC4Aircraft::Init();
        float position[3];
        GetPosition(position);
        const cS3DVector3 from{position[0], position[1], position[2]};
        GetNewDirection(from, heading_);
    }
    return true;
}

void cSC4Blimp::GetNewDirection(const cS3DVector3& from, cS3DVector3& out) {
    IAircraftHost* host = aircraftHost_;
    out.x = static_cast<float>(host->RandomDoubleRange(0.0, static_cast<double>(host->CityCellCountX())));
    out.y = from.y;
    out.z = static_cast<float>(host->RandomDoubleRange(0.0, static_cast<double>(host->CityCellCountZ())));
    const float dx = out.x - from.x;
    const float dy = out.y - from.y;
    const float dz = out.z - from.z;
    const float length = std::sqrt((dx * dx + dy * dy) + dz * dz);
    const float inverse = 1.0f / length;
    out.x = dx * inverse;
    out.y = dy * inverse;
    out.z = inverse * dz;
}

}  // namespace sc4

#include "sc4/AutomataTuningParams.h"

namespace sc4 {

cSC4AutomataTuningParams::cSC4AutomataTuningParams() {
    const std::vector<SC4Point> flat = {{0.0f, 0.5f}, {0.0f, 0.5f}};
    curve030.SetResponseCurveData(flat);
    curve058.SetResponseCurveData(flat);
    curve080.SetResponseCurveData(flat);
    curve0a8.SetResponseCurveData(flat);
    curve0d0.SetResponseCurveData(flat);
    curve100.SetResponseCurveData(flat);
}

cSC4AutomataTuningParams::~cSC4AutomataTuningParams() = default;

}  // namespace sc4

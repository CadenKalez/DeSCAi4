// Grid-combining helpers of the simulators: ProcessTwoGridsWithCallback<T1, T2> and its callbacks (AddCurve,
// AddWithFactor, AddDesirability, Divide, AddTripLengthCurve, ...), 11 instantiations at 0x1000c6fb2 ..
// 0x10021c879. A destination grid is combined with a source grid of a possibly different tract size: equal
// sizes pair cell by cell, a finer destination reuses each source tract value for its whole block, a coarser
// destination receives the integer mean of the source block (sum >> shift >> shift).
#pragma once
#include <cstdint>
#include <functional>

#include "sc4/cSC4ResponseCurve.h"
#include "sc4/cSC4SimGrid.h"

namespace sc4 {

template <class T1, class T2>
using GridCallback = void (*)(T1&, T2, void*);

// The value handed to the callback for a coarser destination: float sources are accumulated through float
// additions truncated to int each step, integral sources in plain int.
template <class T2>
inline std::int32_t SimGridAccumulate(std::int32_t sum, T2 value) {
    if constexpr (std::is_floating_point_v<T2>) return static_cast<std::int32_t>(static_cast<float>(sum) + value);
    else return sum + static_cast<std::int32_t>(value);
}

template <class T1, class T2>
void ProcessTwoGridsWithCallback(cSC4SimGrid<T1>& dst, const cSC4SimGrid<T2>& src, GridCallback<T1, T2> callback,
                                 void* context) {
    const int shiftDst = dst.GetTractShift(), shiftSrc = src.GetTractShift();
    const int countDstX = dst.GetTractCountX(), countDstZ = dst.GetTractCountZ();
    const int countSrcX = src.GetTractCountX(), countSrcZ = src.GetTractCountZ();
    auto& dstData = dst.GetGridData();
    const auto& srcData = src.GetGridData();
    const int difference = shiftDst - shiftSrc;
    if (difference == 0) {
        for (int x = 0; x < countDstX; ++x)
            for (int z = 0; z < countDstZ; ++z) callback(dstData[x][z], srcData[x][z], context);
    } else if (shiftDst < shiftSrc) {                                       // destination is finer
        const int d = shiftSrc - shiftDst;
        for (int sx = 0; sx < countSrcX; ++sx) {
            for (int sz = 0; sz < countSrcZ; ++sz) {
                T2 value = srcData[sx][sz];
                if constexpr (std::is_floating_point_v<T2>) value = static_cast<T2>(static_cast<std::int32_t>(value));
                for (int x = sx << d; x < ((sx + 1) << d); ++x)
                    for (int z = sz << d; z < ((sz + 1) << d); ++z) callback(dstData[x][z], value, context);
            }
        }
    } else {                                                                // destination is coarser
        const int d = difference;
        for (int x = 0; x < countDstX; ++x) {
            for (int z = 0; z < countDstZ; ++z) {
                std::int32_t sum = 0;
                for (int sx = x << d; sx < ((x + 1) << d); ++sx)
                    for (int sz = z << d; sz < ((z + 1) << d); ++sz) sum = SimGridAccumulate<T2>(sum, srcData[sx][sz]);
                callback(dstData[x][z], static_cast<T2>((sum >> d) >> d), context);
            }
        }
    }
}

// Variant used by the residential travel code: only cells whose lot has a zone type 1..3 receive values. The
// source grid is at cell resolution (shift 0) or coarser; the destination is addressed by cell.
template <class T2>
void ProcessTwoGridsForROnly(cSC4SimGrid<std::int8_t>& dst, const cSC4SimGrid<T2>& src,
                             GridCallback<std::int8_t, T2> callback, void* context,
                             const std::function<int(int, int)>& zoneTypeOfLotAt) {
    const int shiftSrc = src.GetTractShift();
    const int countDstX = dst.GetTractCountX(), countDstZ = dst.GetTractCountZ();
    const int countSrcX = src.GetTractCountX(), countSrcZ = src.GetTractCountZ();
    auto& dstData = dst.GetGridData();
    const auto& srcData = src.GetGridData();
    auto zoned = [&](int x, int z) {
        const int zone = zoneTypeOfLotAt(x, z);
        return zone >= 1 && zone <= 3;
    };
    if (shiftSrc == 0) {
        for (int x = 0; x < countDstX; ++x)
            for (int z = 0; z < countDstZ; ++z)
                if (zoned(x, z)) callback(dstData[x][z], srcData[x][z], context);
    } else {
        for (int sx = 0; sx < countSrcX; ++sx) {
            for (int sz = 0; sz < countSrcZ; ++sz) {
                T2 value = srcData[sx][sz];
                for (int x = sx << shiftSrc; x < ((sx + 1) << shiftSrc); ++x)
                    for (int z = sz << shiftSrc; z < ((sz + 1) << shiftSrc); ++z)
                        if (zoned(x, z)) callback(dstData[x][z], value, context);
            }
        }
    }
}

// ----------------------------------------------------------------------------------------- callbacks

// context of AddDesirability (0x1000c7340): factor at +0, optional response curve at +8
struct DesirabilityContext { float factor; cSC4ResponseCurve* curve; };

// destination uint8, clamped to 1..255
inline void AddDesirability(std::uint8_t& dst, std::int16_t value, void* context) {
    auto* c = static_cast<DesirabilityContext*>(context);
    float v = static_cast<float>(value);
    if (c->curve) v = c->curve->GetOutputValue(v);
    int result = static_cast<int>(v * c->factor + static_cast<float>(dst));
    if (result <= 0) result = 1;
    if (result > 0xfe) result = 0xff;
    dst = static_cast<std::uint8_t>(result);
}

// destination int16: += curve(value), truncated; the context is the curve
template <class T>
inline void AddCurveToShort(std::int16_t& dst, T value, void* context) {
    const float v = static_cast<cSC4ResponseCurve*>(context)->GetOutputValue(static_cast<float>(value));
    dst = static_cast<std::int16_t>(dst + static_cast<std::int16_t>(static_cast<int>(v)));
}

// destination int8: -128 is "no data"; value goes through the curve when one is given, else added directly;
// the result is clamped to -127..127
template <class T>
inline void AddCurveToSignedChar(std::int8_t& dst, T value, void* context) {
    if (dst == -0x80) return;
    auto* curve = static_cast<cSC4ResponseCurve*>(context);
    int result;
    if constexpr (std::is_floating_point_v<T>) {
        const float add = curve ? curve->GetOutputValue(value) : value;
        result = static_cast<int>(static_cast<float>(dst) + add);
    } else if (!curve) {
        result = static_cast<int>(dst) + static_cast<int>(value);
    } else {
        result = static_cast<int>(static_cast<float>(dst) + curve->GetOutputValue(static_cast<float>(value)));
    }
    if (result < -0x7f) result = -0x7f;
    if (result > 0x7e) result = 0x7f;
    dst = static_cast<std::int8_t>(result);
}

// destination int8: trip length curve, 0 stands for 255; context may be null (no-op)
inline void AddTripLengthCurve(std::int8_t& dst, std::uint8_t value, void* context) {
    if (!context || dst == -0x80) return;
    const float v = static_cast<cSC4ResponseCurve*>(context)->GetOutputValue(value == 0 ? 255.0f : static_cast<float>(value));
    int result = static_cast<int>(static_cast<float>(dst) + v);
    if (result < -0x7f) result = -0x7f;
    if (result > 0x7e) result = 0x7f;
    dst = static_cast<std::int8_t>(result);
}

// context: float factor, applied to positive values only
inline void AssignWithFactor(std::int16_t& dst, std::int16_t value, void* context) {
    float v = static_cast<float>(value);
    if (value > 0) v *= *static_cast<float*>(context);
    dst = static_cast<std::int16_t>(static_cast<int>(v));
}

inline void AddWithFactor(std::int16_t& dst, std::int16_t value, void* context) {
    float v = static_cast<float>(value);
    if (value > 0) v *= *static_cast<float*>(context);
    dst = static_cast<std::int16_t>(static_cast<int>(static_cast<float>(dst) + v));
}

// percentage of the divisor; a zero divisor clears the destination
inline void Divide(std::int16_t& dst, std::uint16_t divisor, void*) {
    dst = divisor == 0 ? std::int16_t{0}
                       : static_cast<std::int16_t>(static_cast<int>((static_cast<float>(dst) * 100.0f) /
                                                                     static_cast<float>(divisor)));
}

}  // namespace sc4

// cSC4ResponseCurve — piecewise-linear response curve ("map this input to that output"). Recovered
// from Sim City 4 Deluxe Editionsub 1.2.1 (ctor @ 0x1000ac10e, SetResponseCurveData @ 0x1000ac220,
// GetOutputValue @ 0x1000ac4f6, GetMaxInputValue @ 0x1000ac578). Used by the audio sound manager
// to attenuate overlapping instances of one sound.
//
// Original layout (0x20 bytes): std::vector<ResponseCurveData> +0 (20-byte entries), count +0x18.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

struct SC4Point {
    float x = 0.0f;
    float y = 0.0f;
};

class cSC4ResponseCurve {
public:
    // One segment: the point itself, the previous point and the slope between them. The first
    // entry's previous x is -FLT_MAX with a zero slope (a flat extension to the left).
    struct ResponseCurveData {
        float x, y;
        float previousX, previousY;
        float slope;
    };

    cSC4ResponseCurve() = default;

    // @ 0x1000ac220. Points must be sorted by x. A point whose slope to its predecessor is not
    // negligible but whose x step is negligible (|dy| * 1e-10 >= dx) is dropped, like the original.
    // Returns whether `points` was non-empty.
    bool SetResponseCurveData(const std::vector<SC4Point>& points);
    // @ 0x1000ac37a body: a property holding interleaved x,y floats (count / 2 points).
    bool SetResponseCurveData(const float* interleavedValues, std::size_t valueCount);
    // @ 0x1000ac1b8
    void GetResponseCurveData(std::vector<SC4Point>& out) const;
    // @ 0x1000ac4f6: upper-bound segment search; inputs left of the first point return the first
    // point's y, inputs at or right of the last point return the last point's y. An empty curve
    // returns 0 (the original dereferences a null array).
    float GetOutputValue(float input) const;
    // @ 0x1000ac578: x of the last point; false when the curve is empty.
    bool GetMaxInputValue(float& out) const;

    std::size_t PointCount() const { return data_.size(); }

private:
    std::vector<ResponseCurveData> data_;
};

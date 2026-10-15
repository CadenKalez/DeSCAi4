#include "gz/ColorConversion.h"

#include <algorithm>
#include <cmath>

#pragma STDC FP_CONTRACT OFF

namespace nGZColor {

namespace {
inline u8 ToByte(float v) { return u8(long(v)); }

// Piecewise hue ramp of the HLS model; `q` is the high value, `p` the low value, `t` the hue offset in degrees.
// The arithmetic after the comparisons runs in double, like the original.
inline float HueRamp(float t, float q, float p) {
    if (60.0 <= t) {
        if (180.0 <= t) {
            if (240.0 <= t) return float(p * 255.0);
            return float((((240.0 - t) * (q - p)) / 60.0 + p) * 255.0);
        }
        return float(q * 255.0);
    }
    return float((((q - p) * t) / 60.0 + p) * 255.0);
}
}  // namespace

void RGBToHSB(u8 r, u8 g, u8 b, u8& hue, u8& saturation, u8& brightness) {
    const u8 max = std::max<u8>(b, std::max(r, g));
    const u8 min = std::min<u8>(b, std::min(r, g));
    brightness = max;
    if (max == 0) {
        saturation = 0;
        hue = 0;
        return;
    }
    saturation = u8(int(((float)int(max - min) * 255.0f) / (float)max));
    if (saturation == 0) {
        hue = 0;
        return;
    }
    const u8 delta = u8(max - min);
    float h;
    if (max == r) h = (float)int(g - b) / (float)delta;
    else if (max == g) h = (float)int(b - r) / (float)delta + 2.0f;
    else h = (float)int(r - g) / (float)delta + 4.0f;
    h = h * 60.0f;
    hue = u8(long((h < 0.0f ? h + 360.0f : h) * 0.7083333f));
}

void RGBToHSB(u8 r, u8 g, u8 b, float& hue, float& saturation, float& brightness) {
    const u8 max = std::max<u8>(b, std::max(r, g));
    const u8 min = std::min<u8>(b, std::min(r, g));
    float v = (float)max;
    brightness = v;
    if (max == 0) {
        saturation = 0.0f;
        hue = 0.0f;
        return;
    }
    const unsigned s = (unsigned)((v - (float)min) / v);
    saturation = (float)(s & 0xff);
    if (u8(s) == 0) {
        hue = 0.0f;
        return;
    }
    const float delta = (float)int(max - min);
    float h;
    if (max == r) h = (float)int(g - b) / delta;
    else if (max == g) h = (float)int(b - r) / delta + 2.0f;
    else h = (float)int(r - g) / delta + 4.0f;     // blue is the maximum here
    h = h * 60.0f;
    hue = h;
    if (!(0.0f <= h)) hue = h + 360.0f;
}

void HSBToRGB(u8 hue, u8 saturation, u8 brightness, u8& r, u8& g, u8& b) {
    if (saturation == 0) {
        r = brightness;
        g = brightness;
        b = brightness;
        return;
    }
    const float s = (float)saturation / 255.0f;
    const float v = (float)brightness / 255.0f;
    const unsigned sector = (unsigned)((float)hue / 42.666668f);
    float f = (float)hue / 42.666668f - (float)int(sector);
    const float p = (1.0f - s) * v;
    const float q = (1.0f - s * f) * v;
    float red = v, green = p, blue = q;
    if (sector < 5) {
        const float t = (1.0f - s * (1.0f - f)) * v;
        green = t;
        blue = p;
        switch (sector) {
            case 1: red = q; green = v; break;
            case 2: red = p; green = v; blue = t; break;
            case 3: red = p; green = q; blue = v; break;
            case 4: red = t; green = p; blue = v; break;
            default: break;
        }
    }
    r = ToByte(red * 255.0f);
    g = ToByte(green * 255.0f);
    b = ToByte(blue * 255.0f);
}

void HSBToRGB(float hue, float saturation, float brightness, u8& r, u8& g, u8& b) {
    if (saturation != 0.0f || std::isnan(saturation)) {
        // x86 converts through a 64-bit signed integer, so a negative hue yields a huge sector number
        const unsigned sector = (unsigned)(long long)(hue / 60.0f);
        const float f = hue / 60.0f - (float)int(sector);
        const float p = (1.0f - saturation) * brightness;
        const float q = (1.0f - f * saturation) * brightness;
        float red = brightness, green = p, blue = q;
        if (sector < 5) {
            const float t = (1.0f - (1.0f - f) * saturation) * brightness;
            green = t;
            blue = p;
            switch (sector) {
                case 1: red = q; green = brightness; break;
                case 2: red = p; green = brightness; blue = t; break;
                case 3: red = p; green = q; blue = brightness; break;
                case 4: red = t; green = p; blue = brightness; break;
                default: break;
            }
        }
        r = ToByte(red * 255.0f);
        g = ToByte(green * 255.0f);
        b = ToByte(blue * 255.0f);
        return;
    }
    const float grey = brightness * 255.0f;
    r = ToByte(grey);
    g = ToByte(grey);
    b = ToByte(grey);
}

void RGBToHLS(u8 r, u8 g, u8 b, float& hue, float& lightness, float& saturation) {
    const u8 max = std::max<u8>(b, std::max(r, g));
    const u8 min = std::min<u8>(b, std::min(r, g));
    const int sum = int(max) + int(min);
    const int delta = int(max) - int(min);
    lightness = (float)sum / 510.0;
    if (delta == 0) {
        saturation = 0.0f;
        hue = 0.0f;
        return;
    }
    float denominator = (float)sum;
    const float d = (float)delta;
    if (128.0 <= lightness) denominator = denominator / -255.0 + 2.0;
    else denominator = denominator * 255.0;
    saturation = d / denominator;
    float h;
    if (max == r) h = (float)int(g - b) / (d * 255.0);
    else if (max == g) h = (float)int(b - r) / (d * 255.0) + 2.0;
    else h = (float)int(r - g) / (d * 255.0) + 4.0;
    h = h * 60.0;
    hue = h;
    if (!(0.0 <= h)) hue = h + 360.0;
}

void RGBToHLS(u8 r, u8 g, u8 b, u8& hue, u8& lightness, u8& saturation) {
    const u8 max = std::max<u8>(b, std::max(r, g));
    const u8 min = std::min<u8>(b, std::min(r, g));
    const unsigned sum = unsigned(max) + unsigned(min);
    int delta = int(max) - int(min);
    lightness = u8(sum >> 1);
    if (delta == 0) {
        saturation = 0;
        hue = 0;
        return;
    }
    const unsigned divisor = sum < 0x100 ? sum : 0x1fe - sum;
    saturation = u8(delta / int(divisor));
    int h;
    if (max == r) h = (int(g) - int(b)) / delta;
    else if (max == g) h = (int(b) - int(r)) / delta + 0x200;
    else h = (int(r) - int(g)) / delta + 0x400;
    hue = u8(h / 6);
}

void HLSToRGB(u8 hue, u8 lightness, u8 saturation, u8& r, u8& g, u8& b) {
    if (saturation == 0) {
        r = g = b = lightness;
        return;
    }
    const float l = (float)lightness / 255.0;
    float s = (float)saturation / 255.0;
    float q;
    if (0.5 <= l) q = (l + s) - s * l;
    else q = (s + 1.0) * l;
    const float p = (l + l) - q;
    const float h = (float)hue * 360.0 * 0.00390625;
    float t = h + 120.0;
    if (360.0 < t) t = t + -360.0;
    r = u8(int(HueRamp(t, q, p)));
    t = h;
    if (360.0 < t) t = t + -360.0;
    g = u8(int(HueRamp(t, q, p)));
    t = h + -120.0;
    if (t <= 360.0) {
        if (t < 0.0) t = t + 360.0;
    } else {
        t = t + -360.0;
    }
    b = u8(int(HueRamp(t, q, p)));
}

void HLSToRGB(float hue, float lightness, float saturation, u8& r, u8& g, u8& b) {
    if (std::fabs(saturation) < 0.001) {
        r = g = b = u8(int(lightness * 255.0));
        return;
    }
    float q;
    if (lightness <= 0.5) q = (saturation + 1.0) * lightness;
    else q = (lightness + saturation) - saturation * lightness;
    const float p = (lightness + lightness) - q;
    float t = hue + 120.0;
    if (t <= 360.0) {
        if (t < 0.0) t = t + 360.0;
    } else {
        t = t + -360.0;
    }
    r = u8(int(HueRamp(t, q, p)));
    if (hue <= 360.0) {
        t = hue;
        if (!(0.0 <= hue)) t = 360.0f + hue;     // negative hue wraps once
    } else {
        t = -360.0f + hue;
    }
    g = u8(int(HueRamp(t, q, p)));
    t = hue + -120.0;
    if (t <= 360.0) {
        if (t < 0.0) t = t + 360.0;
    } else {
        t = t + -360.0;
    }
    b = u8(int(HueRamp(t, q, p)));
}

void RGBToCYM(u8 r, u8 g, u8 b, u8& c, u8& y, u8& m) {
    c = u8(~r);
    y = u8(~g);
    m = u8(~b);
}

void CYMToRGB(u8 c, u8 y, u8 m, u8& r, u8& g, u8& b) {
    r = u8(~c);
    g = u8(~y);
    b = u8(~m);
}

void RGBToCYMK(u8 r, u8 g, u8 b, u8& c, u8& y, u8& m, u8& k) {
    c = u8(~r);
    y = u8(~g);
    m = u8(~b);
    k = std::min<u8>(b, std::min(r, g));
    c = u8(c - k);
    y = u8(y - k);
    m = u8(m - k);
}

void CYMKToRGB(u8 c, u8 y, u8 m, u8 k, u8& r, u8& g, u8& b) {
    r = u8(~(c + k));
    g = u8(~(y + k));
    b = u8(~(m + k));
}

}  // namespace nGZColor

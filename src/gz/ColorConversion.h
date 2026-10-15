// cGZGraphicSystem colour-space conversions (RGB <-> HSB / HLS / CYM / CYMK), recovered from
// 0x1006184aa..0x100618fd8 and 0x10061faf0..0x10061fcde. They do not use the graphic-system state.
// The floating-point ones mix float and double arithmetic exactly as the original machine code does
// (some intermediate sums stay in double), so this file must be compiled without FMA contraction.
//
// Quirks kept on purpose:
//   * RGBToHSB (float outputs) truncates the saturation to 0 or 1 and reports hue 0 whenever it is 0.
//   * RGBToHLS (byte outputs) uses integer division for saturation and hue (S is nearly always 0/1).
#pragma once
#include <cstdint>

namespace nGZColor {

using u8 = std::uint8_t;

// HSB (byte): hue 0..255 around the circle (255 ~ 360 degrees * 0.7083333), saturation / brightness 0..255
void RGBToHSB(u8 r, u8 g, u8 b, u8& hue, u8& saturation, u8& brightness);
void RGBToHSB(u8 r, u8 g, u8 b, float& hue, float& saturation, float& brightness);
void HSBToRGB(u8 hue, u8 saturation, u8 brightness, u8& r, u8& g, u8& b);
void HSBToRGB(float hueDegrees, float saturation, float brightness, u8& r, u8& g, u8& b);

// HLS
void RGBToHLS(u8 r, u8 g, u8 b, float& hueDegrees, float& lightness, float& saturation);
void RGBToHLS(u8 r, u8 g, u8 b, u8& hue, u8& lightness, u8& saturation);
void HLSToRGB(u8 hue, u8 lightness, u8 saturation, u8& r, u8& g, u8& b);
void HLSToRGB(float hueDegrees, float lightness, float saturation, u8& r, u8& g, u8& b);

// CMY / CMYK
void RGBToCYM(u8 r, u8 g, u8 b, u8& c, u8& y, u8& m);
void CYMToRGB(u8 c, u8 y, u8 m, u8& r, u8& g, u8& b);
void RGBToCYMK(u8 r, u8 g, u8 b, u8& c, u8& y, u8& m, u8& k);
void CYMKToRGB(u8 c, u8 y, u8 m, u8 k, u8& r, u8& g, u8& b);

}  // namespace nGZColor

// RZDraw — the framework's 2-D drawing helpers (0x1006a2d10..0x1006a7ac5): nine-slice edge blits, tiled and
// stretched blits (software fallbacks for buffers that cannot draw through a context), raised / outlined
// rectangles. A "nine-slice" source rectangle is the top-left W x H tile of a 3 x 3 grid of tiles.
// Not recovered (left partial): RZDrawDrawCircle (0x1006a3d1f) and RZDrawFloodFill (0x1006a661a).
#pragma once
#include <cstdint>

#include "gz/Buffer.h"
#include "gz/DrawContext.h"

// Copies `sourceRect` tile-by-tile over `destRect`, starting `offsetX/offsetY` into the pattern; the pixel
// blitters are used when the destination cannot draw through a context.
bool RZDrawBltTiled(cIGZBuffer* dest, cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect,
                    int offsetX, int offsetY, const cRZRect* clip);
// Nine-slice blit: corners are copied, edges tiled; `fillCenter` also tiles the middle tile.
bool RZDrawBltEdge(cIGZBuffer* dest, cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect,
                   bool fillCenter);
void RZDrawBltEdge(gz::cIGZDrawContext* context, cIGZBuffer* source, const cRZRect& sourceRect,
                   const cRZRect& destRect, bool fillCenter);
// Nearest-neighbour stretch of sourceRect onto destRect (through the context when the destination has one).
bool RZDrawBltStretch(cIGZBuffer* dest, cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect,
                      const cRZRect* clip);

void RZDrawRaisedRect(gz::cIGZDrawContext* context, const cRZRect& rect, std::uint32_t rgb, bool raised, bool unused);
void RZDrawRaisedRect(gz::cIGZDrawContext* context, const cRZRect& rect, std::uint8_t r, std::uint8_t g,
                      std::uint8_t b, bool raised, bool unused);
void RZDraw3DRectangleOutline(gz::cIGZDrawContext* context, const cRZRect& rect, std::uint32_t topLeftColor,
                              std::uint32_t bottomRightColor);
void RZDrawRectangleOutline(gz::cIGZDrawContext* context, const cRZRect& rect, std::uint32_t color,
                            std::uint32_t thickness);

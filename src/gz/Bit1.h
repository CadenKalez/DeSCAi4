// Bit1_* — one-bit-per-cell square bitmaps (size x size, rows of size/32 words), used by the flood fill
// and selection tools (0x10014b508..0x10014bd8e). Rectangles are inclusive (left, top, right, bottom).
#pragma once
#include <cstdint>

#include "gz/StreamOps.h"        // cRZRect

void Bit1_PixelSet(std::uint32_t* bits, int row, int column, int size);
bool Bit1_PixelTest(const std::uint32_t* bits, int row, int column, int size);
void Bit1_Clear(std::uint32_t* bits, int size);
void Bit1_Xor(std::uint32_t* destination, const std::uint32_t* source, int size);
// Grows the selection one cell in every direction inside `mask`, up to `maxIterations` rounds; the touched
// row range (min,max) is kept in rowMin / rowMax. Returns the number of rounds performed.
int Bit1_SelectionGrow(std::uint32_t* selection, const std::uint32_t* mask, int size, int maxIterations,
                       int* rowMin, int* rowMax);
bool Bit1_SelectionGrowHoriz(std::uint32_t* selection, const std::uint32_t* mask, int size, int* rowMin, int* rowMax);
bool Bit1_SelectionGrowVert(std::uint32_t* selection, const std::uint32_t* mask, int size, int* rowMin, int* rowMax);
void Bit1_RectSet(std::uint32_t* bits, int size, const cRZRect& rect);
void Bit1_RectClear(std::uint32_t* bits, int size, const cRZRect& rect);
bool Bit1_RectTestAny(const std::uint32_t* bits, int size, const cRZRect& rect);
void Bit1_RectOr(std::uint32_t* destination, const std::uint32_t* source, int size, const cRZRect& rect);

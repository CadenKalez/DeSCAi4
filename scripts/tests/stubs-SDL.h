// Stub SDL.h for building recovered units without SDL installed. Usage:
//   mkdir -p /tmp/stub && cp scripts/tests/stubs-SDL.h /tmp/stub/SDL.h
//   clang++ -std=c++20 -Isrc -I/tmp/stub scripts/tests/cRZTimer_test.cpp src/rz/cRZTimer.cpp src/platform/MacTools.cpp
#pragma once
#include <cstdint>
inline std::uint32_t g_ticks = 0;
inline std::uint32_t SDL_GetTicks() { return g_ticks; }
inline std::uint64_t SDL_GetPerformanceCounter() { return 12345; }
inline std::uint64_t SDL_GetPerformanceFrequency() { return 1'000'000'000ull; }

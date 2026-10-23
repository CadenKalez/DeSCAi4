// EARLY TEST / EXPERIMENTAL HOST — not recovered game code. See README_EARLY_TEST.md.
#pragma once
namespace exphost {
// Runs PreFrameWorkInit -> Init -> PostFrameWorkInit -> shutdown of the recovered cSC4App against the real game data.
bool RunBoot(bool verbose);
}

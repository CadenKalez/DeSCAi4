// EARLY TEST / EXPERIMENTAL HOST — not recovered game code. See README_EARLY_TEST.md.
// The game's own UI path: the recovered UI script service reads the window scripts of the game data (the same scripts
// the game uses) and builds the windows through GZCOM and the recovered control classes. This file supplies what those
// recovered classes reach through seams: class makers for GZCOM, the vocabulary / control / interpreter hosts and the
// image loader.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/Win.h"
#include "gz/cGZPersistResourceKey.h"

class cGZBuffer;
namespace exphost {
class HostWinMgr;

// PNG bytes -> a CPU buffer with one reference (the host's ImageIO code supplies it).
using PngDecoder = cGZBuffer* (*)(const std::vector<std::uint8_t>&);
void SetPngDecoder(PngDecoder decoder);

// Registers the makers, seams and the script service. Needs RunBoot(.., keepAlive = true) to have run (GZCOM, the
// resource manager). Returns false when a step fails (the reason is printed).
bool InitUiScripts(HostWinMgr* mgr);
// SC4CreateWindowFromScript: the window of script `key` (control id `id`) inside `parent`, one reference, or null.
cIGZWin* CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent, std::uint32_t id);
}  // namespace exphost

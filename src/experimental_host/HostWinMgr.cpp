// EARLY TEST / EXPERIMENTAL HOST — not recovered game code. See README_EARLY_TEST.md.
#include "experimental_host/HostWinMgr.h"

#include "gz/WinInterfaces.h"

namespace exphost {

HostWinMgr::HostWinMgr() : gz::cGZWinMgrBase(this) {}
HostWinMgr::~HostWinMgr() = default;

void HostWinMgr::PostWinMessage(std::uint32_t type, std::uint64_t p1, std::uint64_t p2, std::uint64_t p3) {
    cGZMessage message;
    message.type = type;
    if (type == gz::kWinMgrMsgEndModal) {
        // EndModal(window, result): the posted words are (window, result, result)
        modalResult_ = p2;
        modalDone_ = true;
        return;
    }
    if (type >= kGZMsgMouseDownL && type <= kGZMsgMouseWheel) {
        // GZWinMsgPost_Mouse packs (flags, x | y << 16, flags): unpack to the (x, y, flags << 16) form the manager reads.
        const std::int32_t packed = std::int32_t(p2);
        message.param1 = std::uint64_t(std::int64_t(std::int16_t(packed & 0xffff)));
        message.param2 = std::uint64_t(std::int64_t(std::int16_t(packed >> 16)));
        message.param3 = std::uint64_t(p1) << 16;
        ProcessMouseMessage(message);
        return;
    }
    message.param1 = p1; message.param2 = p2; message.param3 = p3;
    if (type == kGZMsgKeyDown || type == kGZMsgKeyUp || type == kGZMsgCharacter) { ProcessKeyboardMessage(message); return; }
    PostMessageToTarget(GetMainWindow(), message);
}

std::uint64_t HostWinMgr::RunModalLoop(cIGZWin*, cIGZFrameWork*, std::uint32_t noResult, std::uint32_t abortCode) {
    modalDone_ = false;
    modalResult_ = noResult;
    while (!modalDone_) {
        if (!modalPump) return abortCode;   // nothing can end the dialog
        modalPump();
    }
    return modalResult_;
}

}  // namespace exphost

#include "sc4/UIFilters.h"

namespace sc4 {

bool cSC4LotConfigurationFilter::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cISC4LotConfigurationFilter) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4LotConfigurationFilter::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

cSC4CtabFilter::cSC4CtabFilter(cIGZWin* window) : window_(window) {
    if (window_) window_->AddRef();
}

cSC4CtabFilter::~cSC4CtabFilter() {
    if (window_) window_->Release();
}

bool cSC4CtabFilter::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cSC4CtabFilter) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cSC4CtabFilter::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

bool cSC4CtabFilter::DoMessage(cIGZWin*, cGZMessage& message) {
    if (message.type == kGZMsgCharacter) {
        const char key = static_cast<char>(message.param1);
        if (key == '\r' || key == '\t') return window_->DoMessage(message);
    }
    return false;
}

}  // namespace sc4

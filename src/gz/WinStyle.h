// The window colour manager's style lookup (RZ::WCM() vslot 0x28): returns the font / text style object registered
// under a style id (a borrowed pointer: callers AddRef it when they keep it).
#pragma once
#include <cstdint>

#include "gz/cIGZUnknown.h"

namespace gz {

class IWinStyleManager {
public:
    virtual ~IWinStyleManager() = default;
    virtual cIGZUnknown* GetFontStyle(std::uint32_t styleId) = 0;      // borrowed pointer, or null when unknown
};
IWinStyleManager* WinStyleManager();
void SetWinStyleManager(IWinStyleManager* manager);

}  // namespace gz

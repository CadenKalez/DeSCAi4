// nsGZWinUtils — small helpers that look a control up by id inside a window and poke it (13 functions at
// 0x1006a1f30..0x1006a2333) plus the recursion callback sSetNotification (0x1006a20e1). Controls are found with the
// recursive child lookups of the parent; a missing control makes the helper answer false.
// Quirk kept: the button / text edit / spinner helpers never release the interface they queried, so each call leaves one
// extra reference on the control.
#pragma once
#include <cstdint>

#include "gz/WinBtn.h"
#include "gz/WinInterfaces.h"
#include "gz/WinSpinner.h"
#include "gz/WinTextEdit.h"

namespace nsGZWinUtils {

bool FindSiblingAs(cIGZWin* window, std::uint32_t id, std::uint32_t iid, void** out);         // @ 0x1006a1f30 (parent->GetChildAs)
bool SetWindowVisible(cIGZWin* window, std::uint32_t id, bool visible, bool invalidate);      // @ 0x1006a1f74
bool GetWindowVisible(cIGZWin* window, std::uint32_t id);                                     // @ 0x1006a1fe8
bool SetWindowEnabled(cIGZWin* window, std::uint32_t id, bool enabled);                       // @ 0x1006a2013
bool SetWindowCaption(cIGZWin* window, std::uint32_t id, const cIGZString& caption);          // @ 0x1006a204a
bool SetWindowNotificationTarget(cIGZWin* window, std::uint32_t id, cIGZWin* target);         // @ 0x1006a208b
void SetWindowNotificationTargetRecursive(cIGZWin* window, cIGZWin* target);                  // @ 0x1006a20be
bool SetWindowNotificationTargetRecursive(cIGZWin* window, std::uint32_t id, cIGZWin* target);   // @ 0x1006a211f
bool SetButtonToggleState(cIGZWin* window, std::uint32_t id, bool on);                        // @ 0x1006a215d
bool GetButtonToggleState(cIGZWin* window, std::uint32_t id);                                 // @ 0x1006a21eb
void ExcludeButtonByRange(cIGZWin* window, std::uint32_t firstId, std::uint32_t count, std::uint32_t onId);   // @ 0x1006a2258
bool SetEditText(cIGZWin* window, std::uint32_t id, const cIGZString& text);                  // @ 0x1006a22a7
bool SetSpinnerValue(cIGZWin* window, std::uint32_t id, int value);                           // @ 0x1006a2333

// EnumChildren callback: points the child (and, recursively, its children) at `target`.
bool sSetNotification(cIGZWin* parent, std::uint32_t id, void* child, void* target);          // @ 0x1006a20e1

}  // namespace nsGZWinUtils

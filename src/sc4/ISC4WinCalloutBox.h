// cISC4WinCalloutBox: the interface of the tool-tip style callout window (cSC4WinCalloutBox, 37 functions at 0x1003c5e30..0x1003c8210,
// interface iid 0xc9b432cf). Slots in the original vtable order; the catalog view is the first user. The implementation is
// cSC4WinCalloutBox (sc4/WinCalloutBox.h), an object with this interface at +0 and its cGZWin at +8.
#pragma once
#include "gz/Win.h"
#include "rz/cRZPoint.h"

class cIGZBuffer;

namespace sc4 {

class cISC4WinCalloutBox : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                              // [3]  0x18
    virtual void SetBackgroundImage(cIGZBuffer* image, int a, int b) = 0;         // [4]  0x20
    virtual cIGZBuffer* GetBackgroundImage() const = 0;                           // [5]  0x28
    virtual void SetTitle(const cIGZString& title) = 0;                           // [6]  0x30
    virtual const cIGZString& GetTitle() const = 0;                               // [7]  0x38
    virtual void SetText(const cIGZString& text) = 0;                             // [8]  0x40
    virtual const cIGZString& GetText() const = 0;                                // [9]  0x48
    virtual void SetTitleFontStyle(std::uint32_t style) = 0;                      // [10] 0x50
    virtual void SetTextFontStyle(std::uint32_t style) = 0;                       // [11] 0x58
    virtual void SetIcon(cIGZBuffer* icon) = 0;                                   // [12] 0x60
    virtual cIGZBuffer* GetIcon() const = 0;                                      // [13] 0x68
    virtual void SetIconMeter(cIGZBuffer* image, std::uint32_t a, float value) = 0;   // [14] 0x70
    virtual void SetPointOfInterest(const cRZPoint& point) = 0;                   // [15] 0x78
    virtual const cRZPoint& GetPointOfInterest() const = 0;                       // [16] 0x80
    virtual void SetAvoidanceRect(const cRZRect& rect) = 0;                       // [17] 0x88
    virtual const cRZRect& GetAvoidanceRect() const = 0;                          // [18] 0x90
    virtual void SetConstraintRect(const cRZRect& rect) = 0;                      // [19] 0x98
    virtual const cRZRect& GetConstraintRect() const = 0;                         // [20] 0xa0
};

}  // namespace sc4

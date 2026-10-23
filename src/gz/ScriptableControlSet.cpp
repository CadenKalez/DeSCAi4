#include "gz/ScriptableControlSet.h"

#include "rz/cRZPoint.h"

cGZScriptableControlSet::cGZScriptableControlSet() = default;   // @ 0x10054c0fa

cGZScriptableControlSet::~cGZScriptableControlSet() {   // @ 0x10054c15a
    for (cIGZScriptableControl* control : controls_) control->Release();
    controls_.clear();
}

bool cGZScriptableControlSet::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x10054c284
    if (iid == kGZIID_cIGZRenderControlSet) {
        *out = static_cast<cIGZRenderControlSet*>(this);   // +8
    } else if (iid == 1 || iid == kGZIID_cIGZScriptableControlSet) {
        *out = static_cast<cIGZScriptableControlSet*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cGZScriptableControlSet::Release() {   // @ 0x10054c2e2
    if (refCount_ > 1) return --refCount_;
    delete this;   // the deleting destructor (vslot 0x68)
    return 0;
}

bool cGZScriptableControlSet::Insert(cIGZScriptableControl* control) {   // @ 0x10054c328
    if (!control) return false;
    control->AddRef();
    controls_.push_back(control);
    return true;
}

cIGZScriptableControl* cGZScriptableControlSet::GetByID(std::uint32_t id) {   // @ 0x10054c37e
    for (cIGZScriptableControl* control : controls_) {
        if (control->GetID() == id) return control;   // vslot 0x28
    }
    return nullptr;
}

cIGZScriptableControl* cGZScriptableControlSet::GetByIndex(std::uint32_t index) {   // @ 0x10054c3f6
    return index < controls_.size() ? controls_[index] : nullptr;
}

namespace {
// The window of a control that answers cIGZLegacyControl (iid 0x9a4527cb); borrowed, like the original's.
cIGZWin* WindowOf(cIGZScriptableControl* control) {
    void* legacy = nullptr;
    if (!control || !control->QueryInterface(kGZIID_cIGZLegacyControl, &legacy)) return nullptr;
    auto* view = static_cast<cIGZLegacyControl*>(legacy);
    cIGZWin* window = view->GetControl();   // vslot 0x18
    view->Release();                        // vslot 0x10
    return window;
}
}  // namespace

cIGZWin* cGZScriptableControlSet::GetByIDAsIGZWin(std::uint32_t id) { return WindowOf(GetByID(id)); }   // @ 0x10054c41a

cIGZWin* cGZScriptableControlSet::GetByIndexAsIGZWin(std::uint32_t index) { return WindowOf(GetByIndex(index)); }   // @ 0x10054c46c

void cGZScriptableControlSet::SetOwnControls(bool own) {   // @ 0x10054c4be
    for (cIGZScriptableControl* control : controls_) control->SetOwnControl(own);   // vslot 0x38
}

bool cGZScriptableControlSet::ThroughClass(std::uint32_t classId, cIGZVariant* value) {   // @ 0x10054c532
    bool result = true;
    for (std::uint32_t index = 0; index < controls_.size(); ++index) {
        cIGZScriptableControl* control = controls_[index];
        void* found = nullptr;
        if (!control || !control->QueryInterface(classId, &found)) continue;   // the query takes a reference on the control
        if (!result) {
            result = false;
        } else {
            result = control->ImplementInterface(this, index, classId, value);   // vslot 0x40: (set, index, class, value)
        }
        control->Release();
    }
    return result;
}

bool cGZScriptableControlSet::Render(cIGZGDriver* driver) {   // @ 0x10054c5e4
    for (cIGZScriptableControl* control : controls_) {
        void* found = nullptr;
        if (control && control->QueryInterface(kGZIID_cIGZControl, &found)) {
            auto* view = static_cast<cIGZControl*>(found);
            view->Render(driver);   // vslot 0x18
            view->Release();        // vslot 0x10
        }
    }
    return true;
}

bool cGZScriptableControlSet::ControlEvent(cRZPoint& point, std::uint32_t event) {   // @ 0x10054c68c
    bool handled = false;
    for (int index = int(controls_.size()) - 1; index >= 0 && !handled; --index) {
        cIGZScriptableControl* control = controls_[std::size_t(index)];
        void* found = nullptr;
        if (!control || !control->QueryInterface(kGZIID_cIGZControl, &found)) continue;
        auto* view = static_cast<cIGZControl*>(found);
        cRZRect area;
        if (view->GetArea(area) &&   // vslot 0x28
            area.left <= point.x && area.top <= point.y && point.x <= area.right && point.y <= area.bottom) {
            handled = view->ControlEvent(point, event);   // vslot 0x30
        }
        view->Release();
    }
    return false;   // the original always answers 0
}

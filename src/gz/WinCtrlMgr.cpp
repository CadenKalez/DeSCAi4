#include "gz/WinCtrlMgr.h"

#include "rz/cRZString.h"

namespace gz {

namespace {
IWinSoundSettings* g_sound = nullptr;
IWinComSource* g_com = nullptr;

struct DefaultColorEntry { std::uint32_t id; std::uint8_t r, g, b; };
constexpr DefaultColorEntry kDefaultColors[] = {
    {0x68963f7d, 0, 0, 0},       {0x28963fc5, 0x80, 0, 0},       {0xa8963f82, 0, 0x80, 0},       {0x48963f84, 0x80, 0x80, 0x40},
    {0x68963f8a, 0, 0, 0x80},    {0xa8963f8c, 0x80, 0, 0x80},    {0x68963f8f, 0, 0x80, 0x80},    {0x08963f91, 0x80, 0x80, 0x80},
    {0x28963f93, 0xc0, 0xc0, 0xc0}, {0xe8963f95, 0xff, 0, 0},    {0x28963f98, 0, 0xff, 0},       {0xe8963f9a, 0xff, 0xff, 0},
    {0xc8963fa2, 0, 0, 0xff},    {0x08963fa5, 0, 0xff, 0xff},    {0x28963fa8, 0xff, 0xff, 0xff}, {0x88963fab, 0x40, 0x40, 0x40},
    {0xa8963fad, 0x40, 0x40, 0x40}, {0x08963faf, 0xc0, 0xc0, 0xc0}, {0xa8963fb2, 0xff, 0xff, 0xff}, {0x88963fb4, 0xc0, 0xc0, 0xc0},
    {0x08963fb9, 0xa0, 0xa0, 0xa0}, {0x28963fbc, 0xff, 0xff, 0xff}};

// Stock system ids.
constexpr int kFontDefault = 0x68963c4c, kFontButtonBold = static_cast<int>(0xc8963c5c), kFontButtonSystem = 0x68963c54;
constexpr int kColorFace = 0x28963f93, kColorShadow = static_cast<int>(0xa8963fad), kColorLight = 0x08963faf;
constexpr int kColorTextDisabled = 0x08963fb9, kColorTextNormal = 0x28963fbc, kColorTextHover = 0x08963fb7;
constexpr int kBmpButton = static_cast<int>(0xe8963ef1), kBmpButtonSmall = static_cast<int>(0xa8963ef3), kBmpButtonLarge = static_cast<int>(0xc8963ef6);
constexpr int kBmpSliderH = static_cast<int>(0x88963ec1), kBmpSliderV = static_cast<int>(0xa8963ec4), kBmpScrollbar = static_cast<int>(0xe8963ec7), kBmpSpinner = 0x68963ed6;

constexpr std::uint32_t kClsButton = 0x22ecfc47, kClsLabel = 0x592, kClsOptGrp = kGZCLSID_cGZWinOptGrp, kClsBMP = 0x82fe68c4,
                        kClsSlider = 0x21325208, kClsScrollbar = 0x61325a2e, kClsSpinner = 0x612ce0c4, kClsGen = 0x4386d516;
constexpr std::uint32_t kIidSlider = 0x21325207, kClsLineInput = 0x21335c5a;

template <class T> T* Typed(void* p) { return static_cast<T*>(p); }
}  // namespace

IWinSoundSettings* WinSoundSettings() { return g_sound; }
void SetWinSoundSettings(IWinSoundSettings* sound) { g_sound = sound; }
IWinComSource* WinComSource() { return g_com; }
void SetWinComSource(IWinComSource* source) { g_com = source; }

cGZWinCtrlMgr::~cGZWinCtrlMgr() = default;   // @ 0x1001f27de

bool cGZWinCtrlMgr::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1001f2832
    if (iid == kGZIID_cIGZWinCtrlMgr) {
        *out = static_cast<cIGZWinCtrlMgr*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cIGZSystemService) {
        *out = static_cast<cIGZSystemService*>(this);
        AddRef();
        return true;
    }
    return cRZUnknown::QueryInterface(iid, out);
}

bool cGZWinCtrlMgr::Init() {   // @ 0x1001f28ec
    if (!initialized_) {
        initialized_ = true;
        for (const DefaultColorEntry& c : kDefaultColors) SetDefaultColor(static_cast<int>(c.id), c.r, c.g, c.b);
    }
    return true;
}

bool cGZWinCtrlMgr::Shutdown() {   // @ 0x1001f2d40
    colors_.clear();
    for (auto& entry : fonts_) if (entry.second) entry.second->Release();
    fonts_.clear();
    for (auto& entry : bitmaps_) if (entry.second) entry.second->Release();
    bitmaps_.clear();
    ClearStrings(texts_);
    ClearStrings(fontMapping_);
    while (!creators_.empty()) RemoveCtrlCreator(creators_.back());
    initialized_ = false;
    return true;
}

IWinFont* cGZWinCtrlMgr::SystemFont(int id) {   // @ 0x1001f2e70
    auto it = fonts_.find(id);
    return it == fonts_.end() ? nullptr : it->second;
}

void cGZWinCtrlMgr::SetSystemFont(int id, IWinFont* font) {   // @ 0x1001f2ecc
    if (font) font->AddRef();
    auto it = fonts_.find(id);
    if (it != fonts_.end()) {
        if (it->second) it->second->Release();
        fonts_.erase(it);
    }
    if (!font) return;
    font->AddRef();
    fonts_.emplace(id, font);
    font->Release();
}

cIGZBuffer* cGZWinCtrlMgr::SystemBMP(int id) {   // @ 0x1001f31a6
    auto it = bitmaps_.find(id);
    return it == bitmaps_.end() ? nullptr : it->second;
}

void cGZWinCtrlMgr::SetSystemBMP(int id, cIGZBuffer* image) {   // @ 0x1001f3204
    if (image) image->AddRef();
    auto it = bitmaps_.find(id);
    if (it != bitmaps_.end()) {
        if (it->second) it->second->Release();
        bitmaps_.erase(it);
    }
    if (!image) return;
    image->AddRef();
    bitmaps_.emplace(id, image);
    image->Release();
}

std::uint32_t cGZWinCtrlMgr::DefaultColor(int id) {   // @ 0x1001f35b8
    auto it = colors_.find(id);
    IWinGraphics* graphics = WinGraphics();
    if (it == colors_.end() || !graphics) return 0;
    const std::uint32_t v = it->second;
    return graphics->MakeNativeColor((v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
}

void cGZWinCtrlMgr::DefaultColor(int id, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {   // @ 0x1001f3616
    auto it = colors_.find(id);
    if (it == colors_.end()) {
        r = g = b = 0;
        return;
    }
    r = (it->second >> 16) & 0xff;
    g = (it->second >> 8) & 0xff;
    b = it->second & 0xff;
}

void cGZWinCtrlMgr::SetDefaultColor(int id, std::uint32_t native) {   // @ 0x1001f370e
    std::uint8_t r = 0, g = 0, b = 0;
    if (IWinGraphics* graphics = WinGraphics()) graphics->NativeToRGB(native, r, g, b);
    SetDefaultColor(id, r, g, b);
}

void cGZWinCtrlMgr::SetDefaultColor(int id, std::uint8_t r, std::uint8_t g, std::uint8_t b) {   // @ 0x1001f37a4
    colors_.erase(id);
    colors_.emplace(id, (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b);
}

cIGZString* cGZWinCtrlMgr::DefaultText(int id) {   // @ 0x1001f3a2e
    auto it = texts_.find(id);
    return it == texts_.end() ? nullptr : it->second;
}

cIGZString* cGZWinCtrlMgr::GetFontMapping(int id) {   // @ 0x1001f3d72
    auto it = fontMapping_.find(id);
    return it == fontMapping_.end() ? nullptr : it->second;
}

int cGZWinCtrlMgr::GetFontMappingID(const cIGZString* name) {   // @ 0x1001f3dd2
    for (auto& entry : fontMapping_)
        if (name->IsEqualToStringPointer(entry.second, true)) return entry.first;
    return 0;
}

void cGZWinCtrlMgr::SetString(StringMap& map, int id, const cIGZString* text) {   // @ 0x1001f3a8e / 0x1001f3e6a
    auto* held = const_cast<cIGZString*>(text);
    if (held) held->AddRef();
    auto it = map.find(id);
    if (it != map.end()) {
        if (it->second) it->second->Release();
        map.erase(it);
    }
    if (!held) return;
    held->AddRef();
    map.emplace(id, held);
    held->Release();
}

void cGZWinCtrlMgr::ClearStrings(StringMap& map) {
    for (auto& entry : map) if (entry.second) entry.second->Release();
    map.clear();
}

template <class T> void* cGZWinCtrlMgr::Make(std::uint32_t classId, std::uint32_t iid) {
    void* out = nullptr;
    if (IWinComSource* source = WinComSource(); source && source->GetCOMObject(classId, iid, &out)) return out;
    T* object = new T(winMgr_);
    object->AddRef();
    object->QueryInterface(iid, &out);
    object->Release();
    return out;
}

bool cGZWinCtrlMgr::QueryAndRelease(void* object, std::uint32_t iid, void** out) {
    auto* unknown = static_cast<cIGZUnknown*>(object);
    const bool ok = unknown->QueryInterface(iid, out);
    unknown->Release();
    return ok;
}

cIGZWinBtn* cGZWinCtrlMgr::CreateButton(std::uint32_t style, std::uint32_t id, const cIGZString& caption, cIGZBuffer* image, const cRZRect* area, std::uint32_t key) {   // @ 0x1001f4186
    auto* button = Typed<cIGZWinBtn>(Make<cGZWinBtn>(kClsButton, kGZIID_cIGZWinBtn));
    cIGZWin* window = button->AsIGZWin();
    IWinFont* font = SystemFont(kFontButtonSystem);
    if (!image) image = SystemBMP(kBmpButton);
    window->SetID(id);
    window->SetCaption(caption);
    window->SetFillColor(DefaultColor(kColorFace));
    button->SetFont(font);
    button->SetStyle(static_cast<int>(style), image, area);
    window->SetKeyEquivalent(key);
    return button;
}

cIGZWinLineInput* cGZWinCtrlMgr::CreateLineInput(std::uint32_t id, bool editable) {   // @ 0x1001f4da6
    auto* input = Typed<cIGZWinLineInput>(Make<cGZWinLineInput>(kClsLineInput, kGZIID_cIGZWinLineInput));
    cIGZWin* window = input->AsIGZWin();
    input->SetEditable(editable);
    input->SetOutlineType(cIGZWinLineInput::kOutlineRaised);
    input->SetOutlineColor(cIGZWinLineInput::kOutlineColor0, DefaultColor(kColorShadow));
    input->SetOutlineColor(cIGZWinLineInput::kOutlineColor1, DefaultColor(kColorLight));
    window->SetID(id);
    return input;
}

cIGZWinText* cGZWinCtrlMgr::CreateLabel(std::uint32_t id, const cIGZString& caption) {   // @ 0x1001f458a
    auto* label = Typed<cIGZWinText>(Make<cGZWinText>(kClsLabel, kGZIID_cIGZWinText));
    cIGZWin* window = label->AsIGZWin();
    IWinFont* font = SystemFont(kFontDefault);
    const std::uint32_t color = font ? font->DefaultTextColor() : 0;
    window->SetID(id);
    window->SetCaption(caption);
    label->SetFont(font);
    label->SetTextColor(color);
    label->SetAlignment(1);
    label->SetBackgroundOpaque(false);
    label->SetWinTextFlag(2, false);
    label->FitWindowToText(false, false);
    return label;
}

cIGZWinOptGrp* cGZWinCtrlMgr::CreateOptGrp(std::uint32_t id, const cIGZString& caption, const cRZRect* area) {   // @ 0x1001f46e2
    auto* group = Typed<cIGZWinOptGrp>(Make<cGZWinOptGrp>(kClsOptGrp, kGZIID_cIGZWinOptGrp));
    cIGZWin* window = group->AsIGZWin();
    window->SetID(id);
    window->SetCaption(caption);
    // The original hands the rectangle to SetArea as (left, right, top, bottom).
    if (area) window->SetArea(area->left, area->right, area->top, area->bottom);
    group->SetOptGrpFlag(1, true);
    group->SetOptGrpFlag(4, false);
    group->SetOptGrpFlag(8, true);
    group->SetOutlineColors(DefaultColor(kColorShadow), DefaultColor(kColorShadow), DefaultColor(kColorLight), DefaultColor(kColorShadow));
    group->SetAutoFillType(0, area ? area->bottom - area->top : 0x80);
    return group;
}

cIGZWinBMP* cGZWinCtrlMgr::CreateWinBMP(std::uint32_t id, cIGZBuffer* image, bool fitToImage) {   // @ 0x1001f48ae
    auto* bmp = Typed<cIGZWinBMP>(Make<cGZWinBMP>(kClsBMP, kGZIID_cIGZWinBMP));
    cIGZWin* window = bmp->AsIGZWin();
    window->SetID(id);
    bmp->SetBMP(image);
    if (image && fitToImage) window->SetSize(image->Width(), image->Height());
    return bmp;
}

cIGZWinSlider* cGZWinCtrlMgr::CreateSlider(std::uint32_t id, int direction, int low, int high, int value) {   // @ 0x1001f49b0
    auto* slider = Typed<cIGZWinSlider>(Make<cGZWinSlider>(kClsSlider, kIidSlider));
    slider->AsIGZWin()->SetID(id);
    slider->SetDirection(static_cast<tSliderWindowDirection>(direction));
    slider->SetImage(SystemBMP(direction == 1 ? kBmpSliderH : kBmpSliderV), nullptr);
    slider->SetMinimumValue(low);
    slider->SetMaximumValue(high);
    slider->SetValue(value);
    return slider;
}

cIGZWinScrollbar* cGZWinCtrlMgr::CreateScrollbar(std::uint32_t id, int direction, int low, int high, int value) {   // @ 0x1001f4ae6
    auto* bar = Typed<cIGZWinScrollbar>(Make<cGZWinScrollbar>(kClsScrollbar, kGZIID_cIGZWinScrollbar));
    cIGZWin* window = bar->AsIGZWin();
    cIGZWinSlider* slider = bar->AsIGZWinSlider();
    cIGZBuffer* image = SystemBMP(kBmpScrollbar);
    window->SetID(id);
    slider->SetDirection(static_cast<tSliderWindowDirection>(direction));
    slider->SetImage(image, nullptr);
    slider->SetMinimumValue(low);
    slider->SetMaximumValue(high);
    bar->SetValue(value);
    return bar;
}

cIGZWinSpinner* cGZWinCtrlMgr::CreateSpinner(std::uint32_t id, int type, int low, int high, int step, int value) {   // @ 0x1001f4c12
    auto* spinner = Typed<cIGZWinSpinner>(Make<cGZWinSpinner>(kClsSpinner, kGZIID_cIGZWinSpinner));
    cIGZWin* window = spinner->AsIGZWin();
    cIGZBuffer* image = SystemBMP(kBmpSpinner);
    const std::uint32_t shadow = DefaultColor(kColorShadow), light = DefaultColor(kColorLight);
    spinner->SetImage(image, nullptr);
    spinner->SetMinMax(low, high);
    spinner->SetValue(value);
    spinner->SetStepSize(step);
    spinner->SetSpinnerType(static_cast<cIGZWinSpinner::tSpinnerType>(type));
    spinner->SetOutlineColors(shadow, shadow, light, light);
    spinner->SetFontColor(cIGZWinSpinner::kColorDisabled, DefaultColor(kColorFace));
    window->SetID(id);
    return spinner;
}

bool cGZWinCtrlMgr::RegisterCtrlCreator(cIGZCtrlCreator* creator) {   // @ 0x1001f4ee0
    void* held = nullptr;
    if (!creator || !creator->QueryInterface(kGZIID_cIGZCtrlCreator, &held)) return false;
    RemoveCtrlCreator(creator);
    if (!creator->Initialize()) return false;
    creators_.push_back(creator);
    return true;
}

bool cGZWinCtrlMgr::RemoveCtrlCreator(cIGZCtrlCreator* creator) {   // @ 0x1001f4f80
    for (auto it = creators_.begin(); it != creators_.end(); ++it) {
        if (*it != creator) continue;
        (*it)->Shutdown();
        (*it)->Release();
        creators_.erase(it);
        return true;
    }
    return false;
}

bool cGZWinCtrlMgr::CreateCtrl(std::uint32_t classId, std::uint32_t iid, void** out) {   // @ 0x1001f5004
    for (auto it = creators_.rbegin(); it != creators_.rend(); ++it)
        if ((*it)->CreateCtrl(classId, iid, out)) return true;
    return CreateDefaultCtrl(classId, iid, out);
}

bool cGZWinCtrlMgr::CreateButtonCtrl(std::uint32_t classId, std::uint32_t iid, void** out) {   // 0x68963e48 / 0xa8963e4b
    auto* button = Typed<cIGZWinBtn>(Make<cGZWinBtn>(kClsButton, kGZIID_cIGZWinBtn));
    const bool ok = button->QueryInterface(iid, out);
    if (ok) {
        cIGZBuffer* image = SystemBMP(classId == 0x68963e48 ? kBmpButtonSmall : kBmpButtonLarge);
        IWinFont* font = SystemFont(kFontButtonBold);
        if (!font) font = SystemFont(kFontDefault);
        cRZRect area;
        if (image) {
            image->GetBufferArea(area);
        } else {
            area = cRZRect{0, 0, 0x80, font ? font->LineHeight(0, 0) : 0x20};
        }
        button->SetFont(font);
        button->SetFontColor(4, DefaultColor(kColorTextDisabled));
        button->SetFontColor(2, DefaultColor(kColorTextNormal));
        button->SetFontColor(0, DefaultColor(kColorTextHover));
        button->SetCaptionStyle(2);
        button->SetGutters(2, 2);
        button->SetStyle(4, image, &area);
    }
    button->Release();
    return ok;
}

bool cGZWinCtrlMgr::CreateOptGrpCtrl(std::uint32_t iid, void** out) {   // 0xc8963e3c
    auto* group = Typed<cIGZWinOptGrp>(Make<cGZWinOptGrp>(kClsOptGrp, kGZIID_cIGZWinOptGrp));
    const bool ok = group->QueryInterface(iid, out);
    if (ok) {
        group->SetOptGrpFlag(1, true);
        group->SetOptGrpFlag(4, false);
        group->SetOptGrpFlag(8, true);
        group->SetOutlineColors(DefaultColor(kColorShadow), DefaultColor(kColorShadow), DefaultColor(kColorLight), DefaultColor(kColorShadow));
    }
    group->Release();
    return ok;
}

bool cGZWinCtrlMgr::CreateListBoxCtrl(std::uint32_t iid, void** out) {   // 0x28963e36
    auto* list = Typed<cIGZWinListBox>(Make<cGZWinListBox>(kGZCLSID_cGZWinListBox, kGZIID_cIGZWinListBox));
    const bool ok = list->QueryInterface(iid, out);
    if (ok) {
        std::uint8_t r = 0, g = 0, b = 0;
        DefaultColor(static_cast<int>(0xa8963fb2), r, g, b);
        list->AsIGZWin()->SetFillColor(r, g, b);
        list->SetFont(SystemFont(kFontDefault));
        list->SetFontColor(cIGZWinListBox::kColorText, DefaultColor(kColorTextHover));
        list->SetFontColor(cIGZWinListBox::kColorSelected, DefaultColor(kColorTextNormal));
        list->SetFontColor(cIGZWinListBox::kColorDisabled, DefaultColor(kColorTextDisabled));
        list->SetHighlightColor(DefaultColor(static_cast<int>(0x88963fb4)));
        list->SetHighlightFillAlpha(1.0f);
        list->SetOutlineDrawing(true);
        list->SetDrawSunkenRect(true);
        list->SetOutlineColors(DefaultColor(kColorShadow), DefaultColor(kColorLight));
    }
    list->Release();
    return ok;
}

bool cGZWinCtrlMgr::CreateDefaultCtrl(std::uint32_t classId, std::uint32_t iid, void** out) {   // @ 0x1001f5098
    const cRZString empty;
    switch (classId) {
        case 0x88963e39: {   // generic window (cGZWinGen)
            void* object = nullptr;
            if (IWinComSource* source = WinComSource(); source && source->GetCOMObject(kClsGen, 0x5386d516, &object)) {
                return QueryAndRelease(object, iid, out);
            }
            auto it = fallbacks_.find(kClsGen);
            if (it == fallbacks_.end()) return false;
            cIGZUnknown* made = it->second();
            return QueryAndRelease(made, iid, out);
        }
        case 0x88963e15:
        case 0xc8963e1d: return QueryAndRelease(CreateButton(2, 0, empty, nullptr, nullptr, 0), iid, out);
        case 0xe8963e24: return QueryAndRelease(CreateButton(3, 0, empty, nullptr, nullptr, 0), iid, out);
        case 0x68963e21: return QueryAndRelease(CreateToggleButton(0, empty, nullptr, nullptr, 0), iid, out);
        case 0xc8963e3c: return CreateOptGrpCtrl(iid, out);
        case 0x28963e36: return CreateListBoxCtrl(iid, out);
        case 0xe8963e4d: {
            void* object = Make<cGZWinBMP>(kClsBMP, kGZIID_cIGZWinBMP);
            return QueryAndRelease(object, iid, out);
        }
        case 0x28963e2c: return QueryAndRelease(CreateScrollbar(0, 1, 0, 200, 100), iid, out);
        case 0x48963e30: return QueryAndRelease(CreateScrollbar(0, 2, 0, 200, 100), iid, out);
        case 0x48963e33: return QueryAndRelease(CreateLabel(0, empty), iid, out);
        case 0x68963e48:
        case 0xa8963e4b: return CreateButtonCtrl(classId, iid, out);
        default: return false;   // text edit and line input controls are not recovered yet
    }
}

}  // namespace gz

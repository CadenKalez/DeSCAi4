#include "sc4/ResourceCatalogView.h"

#include "gz/Buffer.h"
#include "rz/RZConvert.h"
#include "rz/cRZFile.h"
#include "rz/cRZIniFile.h"
#include "rz/cRZPath.h"
#include "sc4/SLBuffer.h"

namespace {

IResourceCatalogHost* g_host = nullptr;

// The catalog's ini (RZ::DDir() + "Sys" + '/' + "Ingred.ini", built inline at 0x100328ec1..0x100328f1e).
constexpr const char* kIngredDir = "Sys";
constexpr const char* kIngredFile = "Ingred.ini";
// The section registerTypes enumerates. Spelled exactly like the literal the binary builds on the stack at
// 0x100328f78 (0x24 size byte, "Exampler" + "Categori" + "es"); the odd 'r' is the original's, not a typo here.
constexpr const char* kTypeSection = "ExamplerCategories";

std::uint32_t Min(std::uint32_t a, std::uint32_t b) { return a < b ? a : b; }

}  // namespace

IResourceCatalogHost* ResourceCatalogHost() { return g_host; }
void SetResourceCatalogHost(IResourceCatalogHost* host) { g_host = host; }

// @ 0x100327af0 (complete-object variant 0x100327b9c). The window base is default-constructed (cGZWin::cGZWin()
// @ 0x100168870 zeroes the manager and keyboard); the director gives it a manager afterwards.
cSCResourceCatalogView::cSCResourceCatalogView() : cGZWin(nullptr) {
    readOnly_ = 0;      // *(uint16*)(this + 0x158) = 0x100
    standalone_ = 1;
    filterType_ = 0;    // *(uint64*)(this + 0x15c) = 0
    filterGroup_ = 0;
    comboRow_.clear();
    types_.clear();
    selectedView_ = nullptr;
    grid_ = nullptr;
}

// @ 0x100327c30 (+ the complete-object form 0x100327cc6, the deleting form 0x100327cde and their +8 thunks). The
// original destroys the vector at +0x190, the map at +0x168 and then the cGZWin at +8; the members do that here.
cSCResourceCatalogView::~cSCResourceCatalogView() = default;

// @ 0x100327d1a: the catalog answers its own interface, everything else is the window's.
bool cSCResourceCatalogView::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISCResourceCatalogView) {
        *out = static_cast<cISCResourceCatalogView*>(this);
        AddRef();
        return true;
    }
    return cGZWin::QueryInterface(iid, out);
}

// @ 0x100327d92. The whole window is built here from the row list registerTypes fills: the flat-rect backdrop, the
// four buttons, the "Select Type:" label, the type combo (one item per row that passes the type/group filter, each
// row's view window hidden behind the same area), the "Instances:" label, the instance grid and the two file-name
// labels; updateKeyList then fills the grid for the first selectable row.
bool cSCResourceCatalogView::Init() {
    IResourceCatalogHost* host = ResourceCatalogHost();
    // The colours are made before the already-initialised early exit (the original calls MakeFillColor five times
    // either way).
    const std::uint32_t black = MakeFillColor(0, 0, 0);
    const std::uint32_t navy = MakeFillColor(0, 0, 0x80);
    const std::uint32_t white = MakeFillColor(0xff, 0xff, 0xff);
    const std::uint32_t grey = MakeFillColor(0xf0, 0xf0, 0xf0);
    const std::uint32_t blackInk = MakeFillColor(0, 0, 0);
    const std::uint32_t limit = Min(static_cast<std::uint32_t>(GetW()) / 3u, 200u);   // 0x100327e36..0x100327e66
    if (get_init_flag()) return true;

    SetFillColor(MakeFillColor(0xf0, 0xf0, 0xf0));   // 0x348
    SetFlag(0x800, false);                            // 0x220
    SetFlag(0x10000, true);
    cGZWin::Init();

    // The flat-rect backdrop fills the window (0x100327edf..0x100327fc5). Its three extra calls set the outline
    // colour to black, the size mode to 0 and all four outline edges.
    if (host) {
        if (IResourceCatalogBackdrop* flat = host->CreateBackdrop()) {
            cIGZWin* win = flat->AsIGZWin();
            win->SetArea(0, 0, GetW(), GetH());     // 0x1b0
            win->SetFillColor(grey);                // 0x348
            win->SetFlag(0x800, false);             // 0x220
            flat->SetOutlineColor(black);           // 0x70
            flat->SetSizeMode(0);                   // 0x90
            flat->SetOutlineFlags(0xf);             // 0x48
            ChildAdd(win);
            flat->Release();
        }
    }

    // The buttons are laid out right-to-left along the bottom, all on one row (0x100327fc7..0x10032831c). The row's
    // y is set once: from the "Done" button when standalone (which exists then) and from "Add New Resource"
    // otherwise, so the remaining buttons reuse it even when their heights differ. "Done" and "Cancel" only exist in
    // standalone mode.
    int x = GetW();
    int y = GetH();
    if (standalone_) {
        IResourceCatalogButton* button = nullptr;
        createButton(button, kResImgDone, kResourceButtonImageGroup, kResCmdDone, cRZString("Done"));
        if (button) {
            cIGZWin* win = button->AsIGZWin();
            x = x - 5 - win->GetW();
            y = y - 5 - win->GetH();
            ChildAdd(win);
            win->GZWinMoveTo(x, y);
            button->Release();
        }
    }
    {
        IResourceCatalogButton* button = nullptr;
        createButton(button, kResImgAdd, kResourceButtonImageGroup, kResCmdAdd, cRZString("Add New Resource"));
        if (button) {
            cIGZWin* win = button->AsIGZWin();
            x = x - 5 - win->GetW();
            if (!standalone_) y = y - 5 - win->GetH();
            ChildAdd(win);
            win->GZWinMoveTo(x, y);
            button->Release();
        }
    }
    {
        IResourceCatalogButton* button = nullptr;
        createButton(button, kResImgRemove, kResourceButtonImageGroup, kResCmdRemove,
                     cRZString("Remove Selected Resource"));
        if (button) {
            cIGZWin* win = button->AsIGZWin();
            x = x - 5 - win->GetW();
            ChildAdd(win);
            win->GZWinMoveTo(x, y);
            button->Release();
        }
    }
    if (standalone_) {
        IResourceCatalogButton* button = nullptr;
        createButton(button, kResImgCancel, kResourceButtonImageGroup, kResCmdCancel, cRZString("Cancel"));
        if (button) {
            cIGZWin* win = button->AsIGZWin();
            x = x - 5 - win->GetW();
            ChildAdd(win);
            win->GZWinMoveTo(x, y);
            button->Release();
        }
    }

    // "Select Type:" at (5, 5); its height + 10 is the top of the combo (0x10032831c..0x1003283db).
    std::uint32_t labelHeight = 0;
    {
        IResourceCatalogLabel* label = nullptr;
        createLabel(label, cRZString("Select Type:"), black);
        if (label) {
            cIGZWin* win = label->AsIGZWin();
            ChildAdd(win);
            win->GZWinMoveTo(5, 5);
            labelHeight = static_cast<std::uint32_t>(win->GetH());
            label->Release();
        }
    }
    const std::uint32_t comboTop = labelHeight + 10u;         // -0x90
    selectedRow_ = 0;                                         // *(uint32*)(this + 0x180) = 0
    registerTypes();
    std::uint32_t labelY = comboTop;                          // r15
    {
        IResourceCatalogCombo* combo = host ? host->CreateTypeCombo() : nullptr;
        if (combo) {
            cIGZWin* win = combo->AsIGZWin();
            win->SetID(kResourceTypeComboID);
            win->SetArea(5, static_cast<int>(comboTop), static_cast<int>(limit + 5u),
                         static_cast<int>(labelHeight + 0x1cu));
            combo->SetEditable(false);                       // 0xa8 (0)
            std::uint32_t comboRow = 0;
            const int shrinkY = GetH() - 0xf;                // the views are 15 pixels shorter than the window
            for (std::uint32_t row = 0; row < types_.size(); ++row) {
                const tTypeNameAssoc& entry = types_[row];
                // (filterGroup == 0 || entry.group == filterGroup) && (filterType == 0 || entry.type == filterType)
                if (!(filterGroup_ == 0 || entry.group == filterGroup_)) continue;
                if (!(filterType_ == 0 || entry.type == filterType_)) continue;
                if (!entry.view) continue;
                entry.view->SetReadOnly(readOnly_ != 0);     // 0x30
                cIGZWin* viewWindow = entry.view->AsIGZWin();   // 0x18
                if (!viewWindow) continue;
                viewWindow->SetArea(0, 0, GetW() - 0x14 - static_cast<int>(limit), shrinkY);
                viewWindow->GZWinMoveTo(static_cast<int>(limit + 10u), 5);
                viewWindow->HideWindow();
                ChildAdd(viewWindow);
                combo->InsertString(entry.name, -2);         // 0xd8 (append)
                comboRow_[comboRow] = row;
                ++comboRow;
            }
            combo->SetHighlightColor(navy);                  // 0x80
            win->SetFillColor(grey);                         // 0x348
            combo->SetSelection(0, false);                   // 0xe0
            std::uint32_t first = 0;
            selectedRow_ = comboRow_[first];
            ChildAdd(win);
            combo->SetFontColor(0, black);                   // 0x60 (text colour)
            combo->SetFontColor(2, white);                   // 0x60 (selected colour)
            combo->Release();
            labelY = labelHeight + 0x21u;
        }
    }

    // "Instances:" label, then the grid under it (0x100328720..0x1003289ab).
    {
        IResourceCatalogLabel* label = nullptr;
        createLabel(label, cRZString("Instances:"), black);
        if (label) {
            cIGZWin* win = label->AsIGZWin();
            win->GZWinMoveTo(5, static_cast<int>(labelY));
            win->SetID(kResourceInstancesLabelID);
            const std::uint32_t instancesHeight = static_cast<std::uint32_t>(win->GetH());
            ChildAdd(win);
            label->Release();
            if (!grid_ && host) {
                const int gridY = static_cast<int>(labelY + instancesHeight + 5u);
                grid_ = host->CreateGrid(0x19bff);
                if (grid_) {
                    grid_->AddRef();
                    grid_->ActLikeAListbox(1, 1, false);                       // 0xf0
                    grid_->SetDefaultRowHeight(0xf);                           // 0x210
                    grid_->SetArea(5, gridY, static_cast<int>(limit + 5u), GetH() - 5);   // 0x38
                    grid_->SetColumnWidth(0, 1, static_cast<int>(limit - 6u)); // 0x208
                    grid_->ClearFlag(2);                                       // 0x60
                    grid_->ClearFlag(0x400);
                    grid_->AsIGZWin()->SetID(kResourceInstanceGridID);
                    grid_->SetDefaultFontColor(blackInk, 0);                   // 0x88
                    grid_->SetDefaultFontColor(white, 4);
                    grid_->SetDefaultFontColor(navy, 5);
                    grid_->AsIGZWin()->SetFillColor(grey);
                    ChildAdd(grid_->AsIGZWin());
                }
            }
        }
    }

    // "Filename:" and the "<none>" value, both 200 pixels wide (0x1003289ad..0x100328b45).
    {
        IResourceCatalogLabel* label = nullptr;
        createLabel(label, cRZString("Filename:"), black);
        if (label) {
            cIGZWin* win = label->AsIGZWin();
            win->GZWinMoveTo(0xdc, 0x1ae);
            win->SetID(kResourceFileNameLabelID);
            win->SetW(200);
            ChildAdd(win);
            label->Release();
        }
    }
    {
        IResourceCatalogLabel* label = nullptr;
        createLabel(label, cRZString("<none>"), black);
        if (label) {
            cIGZWin* win = label->AsIGZWin();
            win->GZWinMoveTo(0xe6, 0x1c2);
            win->SetW(200);
            win->SetID(kResourceFileNameValueID);
            ChildAdd(win);
            label->Release();
        }
    }

    cGZPersistResourceKey none{};
    updateKeyList(none);
    if (grid_) mgr_->GZSetFocus(grid_->AsIGZWin());
    return true;
}

// @ 0x100328bce: createButton(cIGZWinBtn*&, instance, group, id, caption). The button face is a cSLBuffer of the
// standard bitmap type (0) with the sheet group and the frame instance; without an image the button is made
// 26 x 16 instead. All four buttons get the tip flag and the caption as their tip text.
void cSCResourceCatalogView::createButton(IResourceCatalogButton*& button, std::uint32_t instance,
                                          std::uint32_t group, std::uint32_t id, const cRZString& caption) {
    IResourceCatalogHost* host = ResourceCatalogHost();
    button = nullptr;
    cSLBuffer image(0u, group, instance, 1, false);   // cSLBuffer(type, group, instance, colour type, always reload)
    if (!host) return;
    if (cIGZBuffer* buffer = image.GetBuffer())
        button = host->CreateFixedButton(id, caption, buffer, buffer->GetBufferArea());
    else {
        button = host->CreateFixedButton(id, caption, nullptr, nullptr);
        // The original dereferences the result without a null test; it never fails with the real control manager.
        if (button) button->AsIGZWin()->SetSize(0x1a, 0x10);
    }
    if (!button) return;
    button->FitSizeToContent();        // 0x108
    button->SetBtnFlag(0x100, true);   // tip enabled
    button->SetBtnFlag(0x200, false);
    button->SetBtnFlag(0x400, false);
    button->SetTipText(caption);       // 0x118
}

// @ 0x100328d66: createLabel(cIGZWinText*&, caption, text colour). The label is created through GZCOM (class 0x592,
// iid 0x212cdc1f), gets its font from the window control manager, the given text colour, no wrapping/pass-through,
// and is sized to its text before the caller positions it.
void cSCResourceCatalogView::createLabel(IResourceCatalogLabel*& label, const cRZString& caption,
                                         std::uint32_t color) {
    IResourceCatalogHost* host = ResourceCatalogHost();
    label = nullptr;
    if (!host) return;
    label = host->CreateLabel();
    if (!label) return;
    label->SetWinTextFlag(2, false);                                  // 0x38 (wrap off)
    label->SetWinTextFlag(4, false);                                  // 0x38 (opaque background off)
    label->SetFont(host->SystemFont(kResourceLabelFontStyle));        // 0x90
    label->SetTextColor(color);                                       // 0x40
    label->AsIGZWin()->SetCaption(caption);                           // 0x250
    label->FitWindowToText(false, false);                             // 0x28
    label->AsIGZWin()->SetFlag(0x10000, false);                       // 0x220 (no private buffer)
    label->AsIGZWin()->SetFlag(0x200000, true);                       // 0x220 (ignore the cursor)
}

// @ 0x100328e86: open <data dir>Sys/Ingred.ini and hand its [ExamplerCategories] entries to registerTypesFromIni.
void cSCResourceCatalogView::registerTypes() {
    IResourceCatalogHost* host = ResourceCatalogHost();
    if (!host) return;
    const std::string base = host->DataDirectory() + kIngredDir;      // RZ::DDir() + "Sys"
    const std::string path = base + '/' + kIngredFile;                // 0x1003ed0c0 appends '/' here
    cRZString iniPath(path.c_str());
    cRZIniFile ini(iniPath);
    cRZString section(kTypeSection);
    if (ini.SectionExists(section)) ini.EnumEntries(section, &cSCResourceCatalogView::registerTypesFromIni, this);
}

// @ 0x100329424: one ini entry, `Name=exemplarGroup[,ltextGroup]`. The value is split on ','; the first group makes
// the exemplar-type row and a non-zero second group an LTEXT row whose name carries the original's " text" suffix.
void cSCResourceCatalogView::registerTypesFromIni(const cRZString& key, const cRZString& value, void* context) {
    auto* self = static_cast<cSCResourceCatalogView*>(context);
    IResourceCatalogHost* host = ResourceCatalogHost();
    if (!self || !host) return;

    // Split at most two comma-separated parts (0x1003294c9..0x100329584); each round LTrims the remainder first
    // (0x100329504 calls LTrim before SplitToken), which is what lets the ini write "group1, group2" with a space.
    cRZString rest(value);
    std::string parts[2];
    for (int i = 0; i < 2; ++i) {
        rest.LTrim();
        cRZString token = rest.SplitToken(",");
        if (token.Strlen() != 0) {
            parts[i].assign(token.ToChar(), token.Strlen());
        } else {
            rest.RTrim();
            parts[i].assign(rest.ToChar(), rest.Strlen());
            break;
        }
    }
    const std::uint32_t exemplarGroup = RZ::ConvertToUint(parts[0].c_str());
    const std::uint32_t ltextGroup = RZ::ConvertToUint(parts[1].c_str());

    IResourceTypeView* view = host->CreateTypeView();
    if (!view) return;
    {
        tTypeNameAssoc row;
        row.type = kResourceExemplarType;
        row.group = exemplarGroup;
        row.name = key;
        row.view = view;                 // the row's counted reference
        self->types_.push_back(row);     // copies (tTypeNameAssoc copy ctor @ 0x10032a864)
    }
    if (ltextGroup != 0) {
        IResourceTypeView* textView = host->CreateTypeView();
        if (textView) {
            tTypeNameAssoc row;
            row.type = kResourceTextType;
            row.group = ltextGroup;
            // std::operator+(entry name, " Cohort") — the literal read from 0x1009f3c6c.
            row.name = cRZString((key.ToChar() + std::string(" Cohort")).c_str());
            row.view = textView;
            self->types_.push_back(row);
        }
    }
    view->Release();                     // the temporary's reference (the rows hold their own)
}

// @ 0x1003290b6: fill the grid with the available resources of the selected row's type/group. The row whose key
// matches `current` is selected (otherwise the first one) and its key is handed to setViewKey. The resource name is
// the LTEXT/exemplar name when the manager has one, else the instance formatted "0x%08X".
void cSCResourceCatalogView::updateKeyList(const cGZPersistResourceKey& current) {
    IResourceCatalogHost* host = ResourceCatalogHost();
    if (types_.empty() || selectedRow_ >= types_.size() || !grid_ || !host) return;
    const tTypeNameAssoc& entry = types_[selectedRow_];
    const cGZPersistResourceKey typeKey{entry.type, entry.group, 0};
    grid_->ClearAllCells();                               // 0x310
    // The grid's cells keep pointers into this list until the next call (the original keeps pointers into the
    // manager's key list, which it releases at the end of the block).
    const std::vector<cGZPersistResourceKey>& keys = host->AvailableResources(typeKey.type);
    int count = 0;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const cGZPersistResourceKey& key = keys[i];
        if (key.group != typeKey.group) continue;
        cRZString name;
        if (!host->ResourceName(key, name)) name.Sprintf("0x%08X", key.instance);
        grid_->SetCellTextValue(0, count, name.ToChar(), 0xffffffffu, 1, false);   // 0x380
        grid_->SetCellExtraData(0, count, const_cast<cGZPersistResourceKey*>(&key));   // 0x3b8
        ++count;
    }
    grid_->SortColumn(0, 0, true, nullptr);               // 0x418
    if (count > 0) {
        grid_->SetRowHeight(0, count, 0xf);               // 0x218
        cGZPersistResourceKey chosen{};
        for (int row = 0; row < count; ++row) {
            const auto* key = static_cast<const cGZPersistResourceKey*>(grid_->GetCellExtraData(0, row));   // 0x3c0
            if (!key) continue;
            if (row == 0 || current == *key) {
                chosen = *key;
                grid_->SetSelection(0, row);              // 0x158
            }
        }
        setViewKey(chosen);
    }
}

// @ 0x100329be4: show the view of the selected row and give it the key. The outgoing view is asked to save its
// pending edit and hidden; the incoming one is shown and focused. The selected-view pointer is borrowed.
void cSCResourceCatalogView::setViewKey(const cGZPersistResourceKey& key) {
    IResourceTypeView* target = nullptr;
    if (selectedRow_ < types_.size() && types_[selectedRow_].view) target = types_[selectedRow_].view;
    if (selectedView_ != target) {
        if (selectedView_) {
            selectedView_->PromptToSave();                                    // 0x28
            if (cIGZWin* old = selectedView_->AsIGZWin()) old->HideWindow();  // 0x18, 0x230
        }
        selectedView_ = target;
        if (!target) return;
        if (cIGZWin* win = target->AsIGZWin()) win->ShowWindow();             // 0x18, 0x228
        target = selectedView_;
    }
    if (!target) return;
    target->SetKeyToView(key);                                                // 0x20
    if (grid_ && mgr_) mgr_->GZSetFocus(grid_->AsIGZWin());                   // 0x128
}

// @ 0x100329cc8: re-read the grid for the key the user has selected.
void cSCResourceCatalogView::RefreshKeyList() {
    cGZPersistResourceKey key{};
    if (GetSelectedKey(key)) updateKeyList(key);
}

// @ 0x100329d04 (0x100329d54 for the key-up variant): forward the key message to the notification target.
bool cSCResourceCatalogView::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {
    // The original tail-calls cGZWin::SendMsg(target, 5, key, modifiers, 0) (vslot 0x488) and returns its result;
    // the recovered five-argument overload drops that result, so the message is built here instead.
    cGZMessage message;
    message.type = kGZMsgKeyDown;
    message.param1 = key;
    message.param2 = modifiers;
    return SendMsg(GetNotificationTarget(), message);
}
bool cSCResourceCatalogView::GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) {
    cGZMessage message;
    message.type = kGZMsgKeyUp;
    message.param1 = key;
    message.param2 = modifiers;
    return SendMsg(GetNotificationTarget(), message);
}

// @ 0x100329da4 / 0x100329e30 / 0x100329eb4: the window is dragged around by its own body (screen coordinates, the
// offset between the pointer and the window's top-left is kept in +0x184/+0x188).
bool cSCResourceCatalogView::GZOnMouseDownL(int x, int y, std::uint32_t flags) {
    (void)flags;
    GZSetCapture();
    int screenX = x;
    int screenY = y;
    WindowToScreenCoordinates(screenX, screenY);
    dragOffsetX_ = GetL() - screenX;
    dragOffsetY_ = GetT() - screenY;
    return true;
}

bool cSCResourceCatalogView::GZOnMouseMove(int x, int y, std::uint32_t flags) {
    (void)flags;
    if (GZAmCapturing()) {
        int screenX = x;
        int screenY = y;
        WindowToScreenCoordinates(screenX, screenY);
        GZWinMoveTo(screenX + dragOffsetX_, screenY + dragOffsetY_);
    }
    return true;
}

bool cSCResourceCatalogView::GZOnMouseUpL(int x, int y, std::uint32_t flags) {
    (void)x;
    (void)y;
    (void)flags;
    GZReleaseCapture();
    return true;
}

// @ 0x100329ed4: the key of the grid's selected cell (the cell's extra data points at the key).
bool cSCResourceCatalogView::GetSelectedKey(cGZPersistResourceKey& key) {
    if (!grid_) return false;
    int column = 0;
    int row = 0;
    grid_->GetSelection(column, row);                      // 0x148
    const auto* selected = static_cast<const cGZPersistResourceKey*>(grid_->GetCellExtraData(column, row));   // 0x3c0
    if (!selected) return false;
    key = *selected;
    return true;
}

// @ 0x100329f4a: paint the background through the draw buffer (lock 0x8001, fill the draw area with colour 0,
// unlock) and then let the window paint its children.
bool cSCResourceCatalogView::GZPaint() {
    if (drawBuffer_ && drawBuffer_->Lock(0x8001)) {
        drawBuffer_->Fill(0, drawArea_);      // the original also passes a third argument (0 = no clip)
        drawBuffer_->Unlock(0x8001);
    }
    return cGZWin::GZPaint();
}

// @ 0x100329afa: the grid's selection/key notifications select the cell's key, a focus change goes to the message
// server, everything else is the window's.
bool cSCResourceCatalogView::DoMessage(cGZMessage& message) {
    if (static_cast<std::int32_t>(message.type) < 0xd) {
        if (message.type == kGridSelectionChangedMsg || message.type == kGridKeyMsg) {
            // The sender is checked (its window id must be the instance grid's) but the selection is read from our
            // own grid pointer, as in the original.
            auto* sender = reinterpret_cast<IResourceCatalogGrid*>(message.param1);
            cRZRect used;
            if (sender && sender->AsIGZWin() && sender->AsIGZWin()->GetID() == kResourceInstanceGridID &&
                sender->GetCellUsedRange(used) && grid_) {
                int column = 0;
                int row = 0;
                if (grid_->GetSelection(column, row)) {
                    const auto* key = static_cast<const cGZPersistResourceKey*>(grid_->GetCellExtraData(column, row));
                    if (key) setViewKey(*key);
                }
            }
            return true;
        }
        return cGZWin::DoMessage(message);
    }
    if (message.type == kFocusChangeMsg) {
        if (IResourceCatalogHost* host = ResourceCatalogHost()) host->PostMessage(message);
        return true;
    }
    return cGZWin::DoMessage(message);
}

// @ 0x100329fb2: the combo (command 0xe8711255, p2 = the combo's window id) re-maps the selected item to a row and
// refills the grid; a button (0x287259f6) goes to doButtonPress. Quirk: the combo path answers false.
bool cSCResourceCatalogView::GZOnCommand(std::uint32_t type, std::uint64_t param) {
    if (type == kComboChangedCommand) {
        if (param == kResourceTypeComboID) {
            cIGZWin* child = GetChildWindowFromID(kResourceTypeComboID);   // 0x110
            if (!child) return true;                                       // 0x10032a0bb: true when missing
            IResourceCatalogHost* host = ResourceCatalogHost();
            if (host) {
                // The original QueryInterfaces the child for 0x412ce496 and, when that fails as well, still falls
                // through to the refresh below.
                if (IResourceCatalogCombo* combo = host->ComboFromWindow(child)) {
                    const std::uint32_t selected = static_cast<std::uint32_t>(combo->GetSelection());   // 0xe8
                    selectedRow_ = comboRow_[selected];
                }
            }
        }
        cGZPersistResourceKey key{};
        updateKeyList(key);
        if (grid_ && mgr_) mgr_->GZSetFocus(grid_->AsIGZWin());
        return false;   // quirk: the combo path reports "not handled"
    }
    if (type == kButtonActivatedCommand) return doButtonPress(static_cast<std::uint32_t>(param));
    return false;
}

// @ 0x10032a0c0: the button commands. "Add New Resource" falls through into the "Remove" exit (answers true), the
// "Remove Selected Resource" command does nothing at all, "Cancel" kills the window with result 0 and "Done" asks
// the selected view to save, then kills it with result 1.
bool cSCResourceCatalogView::doButtonPress(std::uint32_t command) {
    switch (command) {
        case kResCmdDone:
            if (selectedView_) selectedView_->PromptToSave();   // 0x28
            KillSelf(1);
            return false;
        case kResCmdAdd:
            addResource();
            return true;
        case kResCmdCancel:
            KillSelf(0);
            return false;
        case kResCmdRemove:
            return true;
        default:
            return false;
    }
}

// @ 0x10032a146: close the window. A modal window (flag 0x1000) ends the dialog with `result`; otherwise the window
// posts a delete-child message (type 1) to its parent.
void cSCResourceCatalogView::KillSelf(std::uint32_t result) {
    if (GetFlag(0x1000)) {
        if (mgr_) mgr_->EndDialog(0xf, static_cast<cGZWin*>(this), result);
    } else {
        PostMsg(GetParentWin(), kGZMsgDeleteChild, reinterpret_cast<std::uint64_t>(static_cast<cIGZWin*>(this)), 0, 0);
    }
}

// @ 0x10032a1b8: ask for a new resource with the key input view and move the group's file to the chosen name. The
// new key starts from the selected row's type/group with instance 1.
bool cSCResourceCatalogView::addResource() {
    IResourceCatalogHost* host = ResourceCatalogHost();
    if (!host) return false;
    if (selectedRow_ >= types_.size()) return false;
    const tTypeNameAssoc& entry = types_[selectedRow_];
    cGZPersistResourceKey key{entry.type, entry.group, 1};
    cRZString fileName;
    cGZPersistResourceKey chosen{};
    if (!host->RunKeyInputDialog(key, chosen, fileName)) return false;
    if (host->TestForKey(chosen)) return false;                       // 0x70
    if (!host->CreateNewResource(chosen)) return false;               // 0x28 (iid 0x456b8f1d)
    if (!host->SaveResourceToGroupSegment(chosen)) return false;      // 0x100 + segment 0x58
    cRZString currentFile;
    if (!host->ResourceFileName(chosen, currentFile)) return false;   // 0xf8 + QI 0x856c8cf7 + 0x90
    // New path = the old file's drive+directory + the chosen name + the old file's extension.
    const cRZPath oldPath{std::string(currentFile.ToChar(), currentFile.Strlen())};
    std::string newPath = oldPath.GetDriveAndDirectory();
    newPath.append(fileName.ToChar(), fileName.Strlen());
    newPath += oldPath.GetExtension();
    cRZString newName(newPath.c_str());
    cRZFile::Rename(currentFile, newName);                            // @ 0x100037844
    updateKeyList(chosen);
    return true;
}

// @ 0x100329a1e: hide the window, drop the notification target, release every row's view and empty the row list,
// release the grid and shut the window down. selectedView (+0x1a8) is only zeroed: the row's counted view owns the
// reference.
bool cSCResourceCatalogView::Shutdown() {
    HideWindow();                          // 0x230
    SetNotificationTarget(nullptr);        // 0x2b0
    // The original walks the rows from the end, releases each row's view and frees the name buffer; the element
    // destructors (cRZString and cRZAutoRefCount) do exactly that.
    types_.clear();
    selectedView_ = nullptr;               // no Release: the row's counted view owned the reference
    if (grid_) {
        grid_->Release();
        grid_ = nullptr;
    }
    return cGZWin::Shutdown();
}

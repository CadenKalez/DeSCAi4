#include "sc4/ViewInputControlSignTool.h"

#include <cstring>
#include <string>

#include "rz/CharLength.h"
#include "rz/TextEncoding.h"
#include "sc4/OccupantFilters.h"

namespace sc4 {

static ISignToolHost* g_signHost = nullptr;
ISignToolHost* SignToolHost() { return g_signHost; }
void SetSignToolHost(ISignToolHost* host) { g_signHost = host; }

namespace {
constexpr std::uint32_t kToolId = 0x8b4ce5a0;       // low half of 0x6b72fecf8b4ce5a0
constexpr std::uint32_t kToolCursorId = 0x6b72fecf; // high half (written into +0x14)
constexpr unsigned kMaxGlyphs = 0x21;

bool IsMultiByteCodePage(int codePage) { return codePage == 8 || codePage > 99; }

// cvttss2si to 64 bits followed by the original's unsigned fix-up for values >= 2^63.
std::uint64_t FloatToU64(float v) {
    const float limit = 9.223372e18f;
    if (!(limit > v)) {   // ucomiss + cmovbe: also taken for NaN
        auto big = static_cast<std::int64_t>(v - limit);
        return static_cast<std::uint64_t>(big) ^ 0x8000000000000000ull;
    }
    return static_cast<std::uint64_t>(static_cast<std::int64_t>(v));
}
}  // namespace

cSC4ViewInputControlSignTool::cSC4ViewInputControlSignTool() = default;

cSC4ViewInputControlSignTool::~cSC4ViewInputControlSignTool() {
    if (clipboard_) clipboard_->Release();
    if (selection_) selection_->Release();
}

bool cSC4ViewInputControlSignTool::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZMessageTargetLegacy) {
        *out = static_cast<cIGZMessageTarget*>(this);
        AddRef();
        return true;
    }
    if (iid == 1 || iid == kGZIID_cISC4ViewInputControl) {
        *out = static_cast<cSC4ViewInputControl*>(this);
        AddRef();
        return true;
    }
    return false;
}

bool cSC4ViewInputControlSignTool::Init() {
    if (!initialized_) {
        id_ = kToolId;
        cursorId_ = kToolCursorId;
        codePage_ = GetCurrentFrameworkCodePage();

        ISignToolHost* host = SignToolHost();
        cIGZClipboardManager* clipboard = host->ClipboardManager();
        if (clipboard_ != clipboard) {
            cIGZClipboardManager* old = clipboard_;
            if (clipboard) clipboard->AddRef();
            clipboard_ = clipboard;
            if (old) old->Release();
        }
        if (cIGZMessageServer* server = host->MessageServer())
            server->AddNotification(static_cast<cIGZMessageTarget*>(this), kSignToolMessage);
        host->SetPropDeveloperMode(1);
        cSC4ViewInputControl::Init();
    }
    return true;
}

bool cSC4ViewInputControlSignTool::Shutdown() {
    if (initialized_) {
        if (state_ == kEditing) {
            selection_->SetEditMode(false);
            selection_->SetDynamic(true);
        }
        SetSelection(nullptr);
        if (clipboard_) {
            cIGZClipboardManager* c = clipboard_;
            clipboard_ = nullptr;
            c->Release();
        }
        if (cIGZMessageServer* server = SignToolHost()->MessageServer())
            server->RemoveNotification(static_cast<cIGZMessageTarget*>(this), kSignToolMessage);
        cSC4ViewInputControl::Shutdown();
    }
    return true;
}

// A sign that is deselected keeps living only when it carries text; empty signs are removed from the prop manager.
void cSC4ViewInputControlSignTool::SetSelection(cISC4SignpostOccupant* sign) {
    if (selection_ == sign) return;
    if (selection_) {
        cRZString current;
        selection_->GetText(current);
        if (current.Strlen() != 0)
            selection_->SetDynamic(false);
        else
            SignToolHost()->RemoveProp(selection_->AsOccupant());
    }
    cISC4SignpostOccupant* old = selection_;
    if (old != sign) {
        if (sign) sign->AddRef();
        selection_ = sign;
        if (old) old->Release();
    }
    if (sign) sign->SetDynamic(true);
}

bool cSC4ViewInputControlSignTool::OnMouseDownL(int x, int y, std::uint32_t) {
    if (state_ == kEditing) {
        state_ = kIdle;
        selection_->SetEditMode(false);
        selection_->SetDynamic(true);
        return true;
    }
    ISignToolHost* host = SignToolHost();
    ISignToolRenderer* renderer = host->Renderer();
    if (!renderer) return true;

    cSC4SignpostFilter* filter = new cSC4SignpostFilter();
    filter->AddRef();
    IPickedObject* picked = nullptr;
    if (renderer->PickObject(x, y, filter, &picked)) {
        if (picked) {
            if (cIGZUnknown* owner = picked->GetOwner()) {
                cISC4SignpostOccupant* sign = nullptr;
                void* iface = nullptr;
                if (owner->QueryInterface(kGZIID_cISC4SignpostOccupant, &iface)) {
                    sign = static_cast<cISC4SignpostOccupant*>(iface);
                    // (the original compares against the current selection while editing here, which cannot happen:
                    // state 3 returned above)
                    state_ = kPressed;
                    SetSelection(sign);
                    float p[3] = {0, 0, 0};
                    sign->AsOccupant()->GetPosition(p);
                    host->ProjectToViewport(p);
                    dragOffsetX_ = static_cast<int>(p[0]) - x;
                    dragOffsetY_ = static_cast<int>(p[1]) - y;
                    host->PlaySound(kSignToolSoundGroup, kSignToolSoundGrab);
                }
                if (sign) sign->Release();
            }
            // Quirk kept: the original never releases the pick result on this path (only the filter), so the picked
            // object's reference is leaked on every successful pick (OnMouseMove does release it).
        }
        filter->Release();
        return true;
    }

    float p[3] = {0, 0, 0};
    if (!renderer->PickTerrain(x, y, p, false, true)) {
        filter->Release();
        return false;
    }
    cISC4SignpostOccupant* sign = host->CreateSignpost();
    sign->AddRef();
    host->AddProp(sign->AsOccupant());
    SetSelection(sign);
    const float altitude = host->TerrainAltitude(p[0], p[2]);
    const float sea = host->SeaLevel();
    p[1] = sea > altitude ? sea : altitude;
    sign->AsOccupant()->SetPosition(p);
    state_ = kEditing;
    text_.Resize(0);
    savedText_.Resize(0);
    sign->SetText(text_);
    sign->SetEditMode(true);
    host->PlaySound(kSignToolSoundGroup, kSignToolSoundPlace);
    sign->Release();
    filter->Release();
    return true;
}

bool cSC4ViewInputControlSignTool::OnMouseUpL(int, int, std::uint32_t) {
    bool startEditing = false;
    if (state_ == kDragging) {
        cRZString current;
        selection_->GetText(current);
        const std::uint32_t length = current.Strlen();
        SignToolHost()->PlaySound(kSignToolSoundGroup, kSignToolSoundPlace);
        startEditing = (length == 0);   // a dragged empty sign goes straight into text editing
    } else if (state_ == kPressed) {
        startEditing = true;            // a plain click on a sign edits it
    } else {
        // idle / editing: nothing to do, and the final "not editing -> idle" reset below still runs
        if (state_ != kEditing) state_ = kIdle;
        return true;
    }
    if (startEditing) {
        state_ = kEditing;
        selection_->GetText(text_);
        selection_->SetEditMode(true);
        savedText_.Copy(text_);   // std::string copy of the text (+0x68 -> +0x90)
    }
    if (state_ != kEditing) state_ = kIdle;
    return true;
}

bool cSC4ViewInputControlSignTool::OnMouseMove(int x, int y, std::uint32_t) {
    ISignToolHost* host = SignToolHost();
    if (state_ == kDragging || state_ == kPressed) {
        if (state_ == kPressed) {
            if (!selection_) return true;
            state_ = kDragging;
        }
        ISignToolRenderer* renderer = host->Renderer();
        if (!renderer) return true;
        float p[3] = {0, 0, 0};
        if (renderer->PickTerrain(dragOffsetX_ + x, dragOffsetY_ + y, p, false, true) && selection_)
            selection_->AsOccupant()->SetPosition(p);
    }
    if (state_ != kIdle) return true;

    // hover: select the sign under the cursor, deselect when there is none
    ISignToolRenderer* renderer = host->Renderer();
    cSC4SignpostFilter* filter = new cSC4SignpostFilter();
    filter->AddRef();
    IPickedObject* picked = nullptr;
    if (!renderer->PickObject(x, y, filter, &picked)) {
        SetSelection(nullptr);
    } else if (picked) {
        if (picked->GetOwner()) {
            cISC4SignpostOccupant* sign = nullptr;
            void* iface = nullptr;
            if (picked->GetOwner()->QueryInterface(kGZIID_cISC4SignpostOccupant, &iface)) {
                sign = static_cast<cISC4SignpostOccupant*>(iface);
                SetSelection(sign);
            }
            if (sign) sign->Release();
        }
        picked->Release();
    }
    filter->Release();
    return true;
}

bool cSC4ViewInputControlSignTool::DoMessage(cGZMessage& message) {
    if (message.type == kSignToolMessage && state_ == kEditing && selection_) {
        message.param1 = 1;
        float box[6] = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
        selection_->AsOccupant()->GetBoundingBox(&box[0], &box[3]);
        // the box centre in x and z, the top of the box in y
        float centreX = (box[0] + box[3]) * 0.5f;
        float centreZ = (box[2] + box[5]) * 0.5f;
        float point[3] = {centreX, box[4], centreZ};
        SignToolHost()->ProjectToViewport(point);
        message.param2 = FloatToU64(point[0]);
        message.param3 = FloatToU64(point[1]);
    }
    return false;
}

bool cSC4ViewInputControlSignTool::OnCharacter(char c) {
    if (state_ != kEditing) return false;
    const bool multiByte = IsMultiByteCodePage(codePage_);
    if (c == '\b') {
        int length = static_cast<int>(text_.Strlen());
        if (length > 0) {
            if (!multiByte) {
                length -= 1;
            } else {
                length -= static_cast<int>(GetPreviousCharLength(text_.Data() + length, static_cast<unsigned>(length), codePage_));
            }
            text_.Resize(static_cast<std::uint32_t>(length));
            selection_->SetText(text_);
        }
    } else if (c != 0x1b) {
        if (c == '\r') {
            selection_->SetEditMode(false);
            selection_->SetDynamic(true);
            SetSelection(nullptr);
            state_ = kIdle;
        } else if (static_cast<unsigned char>(c) >= 0x20) {
            if (!multiByte) {
                pendingChar_.AssignFromChars(&c, 1);
            } else {
                if (pendingChar_.Strlen() == 0 && IsLeadByte(c, codePage_)) {
                    pendingChar_.AssignFromChars(&c, 1);
                    return true;
                }
                pendingChar_.AppendChars(&c, 1);
                const std::size_t have = pendingChar_.Strlen();
                const int follow = GetFollowByteCount(pendingChar_.Data()[0], codePage_);
                if (have < static_cast<std::size_t>(static_cast<std::int64_t>(follow + 1))) return true;
            }
            AppendText(pendingChar_);
            pendingChar_.Resize(0);
        }
    }
    return true;
}

void cSC4ViewInputControlSignTool::AppendText(const cIGZString& text) {
    unsigned glyphs = GetGlyphCountFromString(text_.Data(), text_.Strlen(), codePage_);
    const std::uint32_t size = text.Strlen();
    const char* data = text.Data();
    if (size != 0 && glyphs < kMaxGlyphs) {
        std::uint32_t pos = 0;
        do {
            ++glyphs;
            const std::uint32_t length = static_cast<std::uint32_t>(GetCharLength(data + pos, size - pos, codePage_));
            if (data[pos] != '\n') text_.AppendChars(data + pos, length);
            pos += length;
        } while (pos < size && glyphs < kMaxGlyphs);
    }
    selection_->SetText(text_);
}

bool cSC4ViewInputControlSignTool::OnKeyDown(std::uint32_t key, std::uint32_t mods) {
    if (state_ == kEditing) {
        if (key == 0x1b) {
            if (selection_) {
                selection_->SetText(savedText_);
                text_.Copy(savedText_);
            }
            return false;
        }
        if (key == 0x56 && (mods & 7) == 2) {
            cRZString clip;
            if (clipboard_ && clipboard_->GetClipboardText(clip, 0)) AppendText(clip);
        }
        return true;
    }
    if (key == 0x2e && selection_) {
        cISC4Occupant* occupant = selection_->AsOccupant();
        if (occupant) occupant->AddRef();
        SetSelection(nullptr);
        SignToolHost()->RemoveProp(occupant);
        state_ = kIdle;
        if (occupant) occupant->Release();
        return true;
    }
    return false;
}

}  // namespace sc4

#include "sc4/PropertyView.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "gz/cGZDBSegmentDirectoryFiles.h"
#include "rz/RZConvert.h"
#include "rz/RZStringUtils.h"
#include "rz/cRZIniFile.h"

// cSCPropertyView — see PropertyView.h for the recovered layout, the quirks kept and the seams.
//
// The file also contains the twelve file-local helpers of the original translation unit (they sit between
// aliasesSectionCallback and propertiesSectionCallback in the binary):
//   getVariantType                    0x1002695a6
//   convertStringToBoolValue          0x100269cbf
//   getDefaultValue                   0x100269e4e
//   convertStringToUnsignedValue<T>   0x10026abb8 (uint8), 0x10026ad19 (uint16), 0x10026ae7d (uint32)
//   convertStringToSignedValue<T>     0x10026afde (sint8), 0x10026b13f (sint16), 0x10026b2a3 (sint32),
//                                     0x10026b404 (sint64)
//   convertStringToFloatValue<float>  0x10026b569
//   getUnquotedString                 0x10026b8d4
// They are hand-written here as plain static functions (the original has C++ linkage but no class scope).

namespace sc4 {
namespace {

PropertyViewInfoMap g_propertyInfos;               // 0x100b24798
PropertyViewAliasMap g_aliases;                    // 0x100b247b0
PropertyViewExemplarFilterMap g_exemplarFilters;   // 0x100b247c8
PropertyViewUint8MapTable g_uint8ValueMaps;        // 0x100b24768
PropertyViewUint32MapTable g_uint32ValueMaps;      // 0x100b24780
PropertyViewResKeyTableInfoMap g_resKeyTableDefaults;   // 0x100b247e0
bool g_iniProcessed = false;                       // 0x100b248?? (mbINIProcessed)
cRZString g_settingsIniFilePath;                   // 0x100b24800 (msSettingsIniFilePath)
IPropertyViewHost* g_host = nullptr;

bool IsDigitChar(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
bool IsAlphaChar(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool StartsWithDigit(const cRZString& text) {
    const char* p = text.ToChar();
    return p != nullptr && p[0] != '\0' && IsDigitChar(p[0]);
}

// cRZString::LTrim+RTrim on a copy.
cRZString TrimmedCopy(const cRZString& text) {
    cRZString copy = text;
    copy.LTrim();
    copy.RTrim();
    return copy;
}

// getVariantType @ 0x1002695a6: the [Properties] type name -> cIGZVariant::Type. The original trims and upper-cases the
// token first. UINT64 and CHAR have no name (they parse as 0 = unknown) - quirk kept.
std::uint32_t ParseVariantType(const cRZString& typeName) {
    cRZString name = TrimmedCopy(typeName);
    name.MakeUpper();
    const std::string text = name.str();
    if (text == "UINT8") return cIGZVariant::kTypeUint8;
    if (text == "UINT16") return cIGZVariant::kTypeUint16;
    if (text == "UINT32") return cIGZVariant::kTypeUint32;
    if (text == "SINT8") return cIGZVariant::kTypeSint8;
    if (text == "SINT16") return cIGZVariant::kTypeSint16;
    if (text == "SINT32") return cIGZVariant::kTypeSint32;
    if (text == "SINT64") return cIGZVariant::kTypeSint64;
    if (text == "STRING") return cIGZVariant::kTypeArrayFlag | cIGZVariant::kTypeRZChar;
    if (text == "FLOAT32") return cIGZVariant::kTypeFloat32;
    if (text == "BOOL") return cIGZVariant::kTypeBool;
    return cIGZVariant::kTypeNone;
}

// getUnquotedString @ 0x10026b8d4: reads one comma separated field from `source` (which it advances). A field that starts
// with '"' is returned without the quotes and the rest of that comma field is discarded; otherwise the text up to the
// next comma is returned. An empty result falls back to the whole remaining text (and a quoted field without a closing
// quote yields an empty string).
cRZString UnquoteToken(cRZString& source) {
    cRZString out;
    source.LTrim();
    if (source.Strlen() == 0) return out;
    if (source.ToChar()[0] == '"') {
        (void)source.SplitToken("\"");          // the (empty) text before the opening quote
        out = source.SplitToken("\"");          // the quoted content
        (void)source.SplitToken(",");           // drop the rest of the field up to the next comma
    } else {
        out = source.SplitToken(",");
        if (out.Strlen() == 0) out = source;
    }
    return out;
}

// convertStringToBoolValue's scalar rule (no upper-casing in the array helper).
bool ArrayBoolValue(const char* text) {
    if (text != nullptr && text[0] == 'T') return true;
    return RZ::ConvertToUint(text) != 0;
}

// getDefaultValue's scalar bool rule (the text is upper-cased first).
bool ScalarBoolValue(const char* text) {
    cRZString upper(text);
    upper.MakeUpper();
    if (upper.ToChar()[0] == 'T') return true;
    return RZ::ConvertToUint(upper.ToChar()) != 0;
}

// Splits on ',' exactly like convertStringTo*Value: count-1 fields, then the trimmed remainder as the last value.
template <typename T, typename Convert>
void ParseCommaArray(cRZString text, T* out, std::uint32_t count, Convert convert) {
    if (count == 0) return;
    for (std::uint32_t i = 0; i + 1 < count; ++i) {
        text.LTrim();
        const cRZString token = text.SplitToken(",");
        out[i] = static_cast<T>(RZ::ConvertToUint(token.ToChar()));
    }
    text.LTrim();
    text.RTrim();
    out[count - 1] = static_cast<T>(convert(text.ToChar()));
}

// getDefaultValue @ 0x100269e4e: parses `text` into `variant`. The element count is used as its absolute value (quirk);
// count 1 means a scalar, other counts an array, and the supported types are bool, the six fixed-size integers, sint64,
// float32 and STRING (UINT64 and CHAR have no case and fail).
bool ParseDefaultValue(cRZVariant& variant, std::uint32_t type, int elementCount, const cRZString& text) {
    const int count = elementCount < 0 ? -elementCount : elementCount;
    cRZString work = text;
    work.LTrim();
    if (type == (cIGZVariant::kTypeArrayFlag | cIGZVariant::kTypeRZChar)) {
        variant.SetValString(work);
        return true;
    }
    if (work.Strlen() == 0) return false;
    if (count <= 1) {
        const char* value = work.ToChar();
        switch (type) {
            case cIGZVariant::kTypeBool: variant.SetValBool(ScalarBoolValue(value)); return true;
            case cIGZVariant::kTypeUint8: variant.SetValUint8(static_cast<std::uint8_t>(RZ::ConvertToUint(value))); return true;
            case cIGZVariant::kTypeSint8: variant.SetValSint8(static_cast<std::int8_t>(std::atoi(value))); return true;
            case cIGZVariant::kTypeUint16: variant.SetValUint16(static_cast<std::uint16_t>(RZ::ConvertToUint(value))); return true;
            case cIGZVariant::kTypeSint16: variant.SetValSint16(static_cast<std::int16_t>(std::atoi(value))); return true;
            case cIGZVariant::kTypeUint32: variant.SetValUint32(RZ::ConvertToUint(value)); return true;
            case cIGZVariant::kTypeSint32: variant.SetValSint32(std::atoi(value)); return true;
            case cIGZVariant::kTypeSint64: variant.SetValSint64(static_cast<std::int64_t>(std::atoi(value))); return true;
            case cIGZVariant::kTypeFloat32: variant.SetValFloat32(static_cast<float>(RZAtofEnglishA(value))); return true;
            default: return false;
        }
    }
    switch (type) {
        case cIGZVariant::kTypeBool: {
            std::unique_ptr<bool[]> data(new bool[count]);
            ParseCommaArray<bool>(work, data.get(), static_cast<std::uint32_t>(count), ArrayBoolValue);
            variant.RefBool(data.get(), static_cast<std::uint32_t>(count));
            return true;
        }
        case cIGZVariant::kTypeUint8: {
            std::unique_ptr<std::uint8_t[]> data(new std::uint8_t[count]);
            ParseCommaArray<std::uint8_t>(work, data.get(), static_cast<std::uint32_t>(count), RZ::ConvertToUint);
            variant.RefUint8(data.get(), static_cast<std::uint32_t>(count));
            return true;
        }
        case cIGZVariant::kTypeSint8: {
            std::unique_ptr<std::int8_t[]> data(new std::int8_t[count]);
            ParseCommaArray<std::int8_t>(work, data.get(), static_cast<std::uint32_t>(count), [](const char* t){ return std::atoi(t); });
            variant.RefSint8(data.get(), static_cast<std::uint32_t>(count));
            return true;
        }
        case cIGZVariant::kTypeUint16: {
            std::unique_ptr<std::uint16_t[]> data(new std::uint16_t[count]);
            ParseCommaArray<std::uint16_t>(work, data.get(), static_cast<std::uint32_t>(count), RZ::ConvertToUint);
            variant.RefUint16(data.get(), static_cast<std::uint32_t>(count));
            return true;
        }
        case cIGZVariant::kTypeSint16: {
            std::unique_ptr<std::int16_t[]> data(new std::int16_t[count]);
            ParseCommaArray<std::int16_t>(work, data.get(), static_cast<std::uint32_t>(count), [](const char* t){ return std::atoi(t); });
            variant.RefSint16(data.get(), static_cast<std::uint32_t>(count));
            return true;
        }
        case cIGZVariant::kTypeUint32: {
            std::unique_ptr<std::uint32_t[]> data(new std::uint32_t[count]);
            ParseCommaArray<std::uint32_t>(work, data.get(), static_cast<std::uint32_t>(count), RZ::ConvertToUint);
            variant.RefUint32(data.get(), static_cast<std::uint32_t>(count));
            return true;
        }
        case cIGZVariant::kTypeSint32: {
            std::unique_ptr<std::int32_t[]> data(new std::int32_t[count]);
            ParseCommaArray<std::int32_t>(work, data.get(), static_cast<std::uint32_t>(count), [](const char* t){ return std::atoi(t); });
            variant.RefSint32(data.get(), static_cast<std::uint32_t>(count));
            return true;
        }
        case cIGZVariant::kTypeSint64: {
            std::unique_ptr<std::int64_t[]> data(new std::int64_t[count]);
            ParseCommaArray<std::int64_t>(work, data.get(), static_cast<std::uint32_t>(count), [](const char* t){ return std::atoi(t); });
            variant.RefSint64(data.get(), static_cast<std::uint32_t>(count));
            return true;
        }
        case cIGZVariant::kTypeFloat32: {
            std::unique_ptr<float[]> data(new float[count]);
            ParseCommaArray<float>(work, data.get(), static_cast<std::uint32_t>(count), RZAtofEnglishA);
            variant.RefFloat32(data.get(), static_cast<std::uint32_t>(count));
            return true;
        }
        default: return false;
    }
}

// A number token (first character a digit), otherwise an alias lookup; unknown aliases are 0.
std::uint32_t ResolveIdOrAlias(const cRZString& token) {
    if (StartsWithDigit(token)) return RZ::ConvertToUint(token.ToChar());
    const auto it = g_aliases.find(token);
    return it == g_aliases.end() ? 0 : it->second;
}

// The aux field read uses mAliases' operator[] in the original (it inserts a 0 entry); this keeps the value only.
std::uint32_t AliasValueOrZero(const cRZString& token) {
    const auto it = g_aliases.find(token);
    return it == g_aliases.end() ? 0 : it->second;
}

// One printf format slot of GetPropertyValueStr. `format` is the custom +0x70 format or null for the built-in one.
// The original reads scalars through As* and arrays through Ref* (the Ref accessors reinterpret the union, so they are
// only valid for array variants). `array` says which accessor family to use.
std::string FormatScalar(const cIGZVariant& value, std::uint32_t index, bool array, const char* format) {
    char buffer[512];
    switch (value.GetType() & ~cIGZVariant::kTypeArrayFlag) {
        case cIGZVariant::kTypeBool:
            return (array ? value.RefBool()[index] : value.AsBool()) ? "True" : "False";
        case cIGZVariant::kTypeUint8: {
            const unsigned element = array ? value.RefUint8()[index] : value.AsUint8();
            std::snprintf(buffer, sizeof buffer, format ? format : "0x%02X", element);
            return buffer;
        }
        case cIGZVariant::kTypeSint8: {
            const int element = array ? value.RefSint8()[index] : value.AsSint8();
            std::snprintf(buffer, sizeof buffer, format ? format : "%d", element);
            return buffer;
        }
        case cIGZVariant::kTypeUint16: {
            const unsigned element = array ? value.RefUint16()[index] : value.AsUint16();
            std::snprintf(buffer, sizeof buffer, format ? format : "0x%04X", element);
            return buffer;
        }
        case cIGZVariant::kTypeSint16: {
            const int element = array ? value.RefSint16()[index] : value.AsSint16();
            std::snprintf(buffer, sizeof buffer, format ? format : "%d", element);
            return buffer;
        }
        case cIGZVariant::kTypeUint32: {
            const unsigned element = array ? value.RefUint32()[index] : value.AsUint32();
            std::snprintf(buffer, sizeof buffer, format ? format : "0x%08X", element);
            return buffer;
        }
        case cIGZVariant::kTypeSint32: {
            const int element = array ? value.RefSint32()[index] : value.AsSint32();
            std::snprintf(buffer, sizeof buffer, format ? format : "%d", element);
            return buffer;
        }
        case cIGZVariant::kTypeSint64: {
            // quirk kept: the original passes the 64-bit value to "%d", so only the low 32 bits are printed
            const int element = static_cast<int>(array ? value.RefSint64()[index] : value.AsSint64());
            std::snprintf(buffer, sizeof buffer, format ? format : "%d", element);
            return buffer;
        }
        case cIGZVariant::kTypeFloat32: {
            const double element = array ? value.RefFloat32()[index] : value.AsFloat32();
            std::snprintf(buffer, sizeof buffer, format ? format : "%f", element);
            return buffer;
        }
        case cIGZVariant::kTypeChar:
            // the original reads this slot through AsRZChar
            std::snprintf(buffer, sizeof buffer, format ? format : "%c",
                          static_cast<int>(array ? value.RefRZChar()[index] : value.AsRZChar()));
            return buffer;
        case cIGZVariant::kTypeUnknown:
            std::snprintf(buffer, sizeof buffer, "Unk. ptr: %0x08X",
                          static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(value.RefIGZUnknown())));
            return buffer;
        default:
            return "???";
    }
}

// The scalar/array formatter behind GetPropertyValueStr when no custom display class handled the value.
void FormatVariant(const cIGZVariant& value, const char* format, cRZString& out) {
    const std::uint32_t type = value.GetType();
    if (type == (cIGZVariant::kTypeArrayFlag | cIGZVariant::kTypeRZChar)) {
        cRZString text;
        value.GetValString(text);
        if (format) {
            char buffer[512];
            std::snprintf(buffer, sizeof buffer, format, text.ToChar());
            out = buffer;
        } else {
            out = text;
        }
        return;
    }
    const bool array = (type & cIGZVariant::kTypeArrayFlag) != 0;
    std::uint32_t count = value.GetCount();
    if (!array && count == 0) count = 1;
    const std::uint32_t element = type & ~cIGZVariant::kTypeArrayFlag;
    if (element == cIGZVariant::kTypeUint64 || element == cIGZVariant::kTypeRZUnicodeChar ||
        element == cIGZVariant::kTypeVoidPtr || (array && element == cIGZVariant::kTypeChar)) {
        out = "???";
        return;
    }
    if (count == 0) {
        out = "???";
        return;
    }
    std::string text = FormatScalar(value, 0, array, format);
    for (std::uint32_t i = 1; i < count; ++i) {
        text += ", ";
        text += FormatScalar(value, i, array, format);
    }
    out = cRZString(text.c_str());
}

// getFilenameFromKey's right-hand cut: at most the last (length - 9) characters of a text file name (quirk kept).
cRZString FilenameTail(const cRZString& name) {
    const std::int32_t length = static_cast<std::int32_t>(name.Strlen());
    if (length > 0 && length >= 10 && IsAlphaChar(name.ToChar()[0])) {
        return name.Right(static_cast<std::size_t>(length - 9));
    }
    return name;
}

}  // namespace

IPropertyViewHost* PropertyViewHost() { return g_host; }
void SetPropertyViewHost(IPropertyViewHost* host) { g_host = host; }

const PropertyViewInfoMap& cSCPropertyView::PropertyInfos() { return g_propertyInfos; }
const PropertyViewAliasMap& cSCPropertyView::Aliases() { return g_aliases; }
const PropertyViewExemplarFilterMap& cSCPropertyView::ExemplarFilters() { return g_exemplarFilters; }
const PropertyViewUint8MapTable& cSCPropertyView::Uint8ValueMaps() { return g_uint8ValueMaps; }
const PropertyViewUint32MapTable& cSCPropertyView::Uint32ValueMaps() { return g_uint32ValueMaps; }
const PropertyViewResKeyTableInfoMap& cSCPropertyView::ResKeyTableDefaults() { return g_resKeyTableDefaults; }
bool cSCPropertyView::INIProcessed() { return g_iniProcessed; }
void cSCPropertyView::SetINIProcessed(bool processed) { g_iniProcessed = processed; }
void cSCPropertyView::SetSettingsIniFilePath(const cRZString& path) { g_settingsIniFilePath = path; }
const cRZString& cSCPropertyView::SettingsIniFilePath() { return g_settingsIniFilePath; }

void cSCPropertyView::ResetStaticsForTest() {
    g_propertyInfos.clear();
    g_aliases.clear();
    g_exemplarFilters.clear();
    g_uint8ValueMaps.clear();
    g_uint32ValueMaps.clear();
    g_resKeyTableDefaults.clear();
    g_iniProcessed = false;
    g_settingsIniFilePath = cRZString();
}

// cSCPropertyView::cSCPropertyView(cISCProperty&, unsigned int) @ 0x1002679c6
cSCPropertyView::cSCPropertyView(cISCProperty& property, std::uint32_t exemplarFilter)
    : cGZWin(g_host ? g_host->WindowManager() : nullptr), property_(&property) {
    filter_ = 0xffffffff;
    // the original lower_bound + "key <= argument" test is an exact lookup
    const auto it = g_exemplarFilters.find(exemplarFilter);
    if (it != g_exemplarFilters.end()) filter_ = it->second;
}

// cSCPropertyView::~cSCPropertyView() @ 0x100267b18 (+ thunk 0x100267bcc, deleting variant 0x100267bd6)
cSCPropertyView::~cSCPropertyView() { Shutdown(); }

// cSCPropertyView::QueryInterface(unsigned int, void**) @ 0x100267c04
bool cSCPropertyView::QueryInterface(std::uint32_t iid, void** out) { return cGZWin::QueryInterface(iid, out); }

// cSCPropertyView::AddRef() @ 0x100267c0e
std::uint32_t cSCPropertyView::AddRef() { return cGZWin::AddRef(); }

// cSCPropertyView::Release() @ 0x100267c18
std::uint32_t cSCPropertyView::Release() { return cGZWin::Release(); }

// cSCPropertyView::DoMessage(cGZMessage&) @ 0x10026945a
bool cSCPropertyView::DoMessage(cGZMessage& message) { return cGZWin::DoMessage(message); }

// cSCPropertyView::GZOnKeyDown(unsigned int, unsigned int) @ 0x100269464
bool cSCPropertyView::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {
    SendMsg(notify_, kGZMsgKeyDown, key, modifiers, 0);
    return false;
}

// cSCPropertyView::GZOnKeyUp(unsigned int, unsigned int) @ 0x10026948a
bool cSCPropertyView::GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) {
    SendMsg(notify_, kGZMsgKeyUp, key, modifiers, 0);
    return false;
}

// cSCPropertyView::GZOnMouseDownL(int, int, unsigned int) @ 0x1002694b0
bool cSCPropertyView::GZOnMouseDownL(int, int, std::uint32_t) { return true; }

// cSCPropertyView::Shutdown() @ 0x1002693e4
bool cSCPropertyView::Shutdown() {
    HideWindow();
    SetNotificationTarget(nullptr);
    views_.clear();
    currentView_ = nullptr;
    propertyIds_.clear();
    return cGZWin::Shutdown();
}

// cSCPropertyView::GZPaint() @ 0x10026f20e
// Port note: the original dereferences the draw buffer unconditionally; this guards a null one (the window can be asked
// to paint before it has a buffer).
bool cSCPropertyView::GZPaint() {
    if (drawBuffer_ && drawBuffer_->Lock(0x8001)) {
        drawBuffer_->Fill(GetFillColor(), drawArea_);
        drawBuffer_->Unlock(0x8001);
    }
    return cGZWin::GZPaint();
}

// cSCPropertyView::createButton(cIGZWinBtn*&, unsigned int, unsigned int, unsigned int, cRZString const&) @ 0x100268bba
void cSCPropertyView::createButton(gz::cIGZWinBtn*& out, std::uint32_t bitmapInstance, std::uint32_t bitmapGroup,
                                   std::uint32_t buttonId, const cRZString& caption) {
    out = nullptr;
    if (!g_host) return;
    cIGZBuffer* image = g_host->LoadBitmap(bitmapGroup, bitmapInstance);
    gz::cIGZWinBtn* button = g_host->CreateButton(buttonId, caption, image);
    if (image) image->Release();
    if (!button) return;
    out = button;
    button->FitSizeToContent();
    button->SetBtnFlag(gz::kBtnTipEnabled, true);
    button->SetBtnFlag(0x200, false);
    button->SetBtnFlag(0x400, false);
    button->SetTipText(caption);
}

// cSCPropertyView::createLabel(cIGZWinText*&, cRZString const&, unsigned int, cIGZFont*) @ 0x100268d64
void cSCPropertyView::createLabel(gz::cIGZWinText*& out, const cRZString& caption, std::uint32_t textColor,
                                  gz::IWinFont* font) {
    out = nullptr;
    if (!g_host) return;
    auto* label = static_cast<gz::cIGZWinText*>(g_host->CreateObject(kPropertyViewTextClass, gz::kGZIID_cIGZWinText));
    if (!label) return;
    out = label;
    cIGZWin* window = label->AsIGZWin();
    const std::uint32_t fill = window->MakeFillColor(0xe0, 0xe0, 0xe0);
    label->SetWinTextFlag(gz::kTextWrap, false);
    label->SetWinTextFlag(gz::kTextOpaque, false);
    if (!font) font = g_host->LabelFont(kPropertyViewDefaultLabelFont);
    label->SetFont(font);
    label->SetTextColor(textColor);
    window->SetFillColor(fill);
    window->SetCaption(caption);
    label->FitWindowToText(false, false);   // inferred from the unrecovered jumptable at 0x100268e9d
    window->SetFlag(kWinPrivateBuffer, false);
}

// cSCPropertyView::mapPropertiesToViews(int, int, int, int) @ 0x100268ea0
void cSCPropertyView::mapPropertiesToViews(int x, int y, int w, int h) {
    views_.clear();
    currentView_ = nullptr;
    for (const auto& entry : g_propertyInfos) {
        const PropertyViewInfo& info = entry.second;
        if ((filter_ & info.filter) == 0) continue;
        cISCValueEditView* editor = nullptr;
        if (!GetValueEditor(static_cast<std::uint32_t>(entry.first), nullptr, kPropertyViewValueEditorIID,
                            reinterpret_cast<void**>(&editor))) {
            continue;
        }
        views_[static_cast<std::uint32_t>(entry.first)] = editor;   // AddRefs
        editor->Release();
        cIGZWin* window = editor->AsIGZWin();
        window->SetArea(x, y, w, h);
        window->ShowWindow();
    }
}

// cSCPropertyView::Init() @ 0x100267c22
bool cSCPropertyView::Init() {
    if (get_init_flag()) return true;
    if (!g_iniProcessed) processINI();

    const std::uint32_t black = MakeFillColor(0, 0, 0);
    const std::uint32_t gray = MakeFillColor(0xe0, 0xe0, 0xe0);
    const std::uint32_t darkBlue = MakeFillColor(0, 0, 0x80);
    const std::uint32_t white = MakeFillColor(0xff, 0xff, 0xff);

    if (GetW() == 0 || GetH() == 0) {
        SetArea(0, 0, static_cast<int>(kPropertyViewDefaultWindowW), static_cast<int>(kPropertyViewDefaultWindowH));
        cIGZWin* parent = GetParentWin();
        if (parent) {
            GZWinMoveTo((parent->GetW() - GetW()) / 2, (parent->GetH() - GetH()) / 2);
        }
    }
    SetFlag(kWinFlag800, true);
    SetFlag(kWinPrivateBuffer, true);

    if (g_host) {
        // background: a light grey flat rect with a black outline on all four edges
        auto* background = static_cast<gz::cIGZWinFlatRect*>(
            g_host->CreateObject(kPropertyViewFlatRectClass, gz::kGZIID_cIGZWinFlatRect));
        if (background) {
            cIGZWin* window = background->AsIGZWin();
            window->SetArea(0, 0, GetW(), GetH());
            window->SetFillColor(gray);
            window->SetFlag(kWinFlag800, false);
            background->SetOutlineColor(black);
            background->SetSizeMode(0);
            background->SetOutlineFlags(gz::kOutlineLeft | gz::kOutlineTop | gz::kOutlineRight | gz::kOutlineBottom);
            ChildAdd(window);
            window->Release();
        }

        gz::cIGZWinBtn* done = nullptr;
        createButton(done, kPropertyViewDoneBitmapInstance, kPropertyViewDoneBitmapGroup, kPropertyViewDoneId, "Done");
        int buttonY = 0;
        int doneX = 0;
        if (done) {
            cIGZWin* window = done->AsIGZWin();
            const int buttonW = window->GetW();
            const int buttonH = window->GetH();
            ChildAdd(window);
            doneX = (GetW() - 5) - buttonW;
            buttonY = (GetH() - 5) - buttonH;
            window->GZWinMoveTo(doneX, buttonY);
            window->Release();
        }

        gz::cIGZWinBtn* cancel = nullptr;
        createButton(cancel, kPropertyViewCancelBitmapInstance, kPropertyViewDoneBitmapGroup, kPropertyViewCancelId,
                     "Cancel");
        int cancelX = doneX;
        if (cancel) {
            cIGZWin* window = cancel->AsIGZWin();
            cancelX = (doneX - 5) - window->GetW();
            ChildAdd(window);
            window->GZWinMoveTo(cancelX, buttonY);
            window->Release();
        }

        gz::cIGZWinText* help = nullptr;
        createLabel(help, "<help>", black, nullptr);
        if (help) {
            cIGZWin* window = help->AsIGZWin();
            window->SetArea(5, buttonY, cancelX, GetH() - 5);
            window->SetID(kPropertyViewHelpLabelId);
            ChildAdd(window);
            window->Release();
        }

        gz::cIGZWinText* propertyLabel = nullptr;
        createLabel(propertyLabel, "Property ID", black, nullptr);
        if (propertyLabel) {
            cIGZWin* window = propertyLabel->AsIGZWin();
            ChildAdd(window);
            const int labelW = window->GetW();
            window->GZWinMoveTo((GetW() * 2) / 5 - labelW, 10);
            window->Release();
        }
    }

    mapPropertiesToViews(5, 0x21, GetW() - 5, GetH() - 0x2d);

    if (g_host) {
        auto* combo = static_cast<gz::cIGZWinCombo*>(g_host->CreateObject(kPropertyViewComboClass, gz::kGZIID_cIGZWinCombo));
        if (combo) {
            cIGZWin* window = combo->AsIGZWin();
            window->SetID(kPropertyViewComboId);
            window->SetArea(5, 10, GetW() - 5, 0x1c);
            combo->SetEditable(false);
            for (const auto& entry : g_propertyInfos) {
                const cRZString& name = entry.second.name;
                int row = -2;   // -2 = append (cGZWinCombo::InsertString's append index)
                for (std::uint32_t i = 0; i < propertyIds_.size(); ++i) {
                    cRZString existing;
                    if (combo->GetStringFromIndex(static_cast<int>(i), existing) &&
                        existing.CompareToString(name, true) == 0) {
                        row = static_cast<int>(i);
                        break;
                    }
                }
                combo->InsertString(name, row);
                if (row < 0) {
                    propertyIds_.push_back(static_cast<std::uint32_t>(entry.first));
                } else {
                    propertyIds_.insert(propertyIds_.begin() + row, static_cast<std::uint32_t>(entry.first));
                }
            }
            window->SetFillColor(gray);
            combo->SetHighlightColor(darkBlue);
            ChildAdd(window);
            combo->SetFontColor(gz::cIGZWinCombo::kColorText, black);
            combo->SetFontColor(gz::cIGZWinCombo::kColorSelected, white);
            combo->SetSelection(0, true);
            window->Release();
        }
    }

    return cGZWin::Init();
}

// cSCPropertyView::processINI() @ 0x1002686a2
void cSCPropertyView::processINI() {
    cRZIniFile ini;
    if (g_settingsIniFilePath.Strlen() == 0) {
        cRZString directory;
        if (g_host) g_host->DataDir(directory);
        directory.Strcat("Sys/Ingred.ini");
        ini.SetPath(directory);
    } else {
        ini.SetPath(g_settingsIniFilePath);
    }

    // quirk kept: the exemplar filters and the res-key-table defaults are not cleared here
    g_propertyInfos.clear();
    g_aliases.clear();
    g_uint8ValueMaps.clear();
    g_uint32ValueMaps.clear();

    const auto readSection = [&ini](const char* name, cRZIniFile::EnumCallback callback, void* context) {
        const cRZString section(name);
        if (ini.SectionExists(section)) ini.EnumEntries(section, callback, context);
    };
    readSection("Aliases", &cSCPropertyView::aliasesSectionCallback, nullptr);
    readSection("ExemplarFilters", &cSCPropertyView::exemplarFiltersSectionCallback, nullptr);
    readSection("Properties", &cSCPropertyView::propertiesSectionCallback, nullptr);
    readSection("ResKeyTableEditorDefaults", &cSCPropertyView::ResKeyTableEditorDefaultSectionCallback, nullptr);

    for (const auto& entry : g_propertyInfos) {
        if (entry.second.displayClassId == kPropertyViewDisplayUint8Map) {
            cRZString name;
            name.Sprintf("Uint8ValueMap_0x%016X", static_cast<unsigned>(entry.first));
            if (ini.SectionExists(name) && g_uint8ValueMaps.find(entry.first) == g_uint8ValueMaps.end()) {
                ini.EnumEntries(name, &cSCPropertyView::Uint8ValueMapSectionCallback,
                                reinterpret_cast<void*>(static_cast<std::uintptr_t>(entry.first)));
            }
        }
        if (entry.second.displayClassId == kPropertyViewDisplayUint32Map) {
            cRZString name;
            name.Sprintf("Uint32ValueMap_0x%016X", static_cast<unsigned>(entry.first));
            if (ini.SectionExists(name) && g_uint32ValueMaps.find(entry.first) == g_uint32ValueMaps.end()) {
                ini.EnumEntries(name, &cSCPropertyView::Uint32ValueMapSectionCallback,
                                reinterpret_cast<void*>(static_cast<std::uintptr_t>(entry.first)));
            }
        }
    }
    g_iniProcessed = true;
}

// cSCPropertyView::aliasesSectionCallback(cRZString const&, cRZString const&, void*) @ 0x1002694b8
void cSCPropertyView::aliasesSectionCallback(const cRZString& key, const cRZString& value, void* context) {
    (void)context;
    cRZString name = key;
    name.LTrim();
    name.RTrim();
    g_aliases[name] = RZ::ConvertToUint(value.ToChar());
}

// cSCPropertyView::convertStringToFilter(cRZString&) @ 0x10026b6d6
std::uint32_t cSCPropertyView::convertStringToFilter(cRZString& text) {
    if (StartsWithDigit(text)) return RZ::ConvertToUint(text.ToChar());
    const auto it = g_aliases.find(text);
    return it == g_aliases.end() ? 0 : it->second;
}

// cSCPropertyView::getFilter(cRZString&) @ 0x10026b746 — space separated tokens, OR'ed.
std::uint32_t cSCPropertyView::getFilter(cRZString& text) {
    std::uint32_t result = 0;
    text.LTrim();
    cRZString token = text.SplitToken(" ");
    while (token.Strlen() != 0) {
        result |= convertStringToFilter(token);
        text.LTrim();
        token = text.SplitToken(" ");
    }
    result |= convertStringToFilter(text);
    return result;
}

// cSCPropertyView::propertiesSectionCallback(cRZString const&, cRZString const&, void*) @ 0x10026bb10
void cSCPropertyView::propertiesSectionCallback(const cRZString& key, const cRZString& value, void* context) {
    (void)context;
    PropertyViewInfo info;
    info.propertyId = RZ::ConvertToUint(key.ToChar());

    cRZString source = value;
    cRZString token = source.SplitToken(",");
    info.variantType = ParseVariantType(token);
    if (info.variantType == cIGZVariant::kTypeNone) return;

    token = source.SplitToken(",");
    info.elementCount = RZ::ConvertToUint(token.ToChar());
    if (info.elementCount != 1 &&
        info.variantType == (cIGZVariant::kTypeArrayFlag | cIGZVariant::kTypeRZChar)) {
        return;
    }

    token = UnquoteToken(source);
    if (info.elementCount == 0) {
        const std::uint32_t zero = 0;
        info.defaultValue.RefUint32(&zero, 1);
    } else if (!ParseDefaultValue(info.defaultValue, info.variantType, static_cast<int>(info.elementCount), token)) {
        return;
    }

    token = source.SplitToken(",");
    info.filter = getFilter(token);

    info.name = UnquoteToken(source);
    info.helpText = UnquoteToken(source);

    source.LTrim();
    token = source.SplitToken(",");
    info.editorClassId = ResolveIdOrAlias(token);

    source.LTrim();
    if (source.Strlen() != 0 && source.ToChar()[0] == '"') {
        info.defaultText = UnquoteToken(source);
        info.displayClassId = kPropertyViewDisplayFormat;
    } else {
        source.LTrim();
        token = source.SplitToken(",");
        if (StartsWithDigit(token)) {
            info.displayClassId = RZ::ConvertToUint(token.ToChar());
        } else {
            const auto alias = g_aliases.find(token);
            if (alias != g_aliases.end()) {
                info.displayClassId = alias->second;
                info.valueMapOwnerId = info.propertyId;
            } else {
                // "<number>_<alias>" fallback of the original: split the token on '_' and look the first part up
                cRZString prefix = token;
                const cRZString first = prefix.SplitToken("_");
                const auto prefixed = g_aliases.find(first);
                if (prefixed != g_aliases.end()) {
                    info.displayClassId = prefixed->second;
                    info.valueMapOwnerId = RZ::ConvertToUint(token.ToChar());
                }
            }
        }
    }

    source.LTrim();
    source.RTrim();
    if (StartsWithDigit(source)) {
        info.auxValue = RZ::ConvertToUint(source.ToChar());
    } else {
        info.auxValue = AliasValueOrZero(token);
    }

    g_propertyInfos[info.propertyId] = info;
}

// cSCPropertyView::exemplarFiltersSectionCallback(cRZString const&, cRZString const&, void*) @ 0x10026c286
void cSCPropertyView::exemplarFiltersSectionCallback(const cRZString& key, const cRZString& value, void* context) {
    (void)context;
    cRZString filterText = value;
    g_exemplarFilters[RZ::ConvertToUint(key.ToChar())] = getFilter(filterText);
}

// cSCPropertyView::Uint8ValueMapSectionCallback(cRZString const&, cRZString const&, void*) @ 0x10026c346
void cSCPropertyView::Uint8ValueMapSectionCallback(const cRZString& key, const cRZString& value, void* context) {
    std::uint32_t index = RZ::ConvertToUint(key.ToChar());
    if (index > 0xff) index = 0;   // quirk kept
    const std::uint64_t owner = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(context));
    cRZString text = value;
    g_uint8ValueMaps[owner][static_cast<std::uint8_t>(index)] = UnquoteToken(text);
}

// cSCPropertyView::Uint32ValueMapSectionCallback(cRZString const&, cRZString const&, void*) @ 0x10026c690
void cSCPropertyView::Uint32ValueMapSectionCallback(const cRZString& key, const cRZString& value, void* context) {
    const std::uint32_t index = RZ::ConvertToUint(key.ToChar());
    const std::uint64_t owner = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(context));
    cRZString text = value;
    g_uint32ValueMaps[owner][index] = UnquoteToken(text);
}

// cSCPropertyView::ResKeyTableEditorDefaultSectionCallback(cRZString const&, cRZString const&, void*) @ 0x10026c9ea
void cSCPropertyView::ResKeyTableEditorDefaultSectionCallback(const cRZString& key, const cRZString& value,
                                                              void* context) {
    (void)context;
    const std::uint32_t propertyId = RZ::ConvertToUint(key.ToChar());
    cRZVariant variant;
    if (!ParseDefaultValue(variant, cIGZVariant::kTypeUint32, 4, value)) return;
    const std::uint32_t* data = variant.RefUint32();
    if (!data) return;
    PropertyViewResKeyTableInfo info;
    // the original stores {0, 2, 1, 3} of the parsed array (group/type/instance swap, kept)
    info.values[0] = data[0];
    info.values[1] = data[2];
    info.values[2] = data[1];
    info.values[3] = data[3];
    g_resKeyTableDefaults[propertyId] = info;
}

// cSCPropertyView::GetPropertyNameStr(unsigned int, cRZString&) @ 0x10026cea8
void cSCPropertyView::GetPropertyNameStr(std::uint32_t propertyId, cRZString& out) {
    const auto it = g_propertyInfos.find(propertyId);
    if (it != g_propertyInfos.end()) {
        out = it->second.name;
    } else {
        out.Sprintf("0x%08X", propertyId);
    }
}

// cSCPropertyView::GetHelpText(unsigned int, cRZString&) @ 0x10026ed32
bool cSCPropertyView::GetHelpText(std::uint32_t propertyId, cRZString& out) {
    const auto it = g_propertyInfos.find(propertyId);
    if (it != g_propertyInfos.end()) {
        out = it->second.helpText;
        return true;
    }
    out.AssignFromCString("");
    return false;
}

// cSCPropertyView::IsVariableLengthArray(unsigned int) @ 0x10026eda4
bool cSCPropertyView::IsVariableLengthArray(std::uint32_t propertyId) {
    const auto it = g_propertyInfos.find(propertyId);
    if (it == g_propertyInfos.end()) return false;
    return it->second.editorClassId != kPropertyViewNewValueClass && it->second.elementCount < 1;
}

// cSCPropertyView::GetPropertyValueMap(unsigned int, std::map<unsigned char, cRZString>&) @ 0x10026e97a
bool cSCPropertyView::GetPropertyValueMap(std::uint32_t propertyId, std::map<std::uint8_t, cRZString>& out) {
    std::uint64_t owner = propertyId;
    const auto info = g_propertyInfos.find(propertyId);
    if (info != g_propertyInfos.end()) owner = info->second.valueMapOwnerId;
    const auto it = g_uint8ValueMaps.find(owner);
    if (it == g_uint8ValueMaps.end()) return false;
    if (&it->second == &out) return true;
    out = it->second;
    return true;
}

// cSCPropertyView::GetPropertyValueMap(unsigned int, std::map<unsigned int, cRZString>&) @ 0x10026eb56
bool cSCPropertyView::GetPropertyValueMap(std::uint32_t propertyId, std::map<std::uint32_t, cRZString>& out) {
    std::uint64_t owner = propertyId;
    const auto info = g_propertyInfos.find(propertyId);
    if (info != g_propertyInfos.end()) owner = info->second.valueMapOwnerId;
    const auto it = g_uint32ValueMaps.find(owner);
    if (it == g_uint32ValueMaps.end()) return false;
    if (&it->second == &out) return true;
    out = it->second;
    return true;
}

// cSCPropertyView::getResource(cIGZVariant const&, unsigned int, void**) @ 0x10026ee04
// The variant holds {group, type, instance}; the resource key's first two fields are swapped on the way in (quirk kept).
bool cSCPropertyView::getResource(const cIGZVariant& value, std::uint32_t iid, cIGZUnknown** out) {
    *out = nullptr;
    if (!g_host) return false;
    const std::uint32_t* data = value.RefUint32();
    if (!data) return false;
    const cGZPersistResourceKey key{data[1], data[0], data[2]};
    *out = g_host->GetResource(key, iid);
    return *out != nullptr;
}

// cSCPropertyView::getStringResource(cRZString&, cIGZVariant const&) @ 0x10026e5a4
void cSCPropertyView::getStringResource(cRZString& out, const cIGZVariant& value) {
    cIGZUnknown* resource = nullptr;
    if (!getResource(value, kPropertyViewStringResIID, &resource)) {
        const std::uint32_t* data = value.RefUint32();
        out.Sprintf("Invalid String Res key: G:%08X T:%08X I:%08X", data[0], data[1], data[2]);
        return;
    }
    auto* text = static_cast<cIGZString*>(resource);
    out.AssignFromIGZString(*text);
    text->Release();
}

// cSCPropertyView::getDebugNameResource(cRZString&, cIGZVariant const&) @ 0x10026e64e
void cSCPropertyView::getDebugNameResource(cRZString& out, const cIGZVariant& value) {
    cIGZUnknown* resource = nullptr;
    if (!getResource(value, kPropertyViewExemplarIID, &resource)) {
        const std::uint32_t* data = value.RefUint32();
        out.Sprintf("Invalid Exemplar Res Key: G:%08X T:%08X I:%08X", data[0], data[1], data[2]);
        return;
    }
    cISCPropertyHolder* holder = g_host ? g_host->ExemplarProperties(resource) : nullptr;
    const bool found = holder && cSCPropertyHelp::GetPropertyValue(holder, kPropertyViewDebugNameProperty, out);
    if (!found) {
        const std::uint32_t* data = value.RefUint32();
        out.Sprintf("Missing Debug Name Exemplar Res Key: G:%08X T:%08X I:%08X", data[0], data[1], data[2]);
    }
    resource->Release();
}

// cSCPropertyView::getFilenameFromKey(cRZString&, cIGZVariant const&) @ 0x10026e754
void cSCPropertyView::getFilenameFromKey(cRZString& out, const cIGZVariant& value) {
    const std::uint32_t* data = value.RefUint32();
    if (!data || !g_host) return;
    const cGZPersistResourceKey key{data[1], data[0], data[2]};
    cIGZPersistDBSegmentDirectoryFiles* directory = g_host->FindDirectorySegment(data[0]);
    if (!directory) return;
    cRZString name;
    if (!directory->GetFileNameFromKey(key, name)) {
        out.Sprintf("Missing Res Key: G:%08X T:%08X I:%08X", data[0], data[1], data[2]);
        directory->Release();
        return;
    }
    directory->Release();
    out = FilenameTail(name);
}

// cSCPropertyView::GetPropertyValueStr(unsigned int, cIGZVariant const&, cRZString&) @ 0x10026cf26
void cSCPropertyView::GetPropertyValueStr(std::uint32_t propertyId, const cIGZVariant& value, cRZString& out) {
    const auto found = g_propertyInfos.find(propertyId);
    const PropertyViewInfo* info = found == g_propertyInfos.end() ? nullptr : &found->second;

    if (info) {
        switch (info->displayClassId) {
            case kPropertyViewDisplayFormat:
                // the +0x70 text is a printf format for the value
                FormatVariant(value, info->defaultText.ToChar(), out);
                return;
            case kPropertyViewDisplayUint8Map: {
                const auto map = g_uint8ValueMaps.find(info->valueMapOwnerId);
                if (value.GetType() == cIGZVariant::kTypeUint8) {
                    if (map != g_uint8ValueMaps.end()) {
                        const std::uint8_t scalar = value.AsUint8();
                        const auto entry = map->second.find(scalar);
                        if (entry != map->second.end()) {
                            out = entry->second;
                            return;
                        }
                    }
                    out = "???";
                    return;
                }
                const std::uint32_t count = value.GetCount();
                const std::uint8_t* data = value.RefUint8();
                if (map == g_uint8ValueMaps.end() || !data || count == 0) {
                    out = "???";
                    return;
                }
                std::string text;
                for (std::uint32_t i = 0; i < count; ++i) {
                    if (i) text += ", ";
                    const auto entry = map->second.find(data[i]);
                    text += entry == map->second.end() ? std::string("???") : entry->second.str();
                }
                out = cRZString(text.c_str());
                return;
            }
            case kPropertyViewDisplayUint32Map: {
                const auto map = g_uint32ValueMaps.find(info->valueMapOwnerId);
                if (value.GetType() == cIGZVariant::kTypeUint32) {
                    if (map != g_uint32ValueMaps.end()) {
                        const std::uint32_t scalar = value.AsUint32();
                        const auto entry = map->second.find(scalar);
                        if (entry != map->second.end()) {
                            out = entry->second;
                            return;
                        }
                    }
                    out = "???";
                    return;
                }
                const std::uint32_t count = value.GetCount();
                const std::uint32_t* data = value.RefUint32();
                if (map == g_uint32ValueMaps.end() || !data || count == 0) {
                    out = "???";
                    return;
                }
                std::string text;
                for (std::uint32_t i = 0; i < count; ++i) {
                    if (i) text += ", ";
                    const auto entry = map->second.find(data[i]);
                    text += entry == map->second.end() ? std::string("???") : entry->second.str();
                }
                out = cRZString(text.c_str());
                return;
            }
            case kPropertyViewDisplayStringRes:
                getStringResource(out, value);
                return;
            case kPropertyViewDisplayDebugName:
                getDebugNameResource(out, value);
                return;
            case kPropertyViewDisplayFilename:
                getFilenameFromKey(out, value);
                return;
            case 0:
                break;
            default: {
                if (g_host) {
                    auto* customizer =
                        static_cast<cISCPropertyCustomizer*>(g_host->CreateObject(info->displayClassId,
                                                                                  kPropertyViewCustomizerIID));
                    if (customizer) {
                        const bool handled = customizer->GetPropertyValueString(propertyId, value, out);
                        customizer->Release();
                        if (handled) return;
                    }
                }
                break;
            }
        }
    }
    FormatVariant(value, nullptr, out);
}

// cSCPropertyView::GetValueEditor(unsigned int, cIGZVariant*, unsigned int, void**) @ 0x10026cb0c
bool cSCPropertyView::GetValueEditor(std::uint32_t propertyId, const cIGZVariant* value, std::uint32_t iid, void** out) {
    *out = nullptr;
    if (!g_host) return false;
    const auto found = g_propertyInfos.find(propertyId);
    if (found == g_propertyInfos.end()) return false;
    const PropertyViewInfo& info = found->second;

    cRZVariant initial;
    if (value) {
        initial.CopyFrom(*value);
    } else if (info.defaultValue.GetType() == cIGZVariant::kTypeUnknown) {
        cIGZSerializable* serializable = nullptr;
        cRZVariant defaultValue = info.defaultValue;   // the original variant interface method is non-const
        if (defaultValue.RefIGZUnknown(kPropertyViewSerializableIID, reinterpret_cast<void**>(&serializable)) &&
            serializable) {
            cIGZUnknown* object = g_host->CreateObject(serializable->GetGZCLSID(), 1);
            serializable->Release();
            if (object) {
                initial.RefIGZUnknown(object);
                object->Release();
            }
        }
    } else {
        initial.CopyFrom(info.defaultValue);
    }

    auto* editor = static_cast<cISCValueEditView*>(g_host->CreateObject(info.editorClassId, iid));
    if (!editor) return false;

    const bool resKeyTableEditor = info.editorClassId == kPropertyViewResKeyTableEditorClasses[0] ||
                                   info.editorClassId == kPropertyViewResKeyTableEditorClasses[1];
    if (resKeyTableEditor) {
        cISCResKeyTableEntryView* view = nullptr;
        if (editor->QueryInterface(kPropertyViewResKeyTableEntryViewIID, reinterpret_cast<void**>(&view)) && view) {
            const auto defaults = g_resKeyTableDefaults.find(propertyId);
            if (defaults != g_resKeyTableDefaults.end()) {
                const std::uint32_t* values = defaults->second.values;
                view->SetKeyPropDefId(propertyId);
                view->SetDefaultKey(cGZPersistResourceKey{values[0], values[1], values[2]});
            }
            view->Release();
        }
    }

    const bool ok = editor->QueryInterface(iid, out);
    if (ok) {
        editor->SetPropertyID(propertyId);
        editor->SetValue(initial);
    }
    editor->Release();
    return ok;
}

// cSCPropertyView::GZOnCommand(unsigned int, unsigned long) @ 0x10026f266
bool cSCPropertyView::GZOnCommand(std::uint32_t type, std::uint64_t param) {
    if (type == kPropertyViewMsgComboChanged) {
        if (param != kPropertyViewComboId) return false;
        cIGZWin* window = GetChildWindowFromID(kPropertyViewComboId);
        if (!window) return true;
        gz::cIGZWinCombo* combo = nullptr;
        if (window->QueryInterface(gz::kGZIID_cIGZWinCombo, reinterpret_cast<void**>(&combo)) && combo) {
            const int selection = combo->GetSelection();
            if (selection >= 0 && static_cast<std::size_t>(selection) < propertyIds_.size()) {
                if (currentView_) currentView_->AsIGZWin()->HideWindow();
                const std::uint32_t propertyId = propertyIds_[static_cast<std::size_t>(selection)];
                const auto view = views_.find(propertyId);
                currentView_ = view == views_.end() ? nullptr : view->second.get();
                if (currentView_) currentView_->AsIGZWin()->ShowWindow();
                cIGZWin* label = GetChildWindowFromID(kPropertyViewHelpLabelId);
                if (label) {
                    gz::cIGZWinText* text = nullptr;
                    if (label->QueryInterface(gz::kGZIID_cIGZWinText, reinterpret_cast<void**>(&text)) && text) {
                        cRZString help;
                        GetHelpText(propertyId, help);
                        text->SetCaption(help);
                        text->Release();
                    }
                }
            }
            combo->Release();
        }
        return true;
    }
    if (type == kPropertyViewMsgCommandSender) {
        doButtonPress(static_cast<std::uint32_t>(param));
    }
    return false;
}

// cSCPropertyView::doButtonPress(unsigned int) @ 0x10026f412
void cSCPropertyView::doButtonPress(std::uint32_t buttonId) {
    if (buttonId == kPropertyViewCancelId) {
        KillSelf(1);
        return;
    }
    if (buttonId != kPropertyViewDoneId) return;
    cIGZWin* window = GetChildWindowFromID(kPropertyViewComboId);
    if (window) {
        gz::cIGZWinCombo* combo = nullptr;
        if (window->QueryInterface(gz::kGZIID_cIGZWinCombo, reinterpret_cast<void**>(&combo)) && combo) {
            const int selection = combo->GetSelection();
            combo->Release();
            if (selection >= 0 && static_cast<std::size_t>(selection) < propertyIds_.size()) {
                if (currentView_) currentView_->AsIGZWin()->HideWindow();
                const std::uint32_t propertyId = propertyIds_[static_cast<std::size_t>(selection)];
                const auto view = views_.find(propertyId);
                currentView_ = view == views_.end() ? nullptr : view->second.get();
                if (currentView_) currentView_->AsIGZWin()->ShowWindow();
                assignValuesToProperty(propertyId);
            }
        }
    }
    KillSelf(0);
}

// cSCPropertyView::assignValuesToProperty(unsigned int) @ 0x10026f528
void cSCPropertyView::assignValuesToProperty(std::uint32_t propertyId) {
    if (!property_) return;
    const auto view = views_.find(propertyId);
    if (view == views_.end() || !view->second) return;
    cRZVariant value;
    view->second->GetValue(value);          // vslot 0x38
    property_->SetPropertyID(propertyId);   // vslot 0x20
    property_->SetPropertyValue(value);     // vslot 0x38
}

// cSCPropertyView::KillSelf(unsigned int) @ 0x10026f5c6
void cSCPropertyView::KillSelf(std::uint32_t result) {
    if (!GetFlag(kPropertyViewFlagNoEndDialog)) {
        cIGZWin* parent = GetParentWin();
        PostMsg(parent, kPropertyViewDeleteChildMessage, reinterpret_cast<std::uint64_t>(this), 0, 0);
    } else if (GetWindowManager()) {
        GetWindowManager()->EndDialog(kPropertyViewEndDialogCode, this, result);
    }
}

// cSCPropertyView::createLineInput(int, int, cIGZWin*, unsigned int, cRZString const&, unsigned int) @ 0x10026ee9a
void cSCPropertyView::createLineInput(int x, int y, cIGZWin* parent, std::uint32_t id, const cRZString& text,
                                      std::uint32_t color) {
    if (!g_host || !parent) return;
    IPropertyTextEdit* edit = g_host->CreateTextEdit();
    if (!edit) return;
    cIGZWin* window = edit->AsIGZWin();
    const std::uint32_t darkBlue = parent->MakeFillColor(0, 0, 0x80);
    const std::uint32_t white = parent->MakeFillColor(0xff, 0xff, 0xff);
    window->SetArea(x, y, parent->GetW() - 10, y + 0x12);
    edit->SetFont(g_host->TextEditFont(kPropertyViewDefaultLabelFont));
    edit->SetFontColor(0, color);
    edit->SetCaretColor(color);
    edit->SetFontColor(4, white);
    edit->SetFontColor(5, darkBlue);
    edit->SetHighlightFillColor(darkBlue);
    edit->SetText(text, false);
    edit->SetFlag(1);       // kTEEditable
    edit->SetFlag(0x200);   // kTESingleLine
    window->SetID(id);
    window->SetFlag(kWinPrivateBuffer, true);
    parent->ChildAdd(window);
    edit->Release();
}

// cSCPropertyView::createSpinner(int, int, cIGZWin*, unsigned int, int, int, int) @ 0x10026f0fe
void cSCPropertyView::createSpinner(int x, int y, cIGZWin* parent, std::uint32_t id, int low, int high, int value) {
    if (!g_host) return;
    IPropertySpinner* spinner = g_host->CreateSpinner(id, 2, low, high, 1, value);
    if (!spinner) return;
    cIGZWin* window = spinner->AsIGZWin();
    const std::uint32_t fill = window->MakeFillColor(0xe0, 0xe0, 0xe0);
    spinner->SetSpinnerFlag(1, true);
    spinner->SetSpinnerFlag(2, true);
    window->SetFillColor(fill);
    window->SetArea(x, y, x + 0x30, y + 0x14);
    if (parent) parent->ChildAdd(window);
    spinner->Release();
}

}  // namespace sc4

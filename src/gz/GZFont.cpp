#include "gz/GZFont.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "gz/BufferFontRenderer.h"
#include "rz/CharLength.h"
#include "rz/StringConversion.h"
#include "rz/TextEncoding.h"
#include "rz/UTF16Text.h"

#pragma clang fp contract(off)

namespace {

constexpr int kCodePageUTF16 = 0x10;

// FreeType charmap tags: 'unic', 'armn', 'lat2', 'sjis', 'big5'.
constexpr std::uint32_t kCharmapUnicode = 0x756e6963;
constexpr std::uint32_t kCharmapAdobeCustom = 0x61726d6e;
constexpr std::uint32_t kCharmapLatin2 = 0x6c617432;
constexpr std::uint32_t kCharmapShiftJis = 0x736a6973;
constexpr std::uint32_t kCharmapBig5 = 0x62696735;

// The table at 0x100a1c490, indexed by cIGZFont encoding id - 1: the FreeType charmap tag of each encoding.
constexpr std::uint32_t kEncodingCharmapTags[12] = {
    0x73796d62,  // 'symb'
    0x756e6963,  // 'unic'
    0x6c617432,  // 'lat2'
    0x736a6973,  // 'sjis'
    0x67622020,  // 'gb  '
    0x62696735,  // 'big5'
    0x77616e73,  // 'wans'
    0x6a6f6861,  // 'joha'
    0x41444f42,  // 'ADOB'
    0x41444245,  // 'ADBE'
    0x41444243,  // 'ADBC'
    0x61726d6e,  // 'armn'
};

// A space as far as the word breaker is concerned: U+0020 and the general punctuation spaces U+2002..U+200B.
inline bool IsBreakingSpace(std::uint32_t c) { return c == 0x20 || (c - 0x2002) < 10; }

inline std::int32_t DivideBy64(std::int32_t v) { return (v + (v < 0 ? 63 : 0)) >> 6; }   // truncates toward zero
inline std::int32_t DivideBy65536(std::int32_t v) { return (v + (v < 0 ? 0xffff : 0)) >> 16; }

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// FT_GZFont_GetStyleFlags @ 0x1002b9fd0
// ---------------------------------------------------------------------------------------------------------------
std::uint32_t FontStyleFlagsFromFace(std::uint32_t freeTypeStyleFlags, const std::string& styleName) {
    // FreeType: bit 0 italic, bit 1 bold. GZ: bit 0 bold, bit 1 italic.
    std::uint32_t flags = ((freeTypeStyleFlags >> 1) & 1) + (freeTypeStyleFlags & 1) * 2;
    if (styleName.empty()) return flags;
    std::string lower = styleName;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower.find("italic") != std::string::npos) flags |= 2;
    if (lower.find("bold") != std::string::npos) flags |= 1;
    return flags;
}

// ---------------------------------------------------------------------------------------------------------------
// CachedBitmapData
// ---------------------------------------------------------------------------------------------------------------
void cGZFont::CachedBitmapData::Clear() {   // 0x1002bab66
    allocation.reset();
    pixels = nullptr;
    width = height = pitch = cursor = 0;
    glyphs.clear();
}

// ---------------------------------------------------------------------------------------------------------------
// Construction, reference counting, life cycle
// ---------------------------------------------------------------------------------------------------------------
cGZFont::cGZFont(IFontHost* host) : host_(host) {}   // 0x1002bab9c: the field defaults are the constructor's stores

cGZFont::~cGZFont() { Shutdown(); }   // 0x1002bae80

bool cGZFont::QueryInterface(std::uint32_t iid, void** out) {   // 0x1002bb0b6
    if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZFont) {
        *out = static_cast<cIGZFont*>(this);
        AddRef();
        return true;
    }
    return false;
}

std::uint32_t cGZFont::AddRef() { return ++refCount_; }   // 0x1002bb0d8

// 0x1002bb0e6: the last reference shuts the font down and deletes it.
std::uint32_t cGZFont::Release() {
    if (refCount_ < 2) {
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

// 0x1002bb12a
bool cGZFont::Init() {
    if (initialized_) return true;
    if (filePath_.Strlen() == 0) return false;
    if (!host_->FileExists(filePath_)) return false;
    if (InitialiseFace()) initialized_ = true;

    if (initialized_) {
        if (renderer_ == nullptr) {
            // The default renderer is built in place (no Init call) and held with one reference.
            renderer_ = new cGZBufferFontRenderer();
            renderer_->AddRef();
        }
        BuildAntialiasingLookupArray();
    }
    return initialized_;
}

// The FreeType half of Init: open the face, attach the Type 1 metrics, pick a charmap, size it and read its names.
bool cGZFont::InitialiseFace() {
    cRZString utf8Path;
    ConvertStringEncoding(filePath_, utf8Path, -1);
    face_ = host_->Rasterizer()->NewFace(utf8Path.ToChar());
    if (!face_) return false;

    cRZString extension;
    if (host_->GetPathExtension(filePath_, extension)) {
        extension.MakeLower();
        if (extension.Strlen() == 4 && (std::strcmp(extension.ToChar(), ".pfb") == 0 || std::strcmp(extension.ToChar(), ".pfa") == 0)) {
            // Type 1 fonts: attach the sibling metrics file, .afm first and .pfm only when the .afm did not attach.
            const std::string base(filePath_.ToChar(), filePath_.Strlen() - extension.Strlen());
            cRZString metrics((base + ".afm").c_str());
            if (host_->FileExists(metrics)) {
                cRZString metricsUtf8;
                ConvertStringEncoding(metrics, metricsUtf8, -1);
                if (!face_->AttachFile(metricsUtf8.ToChar())) {
                    cRZString fallback((base + ".pfm").c_str());
                    if (host_->FileExists(fallback)) {
                        ConvertStringEncoding(fallback, metricsUtf8, -1);
                        face_->AttachFile(metricsUtf8.ToChar());
                    }
                }
            }
        }
    }

    // First charmap that works: Unicode, Adobe custom, Latin 2, Shift-JIS, Big5.
    if (!face_->SelectCharmap(kCharmapUnicode) && !face_->SelectCharmap(kCharmapAdobeCustom) &&
        !face_->SelectCharmap(kCharmapLatin2) && !face_->SelectCharmap(kCharmapShiftJis)) {
        face_->SelectCharmap(kCharmapBig5);
    }

    if (size_ != 0 && !SetSize(size_)) return false;

    const std::string family = face_->FamilyName();
    if (!family.empty()) name_.AssignFromCString(family.c_str());   // CharStringToRZString
    style_ = (style_ & 0x3c) | FontStyleFlagsFromFace(face_->StyleFlags(), face_->StyleName());
    type_ = ((face_->FaceFlags() >> 2) & 1) + 1;
    if (scaleX_ != 1.0f || scaleY_ != 1.0f) {
        const std::int32_t matrix[4] = {static_cast<std::int32_t>(scaleX_ * 65536.0f), 0, 0,
                                        static_cast<std::int32_t>(scaleY_ * 65536.0f)};
        face_->SetTransform(matrix);
    }
    defaultGlyph_ = face_->GetCharIndex(defaultChar_);
    return true;
}

// 0x1002bb7c4: lookup[i] = 255 * (i / 255) ^ gamma, then an S-curve of strength `sharpness` around the middle.
void cGZFont::BuildAntialiasingLookupArray() {
    if (gamma_ == 1.0f && sharpness_ == 0.0f) {
        lookup_.clear();
        grayCache_.Clear();
        return;
    }
    lookup_.resize(0x100, 0);
    grayCache_.Clear();
    const float exponent = static_cast<float>(std::pow(static_cast<double>(sharpness_) + 1.0, 3.321928094887362));
    for (int i = 0; i < 256; ++i) {
        const float linear = static_cast<float>(i) / 255.0f;
        const float powered = std::pow(linear, gamma_);
        float value = (gamma_ != 1.0f) ? powered : linear;
        if (sharpness_ != 0.0f) {
            const float t = (1.0f < value) ? 1.0f : value;
            if (i < 0x80) {
                const float twice = t + t;
                value = std::pow(twice, exponent) * 0.5f;
            } else {
                const float mirrored = (1.0f - t) + (1.0f - t);
                value = std::pow(mirrored, exponent) * -0.5f + 1.0f;
            }
        }
        lookup_[i] = static_cast<std::uint8_t>(static_cast<int>(value * 255.0f));
    }
}

// 0x1002bb9a2
bool cGZFont::Shutdown() {
    if (initialized_) {
        SetRenderer(nullptr);
        monoCache_.Clear();
        grayCache_.Clear();
        currentPalette_ = nullptr;
        paletteCache_.clear();
        face_.reset();   // FT_Done_Face
        initialized_ = false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Simple properties
// ---------------------------------------------------------------------------------------------------------------
bool cGZFont::SetStyle(int style, bool on) {   // 0x1002bba38: only bits 0x3c can be edited
    if (on) style_ |= static_cast<std::uint32_t>(style) & 0x3c;
    else style_ &= ~(static_cast<std::uint32_t>(style) & 0x3c);
    return true;
}

void cGZFont::GetName(cIGZString& out) const { out.AssignFromCString(name_.ToChar()); }   // 0x1002bf76a
std::uint32_t cGZFont::GetStyle() const { return style_; }                                // 0x1002bf794
bool cGZFont::GetStyle(int style) const {                                                 // 0x1002bf79e
    return (style_ & static_cast<std::uint32_t>(style)) == static_cast<std::uint32_t>(style);
}
std::uint32_t cGZFont::GetType() const { return type_; }                                  // 0x1002bf7ae
std::uint32_t cGZFont::GetSize() const { return size_; }                                  // 0x1002bf7b8
std::uint32_t cGZFont::GetFontColor() const { return color_; }                            // 0x1002bf7c2
std::uint32_t cGZFont::GetFontBackgroundColor() const { return backgroundColor_; }        // 0x1002bf7cc
void cGZFont::SetFontColor(std::uint32_t color, std::uint32_t background) {               // 0x1002bf7d6
    color_ = color;
    backgroundColor_ = background;
}
int cGZFont::GetAntialiasing() { return antialiasing_; }                                  // 0x1002bf7e2
void cGZFont::SetAntialiasing(int mode) {                                                  // 0x1002bd984
    if (mode != 0) antialiasing_ = mode;
}
float cGZFont::GetAntialiasingGammaCorrection() { return gamma_; }                         // 0x1002bd992

void cGZFont::SetAntialiasingGammaCorrection(float gamma) {   // 0x1002bd9a0: clamped to 0..10
    float v = (0.0f <= gamma) ? gamma : 0.0f;
    v = (v <= 10.0f) ? v : 10.0f;
    if (gamma_ == v) return;
    gamma_ = v;
    BuildAntialiasingLookupArray();
}

float cGZFont::GetAntialiasingSharpness() { return sharpness_; }                           // 0x1002bd9d6

void cGZFont::SetAntialiasingSharpness(float sharpness) {   // 0x1002bd9e4: clamped to -1..1
    float v = (-1.0f <= sharpness) ? sharpness : -1.0f;
    v = (v <= 1.0f) ? v : 1.0f;
    if (v == sharpness_) return;
    sharpness_ = v;
    BuildAntialiasingLookupArray();
}

void cGZFont::GetFilePath(cIGZString& out) const { out.AssignFromCString(filePath_.ToChar()); }   // 0x1002bf7ec
void cGZFont::SetFilePath(const cIGZString& path) { filePath_.AssignFromCString(path.ToChar()); }  // 0x1002bf816
bool cGZFont::GetCodePage(int& codePage) const { codePage = codePage_; return true; }              // 0x1002bda1c
bool cGZFont::SetCodePage(int codePage) { codePage_ = codePage; return true; }                     // 0x1002bda2c

void cGZFont::GetScaling(float& x, float& y) const { x = scaleX_; y = scaleY_; }   // 0x1002bdfe8

// 0x1002bdffe: both factors are clamped to 0.01..1000; a change drops both atlases and re-installs the face transform.
void cGZFont::SetScaling(float x, float y) {
    float sx = (0.01f <= x) ? x : 0.01f;
    sx = (sx <= 1000.0f) ? sx : 1000.0f;
    float sy = (0.01f <= y) ? y : 0.01f;
    sy = (sy <= 1000.0f) ? sy : 1000.0f;
    if (sx == scaleX_ && sy == scaleY_) return;
    scaleX_ = sx;
    scaleY_ = sy;
    monoCache_.Clear();
    grayCache_.Clear();
    if (face_) {
        const std::int32_t matrix[4] = {static_cast<std::int32_t>(scaleX_ * 65536.0f), 0, 0,
                                        static_cast<std::int32_t>(scaleY_ * 65536.0f)};
        face_->SetTransform(matrix);
    }
}

// 0x1002be0c2: the y factor reported is always 1.
void cGZFont::GetAdvanceScaling(float& x, float& y) const { x = advanceScale_; y = 1.0f; }

// 0x1002be0d6: only the x factor exists (clamped to 0.1..10); the second argument is ignored.
void cGZFont::SetAdvanceScaling(float x, float) {
    float v = (0.1f <= x) ? x : 0.1f;
    v = (v <= 10.0f) ? v : 10.0f;
    if (advanceScale_ == v) return;
    advanceScale_ = v;
    monoCache_.Clear();
    grayCache_.Clear();
}

void cGZFont::GetSpacingAdjustment(int& x, int& y) const { x = spacingX_; y = spacingY_; }   // 0x1002be136
void cGZFont::SetSpacingAdjustment(int x, int y) { spacingX_ = x; spacingY_ = y; }           // 0x1002be14c

// 0x1002be2d2 / 0x1002be2e2 / 0x1002be2ec / 0x1002be2fe
int cGZFont::GetLineHeight() { return spacingY_ + pixelHeight_; }
int cGZFont::CharHeight(const void*, std::uint32_t) { return pixelHeight_; }
int cGZFont::GetSizeAboveBaseline() { return spacingY_ + ascent_; }
int cGZFont::GetSizeBelowBaseline() { return GetLineHeight() - GetSizeAboveBaseline(); }

// 0x1002bd7d2: size in pixels (at most 0x800). Refused once any glyph is cached (either atlas).
bool cGZFont::SetSize(std::uint32_t size) {
    if (size == 0 || !monoCache_.glyphs.empty() || !grayCache_.glyphs.empty()) return false;
    const std::uint32_t clamped = size < 0x800 ? size : 0x800;
    size_ = clamped;
    if (!face_) return true;   // no face yet: the size is just remembered
    const bool sized = face_->SetPixelSizes(clamped, clamped);
    FontFaceMetrics& m = face_->Metrics();
    if (m.bboxYMax > 0x10000) {
        // A bitmap font reports its box in 16.16 pixels.
        m.bboxXMin = DivideBy65536(m.bboxXMin);
        m.bboxYMin = DivideBy65536(m.bboxYMin);
        m.bboxXMax = DivideBy65536(m.bboxXMax);
        m.bboxYMax = DivideBy65536(m.bboxYMax);
    }
    const std::int32_t top = FontMulFix(m.bboxYMax, m.yScale);
    const std::int32_t bottom = FontMulFix(m.bboxYMin, m.yScale);
    const std::int32_t gap = FontMulFix((m.height - m.ascender) - m.descender, m.yScale);
    const std::int32_t ascent = FontMulFix(m.bboxYMax, m.yScale);
    const std::int32_t belowBaseline = (bottom < -0x3f) ? bottom : 0;
    lineGap_ = DivideBy64(gap);
    ascent_ = DivideBy64(ascent);
    const std::int32_t height = DivideBy64(top - belowBaseline) - 1;
    pixelHeight_ = (height != 0) ? height : 1;
    return sized;
}

// ---------------------------------------------------------------------------------------------------------------
// Renderer plumbing
// ---------------------------------------------------------------------------------------------------------------
cIGZFontRenderer* cGZFont::GetRenderer() const { return renderer_; }   // 0x1002bf654

// 0x1002bf662: the new renderer is referenced and initialised first, the old one shut down and released.
void cGZFont::SetRenderer(cIGZFontRenderer* renderer) {
    cIGZFontRenderer* current = renderer_;
    if (current == renderer) return;
    if (renderer != nullptr) {
        renderer->AddRef();
        renderer->Init(this);
        current = renderer_;
    }
    if (current != nullptr) {
        current->Shutdown(this);
        renderer_->Release();
    }
    renderer_ = renderer;
}

bool cGZFont::LockSurfaceForFontUse(void* surface) {   // 0x1002bf6c4
    if (lockCount_ == 0) renderer_->DrawBegin(this, surface, static_cast<cIGZFontRenderer::eRenderingMode>(NormalisedMode(antialiasing_)));
    ++lockCount_;
    return true;
}

bool cGZFont::UnlockSurfaceForFontUse(void* surface) {   // 0x1002bf716
    if (lockCount_ == 1) renderer_->DrawEnd(this, surface, static_cast<cIGZFontRenderer::eRenderingMode>(NormalisedMode(antialiasing_)));
    --lockCount_;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Character availability
// ---------------------------------------------------------------------------------------------------------------
bool cGZFont::IsCharEncodingAvailable(int encoding) {   // 0x1002bda3a
    if (!face_) return false;
    std::uint32_t tag = 0;
    if (static_cast<std::uint32_t>(encoding - 1) < 12) tag = kEncodingCharmapTags[encoding - 1];
    const bool available = face_->SelectCharmap(tag);
    face_->SelectCharmap(kCharmapUnicode);   // put the Unicode charmap back
    return available;
}

bool cGZFont::IsCharAvailable(void* chars, std::uint32_t length) {   // 0x1002bda96
    if (length == 0) return true;
    const std::uint16_t* text = nullptr;
    std::uint32_t count = 0;
    GetTextAsUnicode(chars, length, text, count, 0, false);
    const int savedCodePage = codePage_;
    codePage_ = kCodePageUTF16;
    bool available = true;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (face_->GetCharIndex(text[i]) == 0) {
            available = false;
            break;
        }
    }
    codePage_ = savedCodePage;
    return available;
}

// 0x1002bdb46: a code page is available when the face has a glyph for each of a few sample characters of its script.
bool cGZFont::IsCodePageAvailable(int codePage) {
    std::vector<std::uint32_t> samples;
    if (codePage < 0x3b5) {
        if (codePage < 0x36a) {
            if (codePage == 0) samples = {0x61};
            else if (codePage == 8 || codePage == 0x10) return true;
        } else if (codePage == 0x36a) samples = {0xe01, 0xe2c};         // Thai
        else if (codePage == 0x3a4) samples = {0x3061, 0x30a4};          // Shift-JIS
        else if (codePage == 0x3a8) samples = {0x70a7, 0x7535};          // GBK
    } else {
        switch (codePage) {
            case 0x4e2: samples = {0x142}; break;
            case 0x4e3: samples = {0x401}; break;
            case 0x4e4: samples = {0x61}; break;
            case 0x4e5: samples = {0x370, 0x393}; break;
            case 0x4e6: samples = {0x11e}; break;
            case 0x4e7: samples = {0x5d0, 0x5e2}; break;
            case 0x4e8: samples = {0x621, 0x641}; break;
            case 0x4e9: samples = {0x160}; break;
            default:
                if (codePage == 0x3b5) samples = {0xc2b9, 0xc9d1};       // Korean
                else if (codePage == 0x3b6) samples = {0x6f54, 0x70b5};  // Big5
                break;
        }
    }
    for (std::uint32_t c : samples) {
        if (face_->GetCharIndex(c) == 0) return false;
    }
    return true;
}

bool cGZFont::PreloadChars(void* chars, std::uint32_t length) {   // 0x1002bdf70
    const bool ready = initialized_;
    if (length != 0 && ready) {
        const std::uint16_t* text = nullptr;
        std::uint32_t count = 0;
        GetTextAsUnicode(chars, length, text, count, 0, false);
        for (std::uint32_t i = 0; i < count; ++i) GetCharData(text[i]);
    }
    return ready;
}

// ---------------------------------------------------------------------------------------------------------------
// Text conversion and Thai shaping
// ---------------------------------------------------------------------------------------------------------------
// 0x1002bbc7c / 0x1002bd004: characters that have presentation-form variants in the font's private area.
std::uint32_t cGZFont::GetSubstitutionCharCount(std::uint16_t c) {
    if (static_cast<std::uint16_t>(c - 0xe0d) < 0x41) {
        const std::uint32_t index = static_cast<std::uint16_t>(c - 0xe31);
        if (index < 0x1d) {
            if ((0x1040007dUL >> index) & 1) return 1;
            if ((0xf800000UL >> index) & 1) return 3;
        }
        if (c == 0xe0d) return 1;
    }
    return 0;
}

// 0x1002bd056: variant 1 of a one-variant character, or variant 1..3 of a tone mark / thanthakhat.
std::uint32_t cGZFont::GetSubstitutionChar(std::uint16_t c, std::uint32_t variant) {
    const std::uint32_t original = c;
    std::uint32_t result = original;
    switch (static_cast<std::uint16_t>(c - 0xe31)) {
        case 0: result = 0xf710; break;
        case 3: result = 0xf701; break;
        case 4: result = 0xf702; break;
        case 5: result = 0xf703; break;
        case 6: result = 0xf704; break;
        case 0x16: result = 0xf712; break;
        case 0x1c: result = 0xf711; break;
        case 0x17: case 0x18: case 0x19: case 0x1a: case 0x1b: {
            static const std::uint16_t tones[5][3] = {
                {0xf713, 0xf70a, 0xf705}, {0xf714, 0xf70b, 0xf706}, {0xf715, 0xf70c, 0xf707},
                {0xf716, 0xf70d, 0xf708}, {0xf717, 0xf70e, 0xf709}};
            if (variant - 1 < 3) return tones[static_cast<std::uint16_t>(c - 0xe31) - 0x17][variant - 1];
            return original;
        }
        default:
            if (c == 0xe0d) { result = 0xf70f; break; }
            return original;
    }
    // The characters above have a single variant: anything but variant 1 leaves them alone.
    return variant == 1 ? result : original;
}

// 0x1002bbba6: only Thai needs shaping (language 0x13).
bool cGZFont::IsSubstitutableCharPresent(const std::uint16_t* text, std::uint32_t length) {
    if (host_->GetCurrentLanguage() != kLanguageThai) return false;
    for (std::uint32_t i = 0; i < length; ++i) {
        if (GetSubstitutionCharCount(text[i]) != 0) return true;
    }
    return false;
}

// 0x1002bbbfe
std::uint32_t cGZFont::ConvertStringToUnicode(const void* text, std::uint32_t length, std::vector<std::uint16_t>& out) {
    if (codePage_ == kCodePageUTF16) {
        out.resize(length);
        std::memcpy(out.data(), text, static_cast<std::size_t>(length) * 2);
        return length;
    }
    unsigned produced = static_cast<unsigned>(out.size());
    ConvertStringEncoding(text, length, codePage_, out.data(), produced, kCodePageUTF16);
    return produced;
}

// 0x1002bba50: the text as UTF-16 (shaped when Thai presentation forms are needed) in one of five scratch buffers.
void cGZFont::GetTextAsUnicode(const void* text, std::uint32_t length, const std::uint16_t*& out, std::uint32_t& outLength,
                               int buffer, bool recordMapping) {
    if (recordMapping) substitutionMap_.clear();
    std::vector<std::uint16_t>& scratch = textBuffers_[buffer];
    std::uint16_t* shapeTarget;
    std::uint32_t shapeLength;
    if (codePage_ == kCodePageUTF16) {
        if (!IsSubstitutableCharPresent(static_cast<const std::uint16_t*>(text), length)) {
            out = static_cast<const std::uint16_t*>(text);
            outLength = length;
            return;
        }
        if (scratch.size() < static_cast<std::size_t>(length) * 2) scratch.resize(static_cast<std::size_t>(length) * 2);
        outLength = ConvertStringToUnicode(text, length, scratch);
        shapeTarget = scratch.data();
        shapeLength = outLength;
    } else {
        if (scratch.size() < static_cast<std::size_t>(length) * 12) scratch.resize(static_cast<std::size_t>(length) * 12);
        outLength = ConvertStringToUnicode(text, length, scratch);
        if (!IsSubstitutableCharPresent(scratch.data(), outLength)) {
            out = scratch.data();
            return;
        }
        shapeTarget = scratch.data();
        shapeLength = outLength;
    }
    MakeSubstitutedString(shapeTarget, shapeLength, out, outLength, recordMapping);
}

// 0x1002bbc7c: Thai shaping. First sara am (U+0E33) is split into nikhahit + sara aa (the nikhahit moves in front of a
// tone mark that precedes it), then every tone mark / upper vowel / lower vowel picks its presentation form from the
// character before it. Works in place on `text` (or on the substitution buffer after a split).
void cGZFont::MakeSubstitutedString(std::uint16_t* text, std::uint32_t length, const std::uint16_t*& out,
                                    std::uint32_t& outLength, bool recordMapping) {
    out = text;
    outLength = length;
    if (length == 0) return;

    bool hasSaraAm = false;
    for (std::uint32_t i = 0; i < length; ++i) {
        if (text[i] == 0xe33) { hasSaraAm = true; break; }
    }
    if (hasSaraAm) {
        std::vector<std::uint16_t>& buf = substitutionBuffer_;
        if (buf.capacity() < static_cast<std::size_t>(length) * 2) buf.reserve(static_cast<std::size_t>(length) * 2);
        buf.clear();
        std::vector<std::uint32_t>& map = substitutionMap_;
        for (std::uint32_t i = 0; i < length; ++i) {
            if (text[i] == 0xe33) {
                if (i == 0 || UTF16GetThaiCharType(text[i - 1]) != 9) {
                    buf.push_back(0xe4d);
                    if (recordMapping) map.push_back(i);
                } else {
                    // The previous character is a tone mark: the nikhahit goes underneath it, in front.
                    buf.insert(buf.end() - 1, 0xe4d);
                    if (recordMapping) map.insert(map.end() - 1, i);
                }
                buf.push_back(0xe32);
                if (recordMapping) map.push_back(i);
            } else {
                buf.push_back(text[i]);
                if (recordMapping) map.push_back(i);
            }
        }
        map.push_back(length);   // kept: the end marker is appended even when nothing was recorded
        MakeSubstitutedString(buf.data(), static_cast<std::uint32_t>(buf.size()), out, outLength, false);
        return;
    }

    std::uint32_t previousType = 0;
    std::uint32_t state = 0;
    std::uint32_t previousChar = 0;
    std::uint32_t currentChar = text[0];
    std::uint32_t currentType = static_cast<std::uint32_t>(UTF16GetThaiCharType(currentChar));
    for (std::uint32_t i = 0; i < length; ++i) {
        std::uint32_t nextChar = 0;
        std::uint32_t nextType = 0;
        std::uint16_t result = text[i];
        bool done = false;
        if (i + 1 < length) {
            nextChar = text[i + 1];
            if (nextChar != 0) {
                nextType = static_cast<std::uint32_t>(UTF16GetThaiCharType(nextChar));
                if (currentChar == 0xe0d && nextType - 6 <= 2) {
                    // yo ying followed by a lower vowel loses its tail
                    result = static_cast<std::uint16_t>(GetSubstitutionChar(0xe0d, 1));
                    state = 0;
                    done = true;
                }
            }
        }
        if (!done) {
            std::uint32_t newState = 0;
            if (currentType < 0x10) {
                if ((0xee00u >> currentType) & 1) {   // tone marks (9..11) and upper vowels (13..15)
                    const bool raised = previousChar == 0xe2c || (previousChar | 2) == 0xe1f || previousChar == 0xe1b || state == 1;
                    std::uint32_t variant = (raised ? 1u : 0u) + 2;
                    if (static_cast<int>(previousType) > 9) variant = raised ? 1u : 0u;
                    state = raised ? 1u : 0u;
                    if (currentType == 9) state = variant;
                    if (currentChar == 0xe4c) state = variant;
                    result = (state == 0) ? text[i] : static_cast<std::uint16_t>(GetSubstitutionChar(static_cast<std::uint16_t>(currentChar), state));
                    done = true;
                } else if ((0xc0u >> currentType) & 1) {   // lower vowels (6, 7)
                    const std::uint32_t index = (previousChar - 0xe1b) & 0xffff;
                    if (index < 0x12 && ((0x20015ul >> index) & 1)) newState = 1;
                }
            }
            if (!done) state = newState;
        }
        text[i] = result;
        previousType = currentType;
        previousChar = currentChar;
        currentChar = nextChar;
        currentType = nextType;
    }
}

// 0x1002be1bc: the first character of `text` as UTF-16; `consumed` receives the source units it took.
std::uint32_t cGZFont::ConvertCharToUnicode(const void* text, std::uint32_t length, std::uint32_t* consumed) {
    if (length == 0) return 0;
    const auto* bytes = static_cast<const std::uint8_t*>(text);
    if (codePage_ == kCodePageUTF16) {
        if (consumed != nullptr) *consumed = 2;
        std::uint16_t c;
        std::memcpy(&c, text, 2);
        return c;
    }
    bool multi = false;
    if (codePage_ == 8) multi = static_cast<std::int8_t>(bytes[0]) < 0;
    else if (codePage_ == 0) {
        if (bytes[0] < 0x80) {
            if (consumed != nullptr) *consumed = 1;
            return bytes[0];
        }
        // A high byte of the system code page: converted on its own, always counted as one unit.
        std::uint16_t converted = 0;
        unsigned capacity = 1;
        ConvertStringEncoding(text, 1, 0, &converted, capacity, kCodePageUTF16);
        if (consumed != nullptr) *consumed = 1;
        return converted;
    } else {
        multi = static_cast<std::int8_t>(bytes[0]) < 0;
    }
    if (multi) {
        const unsigned charLength = static_cast<unsigned>(GetCharLength(text, length, codePage_));
        std::uint16_t converted = 0;
        unsigned capacity = 1;
        const unsigned used = ConvertStringEncoding(text, charLength, codePage_, &converted, capacity, kCodePageUTF16);
        if (consumed != nullptr) *consumed = used;
        return converted;
    }
    if (consumed != nullptr) *consumed = 1;
    return bytes[0];
}

// ---------------------------------------------------------------------------------------------------------------
// Glyph atlas
// ---------------------------------------------------------------------------------------------------------------
cGZFont::CachedBitmapData* cGZFont::GetAppropriateCachedBitmapData() {   // 0x1002bc1d6
    return antialiasing_ == 1 ? &monoCache_ : &grayCache_;
}

// 0x1002bc1f2: the cached glyph for `character`, rendering it into the atlas on first use.
cGZFont::CachedBitmapDataDetail* cGZFont::GetCharData(std::uint32_t character) {
    CachedBitmapData* cache = GetAppropriateCachedBitmapData();
    if (cache == nullptr) return nullptr;
    auto it = cache->glyphs.find(character);
    if (it != cache->glyphs.end()) return &it->second;
    return AddCharData(character, cache);
}

bool cGZFont::StoreGlyph(CachedBitmapData* cache, std::uint32_t character, const CachedBitmapDataDetail& detail) {
    return cache->glyphs.emplace(character, detail).second;   // an existing entry is kept
}

// 0x1002bc24c
cGZFont::CachedBitmapDataDetail* cGZFont::AddCharData(std::uint32_t character, CachedBitmapData* cache) {
    const std::uint32_t half = static_cast<std::uint32_t>(pixelHeight_) >> 1;
    std::unique_ptr<IFontGlyph> glyph;
    bool usable = false;
    std::uint32_t glyphIndex = face_ ? face_->GetCharIndex(character) : 0;
    if (face_ && (glyphIndex != 0 || (glyphIndex = defaultGlyph_) != 0)) glyph = face_->LoadGlyph(glyphIndex);
    bool bitmapMissing = false;   // FT_Glyph_To_Bitmap failed: the glyph keeps its advance but has no pixels
    if (glyph) {
        const bool mono = antialiasing_ == 1;
        if (glyph->IsBitmap()) {
            usable = true;
        } else if (glyph->ConvertToBitmap(mono)) {
            usable = true;
        } else {
            usable = true;
            bitmapMissing = true;
        }
        if (usable && !bitmapMissing) {
            const std::uint16_t grays = glyph->Bitmap().numGrays;
            if ((grays | 0x100) != 0x100) usable = false;   // only 0 (mono) or 256 grays are understood
        }
    }

    if (usable) {
        const std::uint32_t pixelMode = (antialiasing_ == 1) ? 2 : 0x100;
        const FontBitmap& bitmap = glyph->Bitmap();
        CachedBitmapDataDetail detail;
        detail.x0 = static_cast<std::int32_t>(cache->cursor);
        detail.y0 = 0;
        detail.x1 = detail.x0;
        detail.y1 = 0;
        detail.xOffset = 0;
        detail.yOffset = static_cast<std::int16_t>(ascent_);
        if (!bitmapMissing) {
            detail.xOffset = static_cast<std::int16_t>(bitmap.left);
            detail.yOffset = static_cast<std::int16_t>(ascent_ - bitmap.top);
            detail.y1 = static_cast<std::int32_t>(bitmap.rows);
            detail.x1 += static_cast<std::int32_t>(bitmap.width);
        }
        // Advance: the glyph's own (16.16 pixels) or, for fixed width faces, the face's maximum advance (26.6).
        float advance;
        if ((face_->FaceFlags() & kFontFaceFlagFixedWidth) == 0) {
            advance = static_cast<float>(glyph->AdvanceX()) * advanceScale_ * 1.52587890625e-05f;
        } else {
            const FontFaceMetrics& m = face_->Metrics();
            const std::int32_t fixed = FontMulFix(m.maxAdvanceWidth, m.xScale);
            advance = static_cast<float>(fixed) * advanceScale_ * 0.015625f;
        }
        detail.advance = static_cast<std::uint16_t>(static_cast<std::int32_t>(advance));
        if (character == 0xfeff || character == 0x200b) detail.advance = 0;   // zero width characters
        if (bitmapMissing) {
            CopyGlyphToSourceData(cache, &detail, static_cast<int>(detail.x0), static_cast<int>(detail.y0), nullptr, 0, 0, 0, 2);
        } else {
            CopyGlyphToSourceData(cache, &detail, static_cast<int>(detail.x0), static_cast<int>(detail.y0), bitmap.buffer,
                                  bitmap.width, bitmap.rows, static_cast<std::uint32_t>(bitmap.pitch), pixelMode);
        }
        StoreGlyph(cache, character, detail);
        return GetCharData(character);
    }

    // No usable glyph: a half-height square box (nothing for the zero width characters).
    CachedBitmapDataDetail detail;
    detail.x0 = static_cast<std::int32_t>(cache->cursor);
    detail.y0 = 0;
    detail.x1 = detail.x0 + static_cast<std::int32_t>(half);
    detail.y1 = static_cast<std::int32_t>(half);
    detail.xOffset = 0;
    detail.yOffset = 0;
    detail.advance = static_cast<std::uint16_t>(static_cast<std::int32_t>(static_cast<float>(half) * advanceScale_));
    if (character == 0xfeff || character == 0x200b) {
        detail.advance = 0;
        CopyGlyphToSourceData(cache, &detail, detail.x0, 0, nullptr, 0, 0, 0, 2);
    } else {
        std::vector<std::uint8_t> box(static_cast<std::size_t>(half) * half);
        for (std::uint32_t outer = 0; outer < half; ++outer) {
            for (std::uint32_t inner = 0; inner < half; ++inner) {
                const bool border = outer == 0 || inner == 0 || outer == half - 1 || inner == half - 1;
                box[outer + static_cast<std::size_t>(inner) * half] = border ? 0xff : 0;
            }
        }
        CopyGlyphToSourceData(cache, &detail, detail.x0, 0, box.data(), half, half, half, 0x100);
    }
    StoreGlyph(cache, character, detail);
    return GetCharData(character);
}

// 0x1002bcaf2: grows the atlas to at least width x height, keeping the old rows (the block has two spare rows on each
// side of the pixel area).
bool cGZFont::ReallocateSourceData(CachedBitmapData* cache, std::uint32_t width, std::uint32_t height) {
    if (cache->width < width || cache->height < height) {
        const std::size_t total = static_cast<std::size_t>(height + 4) * width;
        std::unique_ptr<std::uint8_t[]> block(new std::uint8_t[total]());
        std::uint8_t* pixels = block.get() + static_cast<std::size_t>(width) * 2;
        if (cache->pixels != nullptr) {
            for (std::uint32_t row = 0; row < cache->height; ++row) {
                if (cache->width != 0) std::memmove(pixels + static_cast<std::size_t>(row) * width,
                                                    cache->pixels + static_cast<std::size_t>(row) * cache->pitch, cache->width);
            }
        }
        cache->allocation = std::move(block);
        cache->pixels = pixels;
        cache->width = width;
        cache->height = height;
        cache->pitch = width;
    }
    return true;
}

// 0x1002bcc2c
bool cGZFont::CopyGlyphToSourceData(CachedBitmapData* cache, CachedBitmapDataDetail* detail, int x, int y,
                                    const std::uint8_t* source, std::uint32_t w, std::uint32_t h, std::uint32_t pitch,
                                    std::uint32_t mode) {
    std::uint32_t span = w;   // columns the glyph occupies in the atlas
    if (style_ & 0xc) {
        // Underlined / struck-through text is as wide as its advance, and a positive left bearing is folded into the glyph.
        const std::int16_t advance = static_cast<std::int16_t>(detail->advance);
        if (advance > 0 && static_cast<std::uint32_t>(advance) >= w) span = static_cast<std::uint32_t>(advance);
        const std::int32_t bearing = detail->xOffset;
        if (bearing > 0) {
            detail->x0 += bearing;
            span += bearing;
            detail->x1 += bearing;
            detail->xOffset = 0;
        }
    }
    const std::uint32_t endX = span + static_cast<std::uint32_t>(x);
    cache->cursor = endX;
    if (endX >= cache->width) {
        const std::uint32_t newHeight = std::max<std::uint32_t>(cache->height, static_cast<std::uint32_t>(pixelHeight_));
        ReallocateSourceData(cache, span * 32 + x, newHeight);
    }
    if (cache->height < h + static_cast<std::uint32_t>(y)) {
        ReallocateSourceData(cache, cache->width, h + y);
        h = cache->height - y;
        if (cache->height < static_cast<std::uint32_t>(y)) h = 0;
    }
    if (w != 0) {
        std::vector<std::uint8_t> expanded;
        const std::uint8_t* rows = source;
        std::uint32_t sourcePitch = pitch;
        if (mode == 2) {
            // 1 bit per pixel -> 0 / 0xff bytes
            expanded.resize(static_cast<std::size_t>(w) * h);
            for (std::uint32_t row = 0; row < h; ++row) {
                for (std::uint32_t col = 0; col < w; ++col) {
                    const std::uint8_t bits = source[(col >> 3) + row * pitch];
                    expanded[row * w + col] = (bits & (0x80 >> (col & 7))) ? 0xff : 0;
                }
            }
            rows = expanded.data();
            sourcePitch = w;
        }
        std::uint8_t* dst = cache->pixels + static_cast<std::ptrdiff_t>(y) * cache->pitch + detail->x0;   // the destination column is detail->x0 (shifted by a folded bearing), not x
        for (std::uint32_t row = 0; row < h; ++row) {
            std::memcpy(dst, rows, w);
            if (antialiasing_ != 1 && !lookup_.empty()) {
                for (std::uint32_t col = 0; col < w; ++col) dst[col] = lookup_[dst[col]];
            }
            dst += cache->pitch;
            rows += sourcePitch;
        }
    }

    if (style_ & 0xc) {
        if (style_ & 4) {   // underline
            std::int32_t thickness = 1;
            if (size_ >= 0x10) {
                thickness = 2;
                if (size_ >= 0x1a) thickness = (size_ < 0x24) ? 3 : 4;
            }
            std::int32_t row = ascent_ - detail->yOffset + thickness;
            if (row >= 0 && static_cast<std::uint32_t>(row) < static_cast<std::uint32_t>(pixelHeight_)) {
                std::int32_t i = 0;
                for (;; ) {
                    std::memset(cache->pixels + static_cast<std::ptrdiff_t>(row + i) * cache->pitch + x, 0xff, span);
                    ++i;
                    if (i >= thickness) break;
                    if (static_cast<std::uint32_t>(row + i) >= static_cast<std::uint32_t>(pixelHeight_)) break;
                }
                if (detail->y1 < row + i) detail->y1 = row + i;
            }
        }
        if (style_ & 8) {   // strike-through
            std::int32_t thickness = 1;
            if (size_ >= 0x18) {
                thickness = 2;
                if (size_ >= 0x30) thickness = (size_ < 0x48) ? 3 : 4;
            }
            const std::int32_t row = ascent_ - detail->yOffset - static_cast<std::int32_t>(static_cast<std::uint32_t>(pixelHeight_) >> 2);
            if (row >= 0 && static_cast<std::uint32_t>(row) < static_cast<std::uint32_t>(pixelHeight_)) {
                std::int32_t i = 0;
                for (;;) {
                    std::memset(cache->pixels + static_cast<std::ptrdiff_t>(row + i) * cache->pitch + x, 0xff, span);
                    ++i;
                    if (i >= thickness) break;
                    if (static_cast<std::uint32_t>(row + i) >= static_cast<std::uint32_t>(pixelHeight_)) break;
                }
                if (detail->y1 < row + i) detail->y1 = row + i;
            }
        }
        detail->x0 = x;
        if (x != 0) detail->x1 = static_cast<std::int32_t>(endX);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Per-character metrics
// ---------------------------------------------------------------------------------------------------------------
int cGZFont::CharWidth(const void* chars, std::uint32_t length) {   // 0x1002be16e
    if (!initialized_) return 0;
    const std::uint32_t c = (codePage_ == kCodePageUTF16) ? *static_cast<const std::uint16_t*>(chars)
                                                          : ConvertCharToUnicode(chars, length, nullptr);
    const CachedBitmapDataDetail* d = GetCharData(c);
    return d ? d->x1 - d->x0 : 0;
}

int cGZFont::GetXOffset(const void* chars, std::uint32_t length) {   // 0x1002be32c
    if (!initialized_) return 0;
    const std::uint32_t c = (codePage_ == kCodePageUTF16) ? *static_cast<const std::uint16_t*>(chars)
                                                          : ConvertCharToUnicode(chars, length, nullptr);
    const CachedBitmapDataDetail* d = GetCharData(c);
    return d ? d->xOffset : 0;
}

int cGZFont::GetAdvanceWidth(const void* chars, std::uint32_t length) {   // 0x1002be378
    if (!initialized_) return 0;
    const std::uint32_t c = (codePage_ == kCodePageUTF16) ? *static_cast<const std::uint16_t*>(chars)
                                                          : ConvertCharToUnicode(chars, length, nullptr);
    const CachedBitmapDataDetail* d = GetCharData(c);
    return d ? static_cast<std::int16_t>(d->advance) + spacingX_ : 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Measuring and wrapping
// ---------------------------------------------------------------------------------------------------------------
// 0x1002bebea: sum of the advances of the base characters (combining Thai marks ride on the previous one).
int cGZFont::GetStringWidth(const void* text, std::uint32_t length) {
    if (length == 0) return 0;
    const std::uint16_t* units = nullptr;
    std::uint32_t count = 0;
    GetTextAsUnicode(text, length, units, count, 4, false);
    const int savedCodePage = codePage_;
    codePage_ = kCodePageUTF16;
    int width = 0;
    if (count != 0) {
        const std::uint16_t* p = units;
        const std::uint16_t* end = units + count;
        do {
            width += GetAdvanceWidth(p, 1);
            const unsigned step = GetComposedCharLength(p, static_cast<unsigned>(end - p), codePage_);
            p += step;
        } while (p < end);
    }
    codePage_ = savedCodePage;
    return width;
}

// 0x1002beca8: width without the trailing spaces; `visible` receives the length of that text.
int cGZFont::GetStringVisibleWidth(const void* text, std::uint32_t length, std::uint32_t& visible) {
    visible = 0;
    if (length == 0) return 0;
    const std::uint16_t* units = nullptr;
    std::uint32_t count = 0;
    GetTextAsUnicode(text, length, units, count, 3, false);
    const std::uint16_t* last = units + (count - 1);
    std::uint32_t remaining = count;
    while (last >= units && IsBreakingSpace(*last)) {
        --remaining;
        --last;
    }
    if (remaining == 0) return 0;
    const int savedCodePage = codePage_;
    codePage_ = kCodePageUTF16;
    const int width = GetStringWidth(units, remaining);
    codePage_ = savedCodePage;
    if ((savedCodePage | 0x10) != 0x10) {
        // Kept: the original converts the glyph count in *visible (still 0 here), not the trimmed length.
        remaining = static_cast<std::uint32_t>(LengthFromGlyphCount(text, visible, savedCodePage));
    }
    visible = remaining;
    return width;
}

// 0x1002be6d8
std::uint32_t cGZFont::CalculateCharsToFitInWidth(const void* text, std::uint32_t length, std::uint32_t width,
                                                  std::uint32_t* fitted) {
    std::uint32_t consumed = 0;
    std::uint32_t fittedWidth = 0;
    if (length != 0) {
        std::uint32_t previousConsumed = 0;
        std::uint32_t previousWidth = 0;
        std::uint32_t current = 0;
        std::uint32_t used = 0;
        if (width != 0) {
            do {
                previousWidth = current;
                previousConsumed = used;
                const std::size_t byteOffset = (codePage_ == kCodePageUTF16) ? static_cast<std::size_t>(used) * 2 : used;
                used += static_cast<std::uint32_t>(GetCharLength(static_cast<const std::uint8_t*>(text) + byteOffset, length - used, codePage_));
                current = static_cast<std::uint32_t>(GetStringWidth(text, used));
            } while (used < length && current < width);
        }
        if (current <= width) {
            consumed = used;
            fittedWidth = current;
        } else {
            consumed = previousConsumed;
            fittedWidth = previousWidth;
        }
    }
    if (fitted != nullptr) *fitted = fittedWidth;
    return consumed;
}

// 0x1002be3ca: how many source units of `text` fit on one line of `width` pixels. mode 2 (what every internal caller
// passes) may break inside a word, mode 1 returns the first word whole, mode 0 returns nothing when the first word
// does not fit. The cIGZFont::WordWrapMode names do not match these values (0 / 1 / 2 here).
std::uint32_t cGZFont::CalculateWordsToFitInWidth(const void* text, std::uint32_t length, std::uint32_t width,
                                                  std::uint32_t* fitted, WordWrapMode wrapMode) {
    const int mode = static_cast<int>(wrapMode);
    if (width == 0 || length == 0) {
        if (fitted != nullptr) *fitted = 0;
        return 0;
    }
    const std::uint16_t* units = nullptr;
    std::uint32_t count = 0;
    GetTextAsUnicode(text, length, units, count, 2, true);
    const int savedCodePage = codePage_;
    codePage_ = kCodePageUTF16;

    std::uint32_t stop = 0;      // number of UTF-16 units that fit
    std::uint32_t total = count;
    bool lineFound = false;      // true: the main scan produced a line (otherwise the first word is too wide)
    if (count != 0) {
        std::uint32_t accumulated = 0;
        std::uint32_t index = 0;
        const std::uint16_t* p = units;
        bool newline = false;
        do {
            if (*p == 0xd || *p == 10) {
                index += 1;
                newline = true;
                break;
            }
            const std::uint32_t before = index;
            const int word = UTF16GetWord(p, count - index);
            std::uint32_t visibleLength;
            const std::uint32_t visibleWidth = static_cast<std::uint32_t>(GetStringVisibleWidth(p, word, visibleLength));
            index = before;
            if (width < visibleWidth + accumulated) break;
            const int wordWidth = GetStringWidth(p, word);
            index = before + word;
            if (width < visibleWidth) break;   // (cannot trigger after the test above; kept)
            p += word;
            accumulated += wordWidth;
        } while (index < count);
        stop = index;
        lineFound = newline || index != 0;
    }
    if (!lineFound) {
        // The first word is wider than the line.
        if (mode == 1) {
            if (count != 0) {
                std::uint32_t i = 0;
                do {
                    const std::uint32_t c = units[i];
                    if ((c < 0xe && ((0x2401u >> c) & 1)) || c == 0x20 || (c - 0x2002) < 10) break;
                    i += static_cast<std::uint32_t>(UTF16GetWord(units + i, count - i));
                } while (i < count);
                stop = i;
            }
        } else if (mode == 2) {
            std::uint32_t before = 0;
            std::uint32_t after = 0;
            do {
                before = after;
                if (before >= count) { after = before; break; }   // bounded (the original would read past the text)
                unsigned step = 1;
                if (UTF16IsThaiChar(units[before])) step = UTF16ThaiCharGroupLength(units + before, count - before);
                after = before + step;
            } while (static_cast<std::uint32_t>(GetStringWidth(units, after)) < width);
            stop = (before != 0) ? before : after;
            if (stop < count && !substitutionMap_.empty()) {
                stop += (substitutionMap_[stop - 1] == substitutionMap_[stop]) ? 1 : 0;
            }
        } else {
            stop = 0;
            total = 0;   // mode 0: nothing fits; no space adjustment
        }
    }
    // A line that ends just before a space takes the space along.
    if (stop > 1 && stop < total) {
        if (!IsBreakingSpace(units[stop - 1])) stop += IsBreakingSpace(units[stop]) ? 1 : 0;
    }
    codePage_ = savedCodePage;
    std::uint32_t glyphs = stop;
    if (!substitutionMap_.empty()) glyphs = substitutionMap_[stop];
    const std::uint32_t result = static_cast<std::uint32_t>(LengthFromGlyphCount(text, glyphs, savedCodePage));
    if (fitted != nullptr) *fitted = static_cast<std::uint32_t>(GetStringWidth(text, result));
    return result;
}

// 0x1002be7a6
std::uint32_t cGZFont::CalculateNumberOfLines(const void* text, std::uint32_t length, std::uint32_t width) {
    if (width == 0) width = 1;
    const std::size_t bytes = (codePage_ == kCodePageUTF16) ? static_cast<std::size_t>(length) * 2 : length;
    const auto* end = static_cast<const std::uint8_t*>(text) + bytes;
    const auto* p = static_cast<const std::uint8_t*>(text);
    std::uint32_t lines = 0;
    while (p < end) {
        const std::uint32_t used = CalculateWordsToFitInWidth(p, length, width, nullptr, static_cast<WordWrapMode>(2));
        p += (codePage_ == kCodePageUTF16) ? static_cast<std::size_t>(used) * 2 : used;
        length -= used;
        ++lines;
    }
    return lines;
}

// 0x1002be848: the narrowest width (a multiple of 10 pixels, 20..4000) at which `text` fits in `height` pixels.
std::uint32_t cGZFont::CalculateWidthOfLines(const void* text, std::uint32_t length, std::uint32_t height) {
    const std::uint32_t lineHeight = static_cast<std::uint32_t>(GetLineHeight());
    std::uint32_t high = 400;
    std::uint32_t low = 2;
    do {
        const std::uint32_t mid = (low + high) >> 1;
        const std::uint32_t lines = CalculateNumberOfLines(text, length, mid * 10);
        if (height / lineHeight < lines) {
            low = mid + 1;
        } else {
            const bool converged = low == high;
            high = mid;
            if (converged) return low * 10;
        }
    } while (low <= high);
    return 0;
}

// 0x1002be8dc
bool cGZFont::CalculateTextArea(const void* text, std::uint32_t length, cRZRect& area, bool wrapWidth, bool wrapHeight) {
    if (text == nullptr || length == 0) {
        area.right = area.left;
        area.bottom = area.top + GetLineHeight();
        return true;
    }
    if (wrapWidth) {
        if (!wrapHeight) {
            area.right = static_cast<std::int32_t>(CalculateWidthOfLines(text, length, static_cast<std::uint32_t>(area.bottom - area.top))) + area.left;
            return true;
        }
        const int lineHeight = GetLineHeight();
        const std::uint32_t lines = CalculateNumberOfLines(text, length, static_cast<std::uint32_t>(area.right - area.left));
        area.bottom = static_cast<std::int32_t>(lines) * lineHeight + area.top;
        return true;
    }
    area.right = area.left + GetStringWidth(text, length);
    area.bottom = area.top + GetLineHeight();
    return true;
}

// 0x1002be9b2: the single-line box of the text at the origin.
bool cGZFont::TextArea(const void* text, std::uint32_t length, cRZRect& area) {
    const int height = CharHeight(nullptr, 0);
    area.left = 0;
    area.top = 0;
    area.right = 0;
    area.bottom = height;
    area.right = GetStringWidth(text, length);
    return true;
}

// 0x1002bea0c: fits `text` into a box of at least `minWidth` and at most `maxWidth` x `maxHeight` pixels, trying 1, 2,
// 3, ... lines. -1 for either width picks 12 / 32 times the width of an 'a'. Returns false when it does not fit.
bool cGZFont::CalculateTextArea(const void* text, std::uint32_t length, cRZRect& area, int minWidth, int maxWidth, int maxHeight) {
    const int lineHeight = GetLineHeight();
    int heightLimit = (maxHeight <= lineHeight) ? lineHeight : maxHeight;
    int low = minWidth;
    int high = maxWidth;
    if (minWidth == -1 || maxWidth == -1) {
        const char a = 'a';
        const int unit = CharWidth(&a, 1);
        const int derivedLow = (minWidth != -1) ? minWidth : unit * 12;
        const int derivedHigh = (maxWidth != -1) ? maxWidth : unit << 5;
        low = derivedLow;
        high = (derivedLow < derivedHigh) ? derivedHigh : derivedLow * 2;
    }

    int widthMeasure = 0;   // the value the multi-line search starts from
    bool search = false;
    bool hasNewline = false;
    if (length != 0) {
        const auto* p = static_cast<const std::uint8_t*>(text);
        const auto* end = p + length;
        do {
            std::uint32_t used = 0;
            const std::uint32_t c = ConvertCharToUnicode(p, length, &used);   // kept: `length`, not the remaining count
            if (c == 10) { hasNewline = true; break; }
            p += used;
        } while (p < end);
    }
    int rowsTried = 0;
    if (hasNewline) {
        widthMeasure = 1;
        search = true;
    } else {
        widthMeasure = GetStringWidth(text, length);
        if (widthMeasure < low) {
            area.right = area.left + widthMeasure;
            area.bottom = area.top + lineHeight;
            return true;
        }
        if (low * 2 <= widthMeasure) {
            if (high <= widthMeasure) search = true;
            else {
                area.right = area.left + widthMeasure;
                area.bottom = area.top + lineHeight;
                return true;
            }
        } else {
            const int twoLines = lineHeight * 2;
            const std::uint32_t w = CalculateWidthOfLines(text, length, static_cast<std::uint32_t>(twoLines));
            if (heightLimit >= twoLines && w < static_cast<std::uint32_t>(high)) {
                area.right = static_cast<std::int32_t>(w) + area.left;
                area.bottom = twoLines + area.top;
                return true;
            }
            // two lines are not allowed or too wide
            if (high <= widthMeasure) search = true;
            else {
                area.right = area.left + widthMeasure;
                area.bottom = area.top + lineHeight;
                return true;
            }
        }
    }
    if (search) {
        const int maxLines = heightLimit / lineHeight;
        rowsTried = widthMeasure / high + 1;
        if (rowsTried < maxLines) {
            int trialHeight = lineHeight * rowsTried;
            do {
                const std::uint32_t w = CalculateWidthOfLines(text, length, static_cast<std::uint32_t>(trialHeight));
                if (trialHeight <= heightLimit && w < static_cast<std::uint32_t>(high) && w != 0) {
                    area.right = static_cast<std::int32_t>(w) + area.left;
                    area.bottom = trialHeight + area.top;
                    return true;
                }
                ++rowsTried;
                trialHeight += lineHeight;
            } while (rowsTried < maxLines);
        }
        area.right = area.left;
        area.bottom = area.top;
        return false;
    }
    return false;
}

// 0x1002bed8e: shortens `text` so it fits `area`'s width, ending in `ellipsis`. Returns the original length (kept:
// the interface declares a bool, the original returns the unsigned length).
std::uint32_t cGZFont::FormatForEllipsisLength(cIGZString& text, const cIGZString& ellipsis, const cRZRect& area, bool skip) {
    const std::uint32_t length = text.Strlen();
    if (!skip) {
        const int width = area.right - area.left;
        const std::uint32_t fits = CalculateCharsToFitInWidth(text.Data(), length, static_cast<std::uint32_t>(width), nullptr);
        if (fits < length) {
            const int ellipsisWidth = GetStringWidth(ellipsis.Data(), ellipsis.Strlen());
            if (width - ellipsisWidth == 0 || width < ellipsisWidth) {
                text.Resize(0);
            } else {
                const std::uint32_t keep = CalculateCharsToFitInWidth(text.Data(), length, static_cast<std::uint32_t>(width - ellipsisWidth), nullptr);
                text.Resize(keep);
                while (text.Strlen() != 0 && text.Data()[text.Strlen() - 1] == ' ') text.Resize(text.Strlen() - 1);
                text.AppendChars(ellipsis.Data(), ellipsis.Strlen());
            }
        }
    }
    return length;
}

bool cGZFont::FormatForEllipsis(cIGZString& out, const cIGZString& in, const cRZRect& area, bool a) {
    return FormatForEllipsisLength(out, in, area, a) != 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Palette (mode 2)
// ---------------------------------------------------------------------------------------------------------------
// 0x1002bd554: 16 groups of 8 entries blending from the background to the foreground. Palettes are cached by
// (foreground, background), four of them, most recently used first.
void cGZFont::SetCurrentPalette(std::uint32_t foreground, std::uint32_t background) {
    if (currentPalette_ != nullptr && currentPalette_[0x7f] == foreground && currentPalette_[0] == background) return;
    for (auto it = paletteCache_.begin(); it != paletteCache_.end(); ++it) {
        std::uint32_t* candidate = it->get();
        if (candidate[0x7f] == foreground && candidate[0] == background) {
            if (it != paletteCache_.begin()) paletteCache_.splice(paletteCache_.begin(), paletteCache_, it);
            currentPalette_ = candidate;
            return;
        }
    }
    // Quirk: the original allocates 128 entries but the renderers index the table with the 8-bit coverage; the entries
    // above 127 are zero here (they were heap garbage there).
    auto palette = std::make_unique<std::uint32_t[]>(kPaletteEntries);
    currentPalette_ = palette.get();
    std::uint8_t fr, fg, fb, br, bg, bb;
    host_->NativeColorToRGB(foreground & 0xffff, fr, fg, fb);   // RZ::GS() vslot 0x120
    host_->NativeColorToRGB(background & 0xffff, br, bg, bb);
    for (int i = 0; i < 8; ++i) palette[i] = background;
    for (int step = 0; step < 15; ++step) {
        auto channel = [step](int from, int to) {
            const int delta = (from - to) * step;
            return static_cast<std::uint8_t>(((delta + (delta < 0 ? 15 : 0)) >> 4) + to);
        };
        const std::uint32_t native = host_->RGBToNativeColor(channel(fr, br), channel(fg, bg), channel(fb, bb));   // vslot 0x110
        for (int i = 0; i < 8; ++i) palette[(step + 1) * 8 + i] = native;
    }
    for (int i = 0; i < 8; ++i) palette[0x78 + i] = foreground;   // the last group is the foreground itself
    if (paletteCache_.size() > 3) paletteCache_.pop_back();
    paletteCache_.push_front(std::move(palette));
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------------------------
// 0x1002bef08
bool cGZFont::DrawTextLine(void* surface, int x, int y, const void* text, std::uint32_t length, std::uint32_t color,
                           std::uint32_t background, const cRZRect* clip, cRZPoint* end) {
    if (length == 0) return initialized_;
    if (!initialized_) return false;
    const std::uint16_t* units = nullptr;
    std::uint32_t count = 0;
    GetTextAsUnicode(text, length, units, count, 1, false);
    CachedBitmapData* cache = GetAppropriateCachedBitmapData();
    int mode = antialiasing_;
    if (mode == 2) {
        std::uint32_t paletteBackground = background;
        if (background == kFontDefaultColor) {
            mode = paletteMode_;
            paletteBackground = kFontDefaultColor;
            if (paletteMode_ == 2) {
                paletteBackground = (color == 0) ? 0xffffffffu : 0u;   // black text gets a white background
                mode = 2;
            }
        }
        SetCurrentPalette(color, paletteBackground);   // kept: the default colour is not resolved here
    }
    if (color == kFontDefaultColor) color = color_;
    int penX = x;
    const auto rendererMode = static_cast<cIGZFontRenderer::eRenderingMode>(NormalisedMode(mode));
    bool lockedHere = false;
    if (surface != nullptr && lockCount_ == 0) {
        if (!renderer_->DrawBegin(this, surface, rendererMode)) return false;
        ++lockCount_;
        lockedHere = true;
    }
    int penY = y;
    if (end != nullptr) {
        penX = end->x;
        penY = end->y;
    }
    if (glyphRun_.size() < count) glyphRun_.resize(count);
    // Fetch every glyph first: a late atlas growth would invalidate pixel pointers taken earlier.
    for (std::uint32_t i = 0; i < count; ++i) GetCharData(units[i]);

    std::uint32_t drawn = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t character = units[i];
        CachedBitmapDataDetail* d = GetCharData(character);
        if (d == nullptr) {
            character = defaultChar_;
            d = GetCharData(character);
            if (d == nullptr) continue;
        }
        const int glyphWidth = d->x1 - d->x0;
        const int glyphHeight = d->y1 - d->y0;
        const int left = d->xOffset + penX;
        const int top = d->yOffset + penY + spacingY_;
        bool visible = true;
        if (clip != nullptr) {
            if (left < clip->right && top < clip->bottom && clip->left < left + glyphWidth) visible = !(top + glyphHeight <= clip->top);
            else visible = false;
        }
        if (visible && glyphHeight != 0 && glyphWidth != 0) {
            cIGZFontRenderer::sGlyphInfo& g = glyphRun_[drawn];
            g.left = left;
            g.top = top;
            g.right = left + glyphWidth;
            g.bottom = top + glyphHeight;
            g.pixels = cache->pixels + static_cast<std::ptrdiff_t>(d->y0) * cache->pitch + d->x0;
            g.pitch = cache->pitch;
            g.width = static_cast<std::uint32_t>(glyphWidth);
            g.height = static_cast<std::uint32_t>(glyphHeight);
            g.character = character;
            ++drawn;
        }
        penX += static_cast<std::int16_t>(d->advance) + spacingX_;
    }
    if (surface != nullptr && drawn != 0) {
        const std::uint32_t* colors = (mode == 2) ? currentPalette_ : &color;
        renderer_->DrawChars(this, surface, glyphRun_.data(), drawn, static_cast<cIGZFontRenderer::eRenderingMode>(NormalisedMode(mode)), colors, clip);
    }
    if (end != nullptr) {
        end->x = penX;
        end->y = penY;
    }
    if (lockedHere) {
        renderer_->DrawEnd(this, surface, static_cast<cIGZFontRenderer::eRenderingMode>(NormalisedMode(mode)));
        --lockCount_;
    }
    return true;
}

// 0x1002bf308: wraps `text` into `area` line by line (alignment 0 left, 1 centre, 2 right).
bool cGZFont::DrawTextParagraph(void* surface, const cRZRect& area, const void* text, std::uint32_t length, int alignment,
                                std::uint32_t color, std::uint32_t background, const cRZRect* clip, cRZPoint* end) {
    bool result = initialized_;
    // Kept on purpose: the copyright line of the splash / about text is rewritten in place to 2014 (the caller's buffer is
    // modified), and for such a text a top edge of exactly 2 is treated as 0.
    char* stamp = std::strstr(const_cast<char*>(static_cast<const char*>(text)), "2003 Electronic Arts");
    if (stamp != nullptr) std::memcpy(stamp, "2014 Electronic Arts", 0x14);
    if (length == 0 || !initialized_) return result;

    const std::uint16_t* units = nullptr;
    std::uint32_t count = 0;
    GetTextAsUnicode(text, length, units, count, 0, false);
    const int savedCodePage = codePage_;
    codePage_ = kCodePageUTF16;
    bool lockedHere = false;
    if (surface != nullptr && lockCount_ == 0) {
        if (!renderer_->DrawBegin(this, surface, static_cast<cIGZFontRenderer::eRenderingMode>(NormalisedMode(antialiasing_)))) {
            codePage_ = savedCodePage;
            return false;
        }
        ++lockCount_;
        lockedHere = true;
    }
    int y = area.top;
    if (stamp != nullptr && area.top == 2) y = 0;
    int x = area.left;
    const int lineHeight = GetLineHeight();
    const std::uint32_t available = static_cast<std::uint32_t>(area.right - area.left);
    if (available != 0 && area.left <= area.right) {
        std::uint32_t remaining = count;
        if (end != nullptr && end->x != area.left) x = end->x;
        cRZPoint pen{end ? end->x : 0, end ? end->y : 0};
        cRZPoint* penPointer = nullptr;
        if (y < area.bottom) {
            const std::uint16_t* cursor = units;
            const std::uint16_t* limit = units + count;
            const std::uint32_t half = available >> 1;
            do {
                if (limit <= cursor) break;
                const std::uint32_t used = CalculateWordsToFitInWidth(cursor, remaining, available, nullptr, static_cast<WordWrapMode>(2));
                if (alignment == 2) x = area.right - GetStringWidth(cursor, used);
                else if (alignment == 1) x = area.left + static_cast<int>(half) - GetStringWidth(cursor, used) / 2;
                else if (alignment == 0) x = area.left;
                if (end != nullptr) {
                    pen.x = x;
                    pen.y = y;
                    penPointer = &pen;
                }
                if (surface != nullptr) {
                    std::uint32_t drawLength = used;
                    while (drawLength > 0 && (cursor[drawLength - 1] == 0xd || cursor[drawLength - 1] == 10)) --drawLength;   // trailing line breaks
                    if (drawLength != 0) DrawTextLine(surface, x, y, cursor, drawLength, color, background, clip, penPointer);
                }
                cursor += used;
                remaining -= used;
                y += lineHeight;
            } while (y < area.bottom);
        }
        if (end != nullptr) *end = pen;
    }
    if (lockedHere) {
        renderer_->DrawEnd(this, surface, static_cast<cIGZFontRenderer::eRenderingMode>(NormalisedMode(antialiasing_)));
        --lockCount_;
    }
    codePage_ = savedCodePage;
    return true;
}

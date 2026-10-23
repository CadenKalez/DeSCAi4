#include "gz/FontSys.h"

#include <algorithm>
#include <cstring>

#include "gz/FontRenderer.h"
#include "gz/GZFont.h"
#include "rz/RZStringUtils.h"
#include "rz/StringConversion.h"

namespace {
constexpr std::uint32_t kStyleMaskKeep = 0xffffffc3;   // FontAcquire keeps only the bits outside 0x3c
}

cRZString cGZFontSys::Lower(const cIGZString& s) {
    cRZString copy(s.ToChar());
    copy.MakeLower();
    return copy;
}

cGZFontSys::cGZFontSys(IFontSysHost* host) : host_(host) {
    // 0x100282440: the four default file filters, then the code page of the running framework.
    for (const char* filter : {"*.tt?", "*.pfb", "*.mxf", "*.otf"}) filters_.emplace_back(filter);
    defaultCodePage_ = host_->GetCurrentFrameworkCodePage();
}

cGZFontSys::~cGZFontSys() = default;

bool cGZFontSys::QueryInterface(std::uint32_t iid, void** out) {
    // 0x1002833ae (the service-interface thunk 0x1002833e2 enters the same body with this - 8)
    if (iid == kGZIID_cIGZSystemService) {
        *out = static_cast<cIGZSystemService*>(this);
    } else if (iid == kGZIID_cIGZUnknown || iid == kGZIID_cIGZFontSys) {
        *out = static_cast<cIGZFontSys*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

std::uint32_t cGZFontSys::AddRef() { return ++refCount_; }

std::uint32_t cGZFontSys::Release() {
    if (refCount_ < 2) {   // FONTSYS-1
        Shutdown();
        delete this;
        return 0;
    }
    return --refCount_;
}

bool cGZFontSys::FontSetName(int genericId, const cIGZString& name) {
    genericNames_[genericId] = cRZString(name.ToChar());
    return true;
}

bool cGZFontSys::Init() {
    if (!initialized_) {
        // The four stock generic names (5 serif, 9 sans, 6 fixed pitch, 10 console), set once.
        FontSetName(5, cRZString("Times New Roman"));
        FontSetName(9, cRZString("Arial"));
        FontSetName(6, cRZString("Courier New"));
        FontSetName(10, cRZString("Lucida Console"));
        initialized_ = true;
    }
    return true;
}

bool cGZFontSys::Shutdown() {
    if (initialized_) {
        for (auto& [path, info] : fonts_) {
            for (FontEntry& entry : info.entries) {
                if (entry.font) {
                    // The original logs a font that somebody still holds, then drops the registry's own reference.
                    if (entry.font->Release() > 0) host_->LogLiveFontAtShutdown(path);
                }
            }
            info.entries.clear();
        }
        fonts_.clear();
        installed_.clear();
        genericNames_.clear();
        substitutions_.clear();
        ranges_.clear();
        if (defaultRenderer_) {
            defaultRenderer_->Release();
            defaultRenderer_ = nullptr;
        }
        initialized_ = false;
    }
    return true;
}

bool cGZFontSys::SetDefaultCodePage(int codePage) {
    defaultCodePage_ = codePage;
    return true;
}

void cGZFontSys::SetDefaultFontRenderer(cIGZFontRenderer* renderer) {
    if (renderer) renderer->AddRef();
    if (defaultRenderer_) defaultRenderer_->Release();
    defaultRenderer_ = renderer;
}

void cGZFontSys::SetDefaultFontAntialiasing(int mode) { defaultAntialiasing_ = mode; }

void cGZFontSys::SetDefaultUnantialiasedSizeRange(int min, int max, const cIGZString* name) {
    if (!name) {
        defaultMin_ = static_cast<std::uint32_t>(min);
        defaultMax_ = static_cast<std::uint32_t>(max);
        return;
    }
    cRZString key(name->ToChar());   // FONTSYS-5: not lower-cased
    if (max <= min) {                // signed comparison
        ranges_.erase(key);
        return;
    }
    // FONTSYS-4: an existing entry is left alone.
    ranges_.emplace(key, MinMax{static_cast<std::uint32_t>(min), static_cast<std::uint32_t>(max)});
}

std::uint32_t cGZFontSys::GetFontSourceTypeFromFileName(const cIGZString& path) {
    cRZString extension;
    host_->GetPathExtension(path, extension);
    extension.MakeLower();
    const std::string ext = extension.ToChar();
    if (ext == ".ttf") return 1;
    if (ext == ".ttc" || ext == ".otf") return 4;
    if (ext == ".pfb") return 2;
    // Unknown extension: look at the first 32 bytes.
    char head[0x20] = {};
    if (!host_->ReadFileHeader(path, head, sizeof head)) return 0;
    const std::string bytes(head, sizeof head);
    if (bytes.find("%!PS") != std::string::npos) return 2;     // Type 1 text header anywhere in the block
    static const char kTrueType[4] = {0, 1, 0, 0};
    if (bytes.compare(0, 4, kTrueType, 4) == 0) return 1;
    return bytes.find("ttcf") == 0 ? 4 : 0;                    // a collection only counts at offset 0
}

bool cGZFontSys::AddFont(const cIGZString& pathArg) {
    const cRZString path(pathArg.ToChar());
    if (fonts_.count(path)) return false;
    if (!host_->FileExists(path)) return false;

    cRZString utf8;
    ConvertStringEncoding(path, utf8, -1);
    auto face = host_->Rasterizer()->NewFace(utf8.ToChar());
    if (!face) return false;

    cRZString extension;
    if (host_->GetPathExtension(path, extension)) {
        extension.MakeLower();
        const std::string ext = extension.ToChar();
        if (ext == ".pfb" || ext == ".pfa") {
            // Same sibling-metrics attach as cGZFont::Init: .afm first, .pfm only if that did not attach.
            const std::string base(path.ToChar(), path.Strlen() - ext.size());
            cRZString metrics((base + ".afm").c_str());
            if (host_->FileExists(metrics)) {
                cRZString metricsUtf8;
                ConvertStringEncoding(metrics, metricsUtf8, -1);
                if (!face->AttachFile(metricsUtf8.ToChar())) {
                    cRZString fallback((base + ".pfm").c_str());
                    if (host_->FileExists(fallback)) {
                        ConvertStringEncoding(fallback, metricsUtf8, -1);
                        face->AttachFile(metricsUtf8.ToChar());
                    }
                }
            }
        }
    }

    FontFixedInfo info;
    info.name.AssignFromCString(face->FamilyName().c_str());
    info.style = FontStyleFlagsFromFace(face->StyleFlags(), face->StyleName());    // FT_GZFont_GetStyleFlags
    info.type = ((face->FaceFlags() >> 2) & 1) + 1;
    face.reset();                                                                  // FT_Done_Face
    info.sourceType = GetFontSourceTypeFromFileName(path);

    installed_.insert(Lower(info.name));
    fonts_.emplace(path, std::move(info));
    return true;
}

int cGZFontSys::AddAllFontsFromDirectory(const cIGZString& directoryArg) {
    cRZString directory(directoryArg.ToChar());
    if (directory.Strlen() == 0) return 0;
    if (directory.ToChar()[directory.Strlen() - 1] != '/') directory.Strcat("/");
    int count = 0;
    for (const cRZString& filter : filters_) {
        std::list<cRZString> names;
        host_->ListDirectoryFiles(directory, filter, names);
        for (const cRZString& name : names) {
            const cRZString full((std::string(directory.ToChar()) + name.ToChar()).c_str());
            if (std::strstr(full.ToChar() + directory.Strlen(), ".PFB") != nullptr) ++count;   // FONTSYS-2
            if (AddFont(full)) ++count;
        }
    }
    return count;
}

void cGZFontSys::RegisterFont(cIGZFont* font, const cRZString& path, std::uint32_t flags, bool) {
    auto it = fonts_.find(path);
    if (it != fonts_.end()) {
        it->second.entries.push_back({font, flags});
        return;
    }
    FontFixedInfo info;
    font->GetName(info.name);
    info.style = font->GetStyle();
    info.type = font->GetType();
    info.sourceType = 0;
    info.entries.push_back({font, flags});
    installed_.insert(Lower(info.name));
    fonts_.emplace(path, std::move(info));
}

bool cGZFontSys::AddFont(cIGZFont* font) {
    cRZString path;
    font->GetFilePath(path);
    auto it = fonts_.find(path);
    if (it != fonts_.end()) {
        for (const FontEntry& entry : it->second.entries) {
            if (entry.font->GetSize() == font->GetSize()) return false;
        }
        font->AddRef();
        it->second.entries.push_back({font, font->GetStyle()});
        return true;
    }
    font->AddRef();
    RegisterFont(font, path, font->GetStyle(), false);
    return true;
}

bool cGZFontSys::RemoveFontByFile(const cIGZString&) { return false; }   // FONTSYS-3

bool cGZFontSys::RemoveFontByName(const cIGZString& name) {
    installed_.erase(Lower(name));
    for (auto it = fonts_.begin(); it != fonts_.end();) {
        if (it->second.name.IsEqualToStringRef(name, false)) {
            for (FontEntry& entry : it->second.entries) entry.font->Release();
            it = fonts_.erase(it);
        } else {
            ++it;
        }
    }
    return false;   // FONTSYS-3
}

std::uint32_t cGZFontSys::EnumerateFontInfo(FontInfo* out, std::uint32_t count) {
    const std::uint32_t total = static_cast<std::uint32_t>(fonts_.size());
    if (count == 0) return total;
    const std::uint32_t written = std::min(total, count);
    std::uint32_t i = 0;
    for (const auto& [path, info] : fonts_) {
        if (i >= written) break;
        std::memset(&out[i], 0, sizeof(FontInfo));
        std::strncpy(out[i].name, info.name.ToChar(), 0x7f);
        out[i].style = static_cast<std::int32_t>(info.style);
        out[i].type = static_cast<std::int32_t>(info.type);
        ++i;
    }
    return written;
}

std::uint32_t cGZFontSys::EnumerateManagedFonts(cIGZFont** out, std::uint32_t count) {
    std::uint32_t total = 0;
    for (const auto& [path, info] : fonts_) {
        for (const FontEntry& entry : info.entries) {
            if (count == 0) {
                ++total;
            } else if (total < count) {
                out[total++] = entry.font;
            }
        }
    }
    return total;
}

bool cGZFontSys::AddSubstitution(const cIGZString& from, const cIGZString& to) {
    substitutions_[cRZString(from.ToChar())].emplace_back(to.ToChar());
    return true;
}

std::uint32_t cGZFontSys::EnumerateFontSubstitutions(const cIGZString& name, cIGZString** out, std::uint32_t count) {
    auto it = substitutions_.find(cRZString(name.ToChar()));
    if (it == substitutions_.end()) return 0;
    const std::uint32_t n = std::min<std::uint32_t>(static_cast<std::uint32_t>(it->second.size()), count);   // FONTSYS-7
    for (std::uint32_t i = 0; i < n; ++i) {
        if (out[i]) out[i]->AssignFromChars(it->second[i].ToChar(), it->second[i].Strlen());
    }
    return n;
}

bool cGZFontSys::IsFontAvailable(const cIGZString& name, std::uint32_t, int style) {
    const cRZString lower = Lower(name);
    if (!installed_.count(lower)) return false;
    for (const auto& [path, info] : fonts_) {
        if (info.style == (static_cast<std::uint32_t>(style) & 3) && Lower(info.name).IsEqualToStringRef(lower, true)) return true;
    }
    return false;
}

bool cGZFontSys::IsSubstitutionAvailable(const cIGZString& name, cIGZString& substitute) {
    auto it = substitutions_.find(cRZString(name.ToChar()));
    if (it == substitutions_.end()) return false;
    for (const cRZString& candidate : it->second) {
        if (IsFontAvailable(candidate, 0, 0)) {
            substitute.AssignFromChars(candidate.ToChar(), candidate.Strlen());
            return true;
        }
    }
    return false;
}

cIGZFont* cGZFontSys::FontAcquire(const cIGZString& namesArg, int type, std::uint32_t size, int style, bool reuse) {
    cRZString list(namesArg.ToChar());
    list.MakeLower();
    cRZString names[32];
    unsigned nameCount = 32;
    RZParseDelimitedText(list.ToChar(), list.Strlen(), ',', names, nameCount);
    const std::uint32_t styleMask = static_cast<std::uint32_t>(style) & kStyleMaskKeep;

    // Pass 1: replace a requested name that is a substitute (and not installed) by the name it stands in for.
    for (int i = static_cast<int>(nameCount) - 1; i >= 0; --i) {
        for (const auto& [key, subs] : substitutions_) {
            for (const cRZString& sub : subs) {
                if (names[i].IsEqualToStringRef(sub, false)) {
                    if (!installed_.count(Lower(names[i]))) {
                        names[i] = key;
                        names[i].MakeLower();
                        break;
                    }
                }
            }
        }
    }

    // Pass 2: score every registered file; the lowest score wins and the first of equals is kept.
    const cRZString* bestKey = nullptr;
    FontFixedInfo* best = nullptr;
    int bestScore = 1000;
    for (auto& [key, info] : fonts_) {
        int score;
        if (nameCount == 0) {
            score = (info.type != static_cast<std::uint32_t>(type)) ? 4 : 0;
        } else {
            const cRZString lower = Lower(info.name);
            unsigned k = 0;
            while (k < nameCount && !names[k].IsEqualToStringRef(lower, true)) ++k;
            score = (k == nameCount) ? 0x20 : (k > 0 ? 2 : 0);
        }
        if (info.style != styleMask) score += ((~styleMask & info.style) == 0) ? 8 : 0x10;
        if (score < bestScore) {
            bestScore = score;
            best = &info;
            bestKey = &key;
        }
    }
    if (!best) return nullptr;
    if (reuse) {
        for (FontEntry& entry : best->entries) {
            if (entry.font->GetSize() == size && entry.flags == static_cast<std::uint32_t>(style)) {   // FONTSYS-8
                entry.font->AddRef();
                return entry.font;
            }
        }
    }
    const cRZString path = *bestKey;
    return FontAcquire(path, size, style, reuse);
}

cIGZFont* cGZFontSys::FontAcquire(int type, std::uint32_t size, int style, std::uint32_t color, bool reuse) {
    cIGZFont* font = FontAcquire(cRZString(), type, size, style, reuse);
    if (font) font->SetFontColor(color, 0x8000000);
    return font;
}

cIGZFont* cGZFontSys::FontAcquire(const cIGZString& pathArg, std::uint32_t size, int style, bool reuse) {
    const cRZString path(pathArg.ToChar());
    cIGZFont* font = nullptr;
    if (size == 0 || !host_->FileExists(path)) return nullptr;

    if (reuse) {
        for (auto& [key, info] : fonts_) {
            if (!key.IsEqualToStringRef(path, true)) continue;
            for (FontEntry& entry : info.entries) {
                if (entry.font->GetSize() == size && entry.flags == static_cast<std::uint32_t>(style)) {
                    font = entry.font;
                    break;
                }
            }
        }
    }

    if (!font) {
        font = host_->CreateFontObject();
        if (!font) {
            font = new cGZFont(host_);
            font->AddRef();
        }
        font->SetFilePath(path);
        font->SetSize(size);
        font->SetStyle(style, true);
        font->SetCodePage(defaultCodePage_);
        font->SetRenderer(defaultRenderer_);

        int antialiasing = defaultAntialiasing_;
        if (size >= defaultMin_ && size <= defaultMax_) {
            antialiasing = 1;   // FONTSYS-6
        } else {
            cRZString fontName;
            font->GetName(fontName);
            auto range = ranges_.find(fontName);
            if (range != ranges_.end() && range->second.min <= size && size <= range->second.max) antialiasing = 1;
        }
        font->SetAntialiasing(antialiasing);
        if (!font->Init()) {
            font->Release();
            return nullptr;
        }
        if (reuse) RegisterFont(font, path, static_cast<std::uint32_t>(style), true);
    }

    font->AddRef();
    if (!reuse) font->Release();
    return font;
}

int cGZFontSys::FontRelease(cIGZFont* font) {
    int result = 0;
    for (auto& [path, info] : fonts_) {
        for (auto it = info.entries.begin(); it != info.entries.end(); ++it) {
            if (it->font == font) {
                result = static_cast<int>(font->Release());
                if (result == 1) {   // FONTSYS-9
                    result = static_cast<int>(font->Release());
                    info.entries.erase(it);
                }
                break;
            }
        }
    }
    return result;
}

// cGZFontSys test: registry, source type sniffing, directory scan, substitutions, scoring and acquire/release.
#include <cstdio>
#include <cstring>
#include <functional>
#include <list>
#include <map>
#include <set>

#include "gz/FontSys.h"
#include "gz/GZFont.h"
#include "gz/FontRenderer.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {

struct FakeGlyph : IFontGlyph {
    bool bitmap = false;
    bool convertOk = true;
    int grays = 256;
    FontBitmap bm;
    std::vector<std::uint8_t> data;
    std::int32_t advance = 6 << 16;
    bool IsBitmap() const override { return bitmap; }
    bool ConvertToBitmap(bool) override { bitmap = true; return convertOk; }
    std::int32_t AdvanceX() const override { return advance; }
    const FontBitmap& Bitmap() const override { return bm; }
};

struct FakeFace : IFontFace {
    FontFaceMetrics m;
    std::uint32_t faceFlags = 0, styleFlags = 0;
    std::string family = "Fake Sans", style = "Regular";
    std::set<std::uint32_t> missing;   // characters without glyph
    std::vector<std::uint32_t> charmapTries;
    std::set<std::uint32_t> okCharmaps{0x756e6963};
    std::vector<std::string> attached;
    bool attachOk = true;
    bool mono = false;
    std::int32_t transform[4] = {0, 0, 0, 0};
    bool transformSet = false;
    FakeFace() { m.bboxYMax = 800; m.bboxYMin = -200; m.ascender = 800; m.descender = -200; m.height = 1000; m.maxAdvanceWidth = 700; }
    bool AttachFile(const std::string& p) override { attached.push_back(p); return attachOk; }
    bool SelectCharmap(std::uint32_t t) override { charmapTries.push_back(t); return okCharmaps.count(t) != 0; }
    void SetTransform(const std::int32_t mat[4]) override { std::memcpy(transform, mat, 16); transformSet = true; }
    std::uint32_t GetCharIndex(std::uint32_t c) override { return missing.count(c) ? 0 : c + 1; }
    bool SetPixelSizes(std::uint32_t, std::uint32_t h) override {
        m.yScale = static_cast<std::int32_t>((static_cast<std::int64_t>(h) * 64 << 16) / 1000);
        m.xScale = m.yScale;
        return true;
    }
    std::unique_ptr<IFontGlyph> LoadGlyph(std::uint32_t idx) override {
        auto g = std::make_unique<FakeGlyph>();
        g->bm.left = 1; g->bm.top = 5; g->bm.rows = 6; g->bm.width = 4; g->bm.pitch = 4; g->bm.numGrays = 256;
        g->data.assign(24, static_cast<std::uint8_t>(0x80 + (idx & 0x3f)));
        g->bm.buffer = g->data.data();
        return g;
    }
    FontFaceMetrics& Metrics() override { return m; }
    std::uint32_t FaceFlags() const override { return faceFlags; }
    std::uint32_t StyleFlags() const override { return styleFlags; }
    std::string FamilyName() const override { return family; }
    std::string StyleName() const override { return style; }
};

struct FakeHost : IFontSysHost, IFontRasterizer {
    FakeFace* lastFace = nullptr;
    std::set<std::string> files{"/fonts/a.ttf", "/fonts/t1.pfb", "/fonts/t1.afm", "/fonts/t1.pfm"};
    std::map<std::string, std::string> headers;
    std::map<std::string, std::list<std::string>> dirs;   // "dir|filter" -> names
    std::map<std::string, std::string> families;          // path -> family name
    std::map<std::string, std::uint32_t> faceStyle;
    int comCreated = 0;
    bool comAvailable = false;
    std::vector<std::string> logged;
    bool ReadFileHeader(const cIGZString& p, void* buf, std::uint32_t n) override {
        auto it = headers.find(p.ToChar());
        if (it == headers.end()) return false;
        std::memset(buf, 0, n);
        std::memcpy(buf, it->second.data(), std::min<std::size_t>(n, it->second.size()));
        return true;
    }
    void ListDirectoryFiles(const cIGZString& d, const cIGZString& f, std::list<cRZString>& out) override {
        auto it = dirs.find(std::string(d.ToChar()) + "|" + f.ToChar());
        if (it == dirs.end()) return;
        for (auto& n : it->second) out.emplace_back(n.c_str());
    }
    cIGZFont* CreateFontObject() override {
        if (!comAvailable) return nullptr;
        ++comCreated;
        auto* f = new cGZFont(this);
        f->AddRef();
        return f;
    }
    int GetCurrentFrameworkCodePage() override { return 8; }
    void LogLiveFontAtShutdown(const cIGZString& p) override { logged.push_back(p.ToChar()); }
    std::uint32_t language = 9;
    std::function<void(FakeFace&)> setup;
    IFontRasterizer* Rasterizer() override { return this; }
    std::unique_ptr<IFontFace> NewFace(const std::string& path) override {
        if (!files.count(path)) return nullptr;
        auto f = std::make_unique<FakeFace>();
        if (families.count(path)) f->family = families[path];
        if (faceStyle.count(path)) f->styleFlags = faceStyle[path];
        if (setup) setup(*f);
        lastFace = f.get();
        return f;
    }
    bool FileExists(const cIGZString& p) override { return files.count(p.ToChar()) != 0; }
    bool GetPathExtension(const cIGZString& p, cIGZString& e) override {
        std::string s = p.ToChar();
        auto d = s.rfind('.');
        if (d == std::string::npos) return false;
        e.AssignFromCString(s.substr(d).c_str());
        return true;
    }
    std::uint32_t GetCurrentLanguage() override { return language; }
    void NativeColorToRGB(std::uint32_t n, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override {
        r = static_cast<std::uint8_t>((n >> 11) << 3); g = static_cast<std::uint8_t>(((n >> 5) & 63) << 2); b = static_cast<std::uint8_t>((n & 31) << 3);
    }
    std::uint32_t RGBToNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override {
        return static_cast<std::uint32_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    }
};


struct FakeRenderer : cIGZFontRenderer {
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init(cIGZFont*) override { return true; }
    bool Shutdown(cIGZFont*) override { return true; }
    bool DrawBegin(cIGZFont*, void*, eRenderingMode) override { return true; }
    bool DrawEnd(cIGZFont*, void*, eRenderingMode) override { return true; }
    bool DrawChars(cIGZFont*, void*, sGlyphInfo*, std::uint32_t, eRenderingMode, const std::uint32_t*, const cRZRect*) override { return true; }
};

cRZString S(const char* s) { return cRZString(s); }

}  // namespace

int main() {
    FakeHost host;
    host.families["/fonts/a.ttf"] = "Alpha Sans";
    host.families["/fonts/b.ttf"] = "Beta Serif";
    host.files.insert("/fonts/b.ttf");
    host.files.insert("/fonts/c.xyz");
    host.families["/fonts/c.xyz"] = "Gamma Mono";
    host.families["/fonts/t1.pfb"] = "Type One";

    auto* sys = new cGZFontSys(&host);
    CHECK(sys->FileFilters().size() == 4 && std::strcmp(sys->FileFilters()[0].ToChar(), "*.tt?") == 0 &&
          std::strcmp(sys->FileFilters()[3].ToChar(), "*.otf") == 0);
    CHECK(sys->DefaultCodePage() == 8 && sys->GetServiceID() == 0x20673960 && sys->GetServicePriority() == 900000);
    CHECK(sys->IsServiceRunning() && sys->OnTick(1) && !sys->OnIdle(1));
    sys->SetServiceID(7);
    CHECK(sys->GetServiceID() == 7);

    // interfaces and refcount
    void* q = nullptr;
    cIGZFontSys* iface = sys;
    cIGZSystemService* svc = sys;
    CHECK(iface->QueryInterface(0x673960, &q) && q == iface && sys->QueryInterface(1, &q) && q == iface);
    CHECK(sys->QueryInterface(0x287fb697, &q) && q == svc && !sys->QueryInterface(5, &q));
    // three AddRefs from the queries above
    CHECK(sys->Release() == 2 && sys->Release() == 1);   // the two queries; the third reference is dropped below

    // Init
    CHECK(sys->Init() && sys->IsInitialized());
    CHECK(sys->GenericNames().size() == 4 && std::strcmp(sys->GenericNames().at(5).ToChar(), "Times New Roman") == 0 &&
          std::strcmp(sys->GenericNames().at(9).ToChar(), "Arial") == 0 &&
          std::strcmp(sys->GenericNames().at(6).ToChar(), "Courier New") == 0 &&
          std::strcmp(sys->GenericNames().at(10).ToChar(), "Lucida Console") == 0);
    sys->FontSetName(5, S("Changed"));
    CHECK(sys->Init());   // second Init keeps the changed name
    CHECK(std::strcmp(sys->GenericNames().at(5).ToChar(), "Changed") == 0);

    // source type
    host.headers["/fonts/c.xyz"] = "\0\1\0\0rest";
    host.headers["/fonts/c.xyz"] = std::string("\0\1\0\0rest", 8);
    CHECK(sys->GetFontSourceTypeFromFileName(S("/fonts/a.TTF")) == 1);
    CHECK(sys->GetFontSourceTypeFromFileName(S("/fonts/a.ttc")) == 4 && sys->GetFontSourceTypeFromFileName(S("/x/y.otf")) == 4);
    CHECK(sys->GetFontSourceTypeFromFileName(S("/fonts/t1.pfb")) == 2);
    CHECK(sys->GetFontSourceTypeFromFileName(S("/fonts/c.xyz")) == 1);
    host.headers["/fonts/c.xyz"] = "abcd  %!PS-AdobeFont";
    CHECK(sys->GetFontSourceTypeFromFileName(S("/fonts/c.xyz")) == 2);
    host.headers["/fonts/c.xyz"] = "ttcfxxxx";
    CHECK(sys->GetFontSourceTypeFromFileName(S("/fonts/c.xyz")) == 4);
    host.headers["/fonts/c.xyz"] = "xttcfxxx";
    CHECK(sys->GetFontSourceTypeFromFileName(S("/fonts/c.xyz")) == 0);      // collection only counts at offset 0
    CHECK(sys->GetFontSourceTypeFromFileName(S("/fonts/none.bin")) == 0);   // cannot open

    // AddFont(path)
    CHECK(sys->AddFont(S("/fonts/a.ttf")));
    CHECK(!sys->AddFont(S("/fonts/a.ttf")));      // already registered
    CHECK(!sys->AddFont(S("/fonts/missing.ttf")));
    CHECK(sys->AddFont(S("/fonts/b.ttf")));
    host.faceStyle["/fonts/t1.pfb"] = 1;          // FT_STYLE_FLAG_ITALIC
    CHECK(sys->AddFont(S("/fonts/t1.pfb")));
    CHECK(sys->Fonts().size() == 3);
    const auto& a = sys->Fonts().at(S("/fonts/a.ttf"));
    CHECK(std::strcmp(a.name.ToChar(), "Alpha Sans") == 0 && a.type == 1 && a.sourceType == 1 && a.style == 0);
    CHECK(sys->Fonts().at(S("/fonts/t1.pfb")).sourceType == 2 && sys->Fonts().at(S("/fonts/t1.pfb")).style != 0);
    CHECK(sys->InstalledNames().count(S("alpha sans")) && sys->InstalledNames().count(S("type one")));
    CHECK(sys->IsFontAvailable(S("ALPHA Sans"), 0, 0) && !sys->IsFontAvailable(S("Alpha Sans"), 0, 1) &&
          !sys->IsFontAvailable(S("Nope"), 0, 0));
    CHECK(sys->IsFontAvailable(S("Alpha Sans"), 99, 0x40));   // only (style & 3) matters

    // fixed pitch type from face flags
    host.setup = [](FakeFace& f) { f.faceFlags = 4; };
    host.files.insert("/fonts/m.ttf");
    host.families["/fonts/m.ttf"] = "Mono One";
    CHECK(sys->AddFont(S("/fonts/m.ttf")) && sys->Fonts().at(S("/fonts/m.ttf")).type == 2);
    host.setup = nullptr;

    // metrics sibling attach for Type 1 happens through the face (afm exists)
    // Directory scan
    host.dirs["/fonts/|*.tt?"] = {"d.ttf", "e.ttf", "a.ttf"};
    host.dirs["/fonts/|*.pfb"] = {"UP.PFB", "t1.pfb"};
    for (const char* f : {"/fonts/d.ttf", "/fonts/e.ttf", "/fonts/UP.PFB"}) host.files.insert(f);
    host.families["/fonts/d.ttf"] = "Delta"; host.families["/fonts/e.ttf"] = "Eps"; host.families["/fonts/UP.PFB"] = "Upper";
    // a.ttf and t1.pfb are known; d, e added; UP.PFB added and counted twice
    CHECK(sys->AddAllFontsFromDirectory(S("/fonts")) == 2 + 2);
    CHECK(sys->Fonts().size() == 7);
    CHECK(sys->AddAllFontsFromDirectory(S("")) == 0);

    // Enumerate font info
    cIGZFontSys::FontInfo info[10];
    CHECK(sys->EnumerateFontInfo(nullptr, 0) == 7);
    CHECK(sys->EnumerateFontInfo(info, 3) == 3);
    CHECK(std::strcmp(info[0].name, "Alpha Sans") == 0 || info[0].name[0] != 0);
    CHECK(sys->EnumerateFontInfo(info, 10) == 7);
    // the first (map order) is "/fonts/UP.PFB"
    CHECK(std::strcmp(info[0].name, "Upper") == 0 && info[0].type == 1);

    // substitutions
    CHECK(sys->AddSubstitution(S("Helvetica"), S("Missing Font")));
    CHECK(sys->AddSubstitution(S("Helvetica"), S("Alpha Sans")));
    CHECK(sys->AddSubstitution(S("Helvetica"), S("Beta Serif")));
    cRZString o1, o2, o3, o4;
    cIGZString* outs[4] = {&o1, &o2, nullptr, &o4};
    CHECK(sys->EnumerateFontSubstitutions(S("Helvetica"), outs, 4) == 3);   // bounded at the list end (FONTSYS-7)
    CHECK(std::strcmp(o1.ToChar(), "Missing Font") == 0 && std::strcmp(o2.ToChar(), "Alpha Sans") == 0);
    CHECK(sys->EnumerateFontSubstitutions(S("Helvetica"), outs, 2) == 2);
    CHECK(sys->EnumerateFontSubstitutions(S("Helvetica"), outs, 0) == 0);
    CHECK(sys->EnumerateFontSubstitutions(S("Nothing"), outs, 4) == 0);
    cRZString sub;
    CHECK(sys->IsSubstitutionAvailable(S("Helvetica"), sub) && std::strcmp(sub.ToChar(), "Alpha Sans") == 0);
    CHECK(!sys->IsSubstitutionAvailable(S("Nothing"), sub));

    // size ranges
    FakeRenderer renderer;
    sys->SetDefaultFontRenderer(&renderer);
    CHECK(renderer.refs == 1);
    sys->SetDefaultFontAntialiasing(3);
    sys->SetDefaultCodePage(0x10);
    CHECK(sys->DefaultAntialiasing() == 3 && sys->DefaultCodePage() == 0x10);
    sys->SetDefaultUnantialiasedSizeRange(8, 12, nullptr);

    // FontAcquire by path: no reuse
    CHECK(sys->FontAcquire(S("/fonts/a.ttf"), 0, 0, false) == nullptr);              // size 0
    CHECK(sys->FontAcquire(S("/fonts/zzz.ttf"), 10, 0, false) == nullptr);           // missing file
    cIGZFont* f1 = sys->FontAcquire(S("/fonts/a.ttf"), 10, 0, false);
    CHECK(f1 != nullptr && f1->GetSize() == 10 && f1->GetAntialiasing() == 1);       // inside the default range
    CHECK(f1->GetRenderer() == &renderer);
    int cp = 0;
    CHECK(f1->GetCodePage(cp) && cp == 0x10);
    CHECK(sys->EnumerateManagedFonts(nullptr, 0) == 0);                              // not registered without reuse
    CHECK(f1->Release() == 0);
    // outside the range: default antialiasing
    cIGZFont* f2 = sys->FontAcquire(S("/fonts/a.ttf"), 20, 0, false);
    CHECK(f2 && f2->GetAntialiasing() == 3);
    f2->Release();
    // per-font range containing 20
    // FONTSYS-10: the per-font range is looked up with the name of the font BEFORE Init, i.e. an empty name
    // for a fresh font, so only a range registered under "" is ever found.
    cRZString alpha("");
    sys->SetDefaultUnantialiasedSizeRange(18, 22, &alpha);
    cIGZFont* f3 = sys->FontAcquire(S("/fonts/a.ttf"), 20, 0, false);
    CHECK(f3 && f3->GetAntialiasing() == 1);
    f3->Release();
    // existing entry is not updated (FONTSYS-4)
    sys->SetDefaultUnantialiasedSizeRange(30, 40, &alpha);
    cIGZFont* f4 = sys->FontAcquire(S("/fonts/a.ttf"), 35, 0, false);
    CHECK(f4 && f4->GetAntialiasing() == 3);
    f4->Release();
    // erase with max <= min
    sys->SetDefaultUnantialiasedSizeRange(5, 5, &alpha);
    cIGZFont* f5 = sys->FontAcquire(S("/fonts/a.ttf"), 20, 0, false);
    CHECK(f5 && f5->GetAntialiasing() == 3);
    f5->Release();

    // reuse: registers and shares
    host.comAvailable = true;
    cIGZFont* r1 = sys->FontAcquire(S("/fonts/a.ttf"), 14, 0, true);
    CHECK(r1 && host.comCreated == 1 && sys->EnumerateManagedFonts(nullptr, 0) == 1);
    cIGZFont* r2 = sys->FontAcquire(S("/fonts/a.ttf"), 14, 0, true);
    CHECK(r2 == r1 && host.comCreated == 1);
    cIGZFont* r3 = sys->FontAcquire(S("/fonts/a.ttf"), 16, 0, true);
    CHECK(r3 && r3 != r1 && sys->EnumerateManagedFonts(nullptr, 0) == 2);
    cIGZFont* managed[4];
    CHECK(sys->EnumerateManagedFonts(managed, 1) == 1 && managed[0] == r1);
    CHECK(sys->EnumerateManagedFonts(managed, 4) == 2 && managed[1] == r3);
    // FontRelease: r1 has registry + two caller references
    CHECK(sys->FontRelease(r1) == 2);   // 3 -> 2, entry kept
    CHECK(sys->EnumerateManagedFonts(nullptr, 0) == 2);
    CHECK(sys->FontRelease(r1) == 0);   // 2 -> 1 then the registry's reference goes too, entry removed
    CHECK(sys->EnumerateManagedFonts(nullptr, 0) == 1);
    CHECK(sys->FontRelease(nullptr) == 0);
    host.comAvailable = false;

    // FontAcquire by name list / scoring
    cIGZFont* n1 = sys->FontAcquire(S("Nothing,Beta Serif"), 1, 12, 0, false);
    cRZString path;
    CHECK(n1); n1->GetFilePath(path); CHECK(std::strcmp(path.ToChar(), "/fonts/b.ttf") == 0);
    n1->Release();
    // first listed name beats later ones
    cIGZFont* n2 = sys->FontAcquire(S("alpha sans, beta serif"), 1, 12, 0, false);
    n2->GetFilePath(path); CHECK(std::strcmp(path.ToChar(), "/fonts/a.ttf") == 0);
    n2->Release();
    // substitution: "helvetica" is a key; asking for "Missing Font" (a substitute, not installed) resolves to Helvetica
    // then no file is named that, so everything scores 0x20 and the first registered path (map order) wins
    cIGZFont* n3 = sys->FontAcquire(S("Missing Font"), 1, 12, 0, false);
    CHECK(n3); n3->GetFilePath(path); CHECK(std::strcmp(path.ToChar(), "/fonts/UP.PFB") == 0);
    n3->Release();
    // style penalty: italic request prefers the italic Type 1 file when names do not matter
    cIGZFont* n4 = sys->FontAcquire(S(""), 1, 12, 2, false);
    CHECK(n4); n4->GetFilePath(path); CHECK(std::strcmp(path.ToChar(), "/fonts/t1.pfb") == 0);
    n4->Release();
    // by type only (fixed pitch)
    cIGZFont* n5 = sys->FontAcquire(S(""), 2, 12, 0, false);
    n5->GetFilePath(path); CHECK(std::strcmp(path.ToChar(), "/fonts/m.ttf") == 0);
    n5->Release();
    // the colour overload
    cIGZFont* n6 = sys->FontAcquire(2, 12, 0, 0x1234, false);
    CHECK(n6 && n6->GetFontColor() == 0x1234 && n6->GetFontBackgroundColor() == 0x8000000);
    n6->Release();

    // AddFont(cIGZFont*) and RemoveFontByName
    auto* ext = new cGZFont(&host);
    ext->AddRef();
    ext->SetFilePath(S("/fonts/ext.ttf"));
    ext->SetSize(9);
    host.files.insert("/fonts/ext.ttf");
    CHECK(sys->AddFont(ext) && ext->Release() == 1);   // registry holds one reference
    CHECK(!sys->AddFont(ext));                          // same path and size
    ext->AddRef();
    CHECK(sys->Fonts().count(S("/fonts/ext.ttf")) == 1);
    auto* ext2 = new cGZFont(&host);
    ext2->AddRef();
    ext2->SetFilePath(S("/fonts/ext.ttf"));
    ext2->SetSize(11);
    CHECK(sys->AddFont(ext2));
    CHECK(sys->Fonts().at(S("/fonts/ext.ttf")).entries.size() == 2);
    ext2->Release();
    CHECK(!sys->RemoveFontByFile(S("/fonts/a.ttf")) && sys->Fonts().count(S("/fonts/a.ttf")) == 1);
    CHECK(!sys->RemoveFontByName(S("BETA serif")));
    CHECK(sys->Fonts().count(S("/fonts/b.ttf")) == 0 && !sys->InstalledNames().count(S("beta serif")));
    CHECK(sys->Fonts().size() == 7 - 1 + 1);
    ext->Release();

    // Shutdown / destruction: a still-held font is logged
    CHECK(sys->DefaultAntialiasing() == 3);
    sys->Shutdown();
    CHECK(!sys->IsInitialized());
    CHECK(sys->Fonts().empty());
    CHECK(sys->GenericNames().empty());
    CHECK(host.logged.size() == 1 && host.logged[0] == "/fonts/a.ttf");   // r3 is still held by the caller
    CHECK(renderer.refs == 1);                                            // ... and keeps the renderer alive
    CHECK(r3->Release() == 0 && renderer.refs == 0);
    CHECK(sys->Release() == 0);   // count was 0 after the interface checks: Release destroys (FONTSYS-1)

    if (failures == 0) std::printf("gz_fontsys OK\n");
    return failures == 0 ? 0 : 1;
}

// EARLY TEST / EXPERIMENTAL HOST — not recovered game code. See README_EARLY_TEST.md.
#include "experimental_host/HostUi.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "experimental_host/HostBoot.h"
#include "experimental_host/HostWinMgr.h"
#include "gz/Buffer.h"
#include "gz/GZCOMDirector.h"
#include "gz/GZFramework.h"
#include "gz/LegacyControl.h"
#include "gz/ScriptTerp.h"
#include "gz/ScriptVocab.h"
#include "gz/ScriptableControlSet.h"
#include "gz/UIScriptService.h"
#include "gz/SmallObjects.h"
#include "gz/WinBMP.h"
#include "gz/WinBtn.h"
#include "gz/WinFlatRect.h"
#include "gz/WinGen.h"
#include "gz/WinStyle.h"
#include "gz/WinText.h"
#include "rz/COMDirectors.h"
#include "sc4/WinSmallViews.h"
#include "rz/cRZString.h"

#include "dbpf/DBPFReader.h"

// gz::WinStyleManager() is declared in WinStyle.h and defined nowhere in the recovered tree (a seam); the host owns it.
namespace gz {
static IWinStyleManager* g_styleManager = nullptr;
IWinStyleManager* WinStyleManager() { return g_styleManager; }
void SetWinStyleManager(IWinStyleManager* manager) { g_styleManager = manager; }
}  // namespace gz

namespace exphost {
namespace {

HostWinMgr* g_mgr = nullptr;
cIGZUIScriptService* g_service = nullptr;

// ---- the image loader: PNG resources of the game data (read with the recovered DBPF reader; the engine's own path
// goes through cGZPersistResourceManager + the Gimex factory, which is not wired in the host yet) -------------------
std::string GameDataDir() {
    return std::string(std::getenv("HOME")) +
           "/Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents/GameData/";
}

PngDecoder g_decodePng = nullptr;   // set by host_main.mm (ImageIO lives there)

class HostScriptServices final : public IVocabularyServices, public IScriptControlHost, public IScriptTerpHost {
public:
    // ---- IVocabularyServices (spLocalVocab)
    IVocabulary* Cached() override { return cached_; }
    void AddRefCached() override { if (cached_) cached_->AddRef(); }
    void ReleaseCached() override { if (cached_ && cached_->Release() == 0) cached_ = nullptr; }
    bool CreateCached() override {
        void* object = nullptr;
        if (!GZCOM() || !GZCOM()->CreateInstance(0xba2e7954, kGZIID_cIGZScriptVocab, &object) || !object) return false;
        cached_ = reinterpret_cast<IVocabulary*>(static_cast<IScriptVocab*>(static_cast<cIGZUnknown*>(object)));
        return true;
    }
    // ---- IScriptTerpHost
    IScriptVocab* CreateVocabulary() override { auto* v = new cGZScriptVocab(); v->AddRef(); return v; }
    // ---- IScriptControlHost
    cIGZWin* CreateControl(std::uint32_t clsid, std::uint32_t iid) override {
        void* object = nullptr;
        if (!GZCOM() || !GZCOM()->CreateInstance(clsid, iid, &object) || !object) {
            if (std::getenv("HOST_DEBUG")) std::printf("ui: no class for control clsid %08x (iid %08x)\n", clsid, iid);
            return nullptr;
        }
        void* window = nullptr;
        const bool ok = static_cast<cIGZUnknown*>(object)->QueryInterface(0x22ba0121, &window);
        static_cast<cIGZUnknown*>(object)->Release();
        return ok ? static_cast<cIGZWin*>(window) : nullptr;
    }
    cIGZWin* GetMainWindow() override { return g_mgr->GetMainWindow(); }
    bool UnscriptWindow(cIGZWin*, cIGZString&) override { return false; }   // unscript_IGZWin is not recovered
    bool LoadScriptString(std::uint32_t, std::uint32_t, cIGZString&) override { return false; }   // captions fall back to the script text
    cIGZBuffer* LoadScriptBuffer(std::uint32_t group, std::uint32_t instance) override {
        if (std::getenv("HOST_DEBUG")) std::printf("ui: LoadScriptBuffer(%08x, %08x)\n", group, instance);
        if (!g_decodePng) return nullptr;
        // The packages stay open once read (each .dat is read whole by the DBPF reader), in the engine's search order.
        static std::vector<std::unique_ptr<dbpf::Package>> packages;
        static const char* kNames[] = {"Intro.dat", "SimCity_1.dat", "SimCity_2.dat", "SimCity_3.dat", "SimCity_4.dat", "SimCity_5.dat", "EP1.dat"};
        static std::size_t opened = 0;
        for (std::size_t i = 0; i <= opened && i < 7; ++i) {
            if (i == packages.size()) {   // open the next one only when the earlier ones do not have it
                auto package = std::make_unique<dbpf::Package>();
                package->Open(GameDataDir() + kNames[i]);
                packages.push_back(std::move(package));
                opened = packages.size();
            }
            const dbpf::Entry* entry = packages[i]->Find(0x856ddbac, group, instance);
            std::vector<std::uint8_t> bytes;
            if (entry && packages[i]->Read(*entry, bytes)) {
                if (cGZBuffer* buffer = g_decodePng(bytes)) { if (std::getenv("HOST_DEBUG")) std::printf("ui:   found in %s %ux%u\n", kNames[i], buffer->Width(), buffer->Height()); return buffer; }   // one counted reference
            }
        }
        return nullptr;
    }
    void GetNativeResolution(int index, int& width, int& height) override {
        static const int modes[4][2] = {{1024, 768}, {1280, 1024}, {1600, 1200}, {1920, 1080}};
        width = modes[index & 3][0]; height = modes[index & 3][1];
    }
private:
    IVocabulary* cached_ = nullptr;
} g_services;

template <class T>
cIGZUnknown* MakeWin() { return static_cast<cIGZUnknown*>(static_cast<cIGZWin*>(new T(g_mgr))); }

}  // namespace

void SetPngDecoder(PngDecoder decoder) { g_decodePng = decoder; }

bool InitUiScripts(HostWinMgr* mgr) {
    g_mgr = mgr;
    cGZFramework* framework = BootFramework();
    if (!framework || !GZCOM()) { std::printf("ui: no framework (run the boot first)\n"); return false; }

    // GZCOM class makers: the recovered classes the scripts create (constructors take the window manager)
    gz::RegisterDirectorMaker("cGZUIScriptService", +[]() -> cIGZUnknown* { return static_cast<cIGZUIScriptService*>(new cGZUIScriptService()); });
    gz::RegisterDirectorMaker("cGZScriptableControlSet", +[]() -> cIGZUnknown* { return static_cast<cIGZScriptableControlSet*>(new cGZScriptableControlSet()); });
    gz::RegisterDirectorMaker("cGZLegacyControl", +[]() -> cIGZUnknown* { return static_cast<cIGZScriptableControl*>(new cGZLegacyControl()); });
    gz::RegisterDirectorMaker("cGZScriptVocab", +[]() -> cIGZUnknown* { return static_cast<IScriptVocab*>(new cGZScriptVocab()); });
    gz::RegisterDirectorMaker("cGZScriptTerp", +[]() -> cIGZUnknown* { return static_cast<cIGZScriptTerp*>(new cGZScriptTerp()); });
    gz::RegisterDirectorMaker("cGZWinGen", +[]() -> cIGZUnknown* { return MakeWin<gz::cGZWinGen>(); });
    gz::RegisterDirectorMaker("cGZWinText", +[]() -> cIGZUnknown* { return MakeWin<gz::cGZWinText>(); });
    gz::RegisterDirectorMaker("cGZWinBtn", +[]() -> cIGZUnknown* { return MakeWin<gz::cGZWinBtn>(); });
    gz::RegisterDirectorMaker("cGZWinBMP", +[]() -> cIGZUnknown* { return MakeWin<gz::cGZWinBMP>(); });
    gz::RegisterDirectorMaker("cGZWinCustom", +[]() -> cIGZUnknown* { return MakeWin<gz::cGZWinCustom>(); });
    gz::RegisterDirectorMaker("cSC4WinGenTransparent", +[]() -> cIGZUnknown* { return MakeWin<sc4::cSC4WinGenTransparent>(); });
    // cSC4WinText (clsid 0xaa7cecfd) is not recovered (3 functions); a plain cGZWinText stands in for it
    gz::RegisterDirectorMaker("cSC4WinText", +[]() -> cIGZUnknown* { return MakeWin<gz::cGZWinText>(); });
    gz::RegisterDirectorMaker("cGZWinFlatRect", +[]() -> cIGZUnknown* { return MakeWin<gz::cGZWinFlatRect>(); });

    // seams
    SetVocabularyServices(&g_services);
    SetScriptControlHost(&g_services);
    SetScriptTerpHost(&g_services);

    // the window manager is a framework service (the script service reads the main window size from it)
    framework->AddSystemService(static_cast<cIGZSystemService*>(mgr));

    void* service = nullptr;
    if (!GZCOM()->CreateInstance(0xfa3562fb, kGZIID_cIGZUIScriptService, &service) || !service) {
        std::printf("ui: GZCOM could not create the UI script service\n");
        return false;
    }
    g_service = static_cast<cIGZUIScriptService*>(service);
    return true;
}

cIGZWin* CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent, std::uint32_t id) {
    if (!g_service) return nullptr;
    return g_service->CreateWindowFromScript(key, parent, id);
}

}  // namespace exphost

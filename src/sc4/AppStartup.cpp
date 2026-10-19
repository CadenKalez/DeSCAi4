// cSC4App start-up: the COM directors and the system services the framework runs.
#include "gz/GZCOMDirector.h"
#include "gz/cGZCOM.h"
#include "gz/cIGZFrameWork.h"
#include "gz/cIGZSystemService.h"
#include "rz/COMDirectors.h"
#include "rz/cRZOStream.h"
#include "sc4/App.h"

namespace {
constexpr std::uint32_t kSystemServiceIid = kGZIID_cIGZSystemService;   // 0x287fb697

// The recurring step: QueryInterface(system service) -> AddSystemService -> Release.
void RegisterAsService(cIGZFrameWork& framework, cIGZUnknown* object) {
    if (!object) return;
    cIGZSystemService* service = nullptr;
    if (object->QueryInterface(kSystemServiceIid, reinterpret_cast<void**>(&service))) {
        framework.AddSystemService(service);
        service->Release();
    }
}
}  // namespace

void cSC4App::AddCOMDirectors() {
    cRZCOMDllDirector* process = RZGetCOMDllDirector();
    cIGZCOM* com = GZCOM();
    auto add = [&](cRZCOMDllDirector* director) {
        process->AddDirector(director);
        director->OnStart(com);
    };
    add(gz::SC4GetCOMDirector());
    if (preferences_.Get<std::uint8_t>(sc4::prefs::kAudioEnabledMirror) != 0 && startupHooks_.audioDirector)
        add(startupHooks_.audioDirector());
    if (startupHooks_.showFpsPreference && startupHooks_.showFpsPreference()) {
        static gz::cGZToolsCOMDirector tools;       // cGZToolsCOMDirector::Get()
        add(&tools);
    }
    add(gz::GetGZWWWCOMDllDirector());
}

// Every service follows one of two shapes: ask COM for the class and fall back to constructing the built-in one
// (`fallbackClass` names it), or construct it unconditionally. Either way the object is registered through its
// cIGZSystemService view and then released.
void cSC4App::AddSystemServices() {
    cIGZFrameWork& framework = *framework_;
    cIGZCOM& com = *GZCOM();

    // The object as the requested interface (a pointer of that interface's type, held through cIGZUnknown).
    auto create = [&](std::uint32_t clsid, std::uint32_t iid, const char* fallbackClass) -> cRZAutoRefCount<cIGZUnknown> {
        cRZAutoRefCount<cIGZUnknown> object;
        if (com.CreateInstance(clsid, iid, object.AsOutParam())) return object;
        if (fallbackClass) {
            auto factory = serviceFactories_.find(fallbackClass);
            if (factory != serviceFactories_.end()) {
                cRZAutoRefCount<cIGZUnknown> built(factory->second());
                if (built && built->QueryInterface(iid, object.AsOutParam())) return object;
                return built;
            }
        }
        return object;
    };
    auto createAndRegister = [&](std::uint32_t clsid, std::uint32_t iid, const char* fallbackClass) {
        cRZAutoRefCount<cIGZUnknown> object = create(clsid, iid, fallbackClass);
        RegisterAsService(framework, object.get());
        return object;
    };
    auto build = [&](const char* className) -> cRZAutoRefCount<cIGZUnknown> {
        auto factory = serviceFactories_.find(className);
        return factory != serviceFactories_.end() ? cRZAutoRefCount<cIGZUnknown>(factory->second()) : cRZAutoRefCount<cIGZUnknown>();
    };

    createAndRegister(0xa9a1db23, 0x3ae4beaa, "cGZAllocatorService");
    createAndRegister(0x2073215d, 0xe0203660, "cGZFileSystem");
    FindAndPossiblyFixIniFiles();
    if (auto server = createAndRegister(0x04063236, 0xc1085722, "cGZMessageServer"))
        static_cast<cIGZMessageServer*>(static_cast<void*>(server.get()))->SetAlwaysClearQueueOnTick(false);
    if (auto server = createAndRegister(0x252294e9, 0x652294c7, "cGZMessageServer2"))
        static_cast<cIGZMessageServer2*>(static_cast<void*>(server.get()))->SetAlwaysClearQueueOnTick(false);

    // the log service, and the standard output / error streams go quiet
    if (auto log = build("nGZLog::cService")) RegisterAsService(framework, log.get());
    {
        cRZAutoRefCount<cRZNullOStream> quiet(new cRZNullOStream());
        framework.SetStream(0, static_cast<cIGZOStream*>(quiet.get()));
        framework.SetStream(1, static_cast<cIGZOStream*>(quiet.get()));
    }

    createAndRegister(0x8b985476, 0xcb90253a, "cGZCommandServer");
    createAndRegister(0x8898ed08, 0xe898ed03, "cSC4GameDataRegistry");
    createAndRegister(0xcbd101a9, 0x4bd10129, "cGZLuaScriptServer");
    createAndRegister(0xa3024279, 0x0054b7d5, "cRZLanguageManager");

    // the graphics system learns where the application's .ini is
    {
        cRZAutoRefCount<cIGZUnknown> graphics = create(0xa499932c, 0x0073283c, nullptr);
        if (graphics) {
            cRZAutoRefCount<cIGZUnknown> graphics2;
            if (graphics->QueryInterface(0xc47b747c, graphics2.AsOutParam()) && startupHooks_.configureGraphicSystem) {
                cRZString iniPath;
                GetAppIniFilePath(iniPath);
                startupHooks_.configureGraphicSystem(graphics2.get(), iniPath);
            }
            RegisterAsService(framework, graphics.get());
        }
    }
    createAndRegister(0x445fabbb, 0x66b217a1, nullptr);                   // cGZCanvasW32
    createAndRegister(0x20673960, 0x00673960, "cGZFontSys");
    createAndRegister(0x84174448, 0x000005a4, nullptr);                   // the window manager
    createAndRegister(0xc2c2eb0f, 0x22c2eb1f, "cGZWinCtrlMgr");
    createAndRegister(0xc7ec0014, 0x07ec0010, "cGZClipboardManager");
    if (preferences_.Get<std::uint8_t>(sc4::prefs::kImeEnabled) != 0) {
        createAndRegister(0x2ac4516b, 0x2ac44ae7, "cGZWinIMEServer");
        if (preferences_.Get<std::uint8_t>(sc4::prefs::kImeEnabled) != 0)
            createAndRegister(0x2ac4516d, 0x2ac44ae9, "cGZWinIMEProxy");
    }
    createAndRegister(0x656b8f27, 0x656b8efc, "cGZPersistResourceManager");
    if (auto registry = build("cGZRegistry")) RegisterAsService(framework, registry.get());
    createAndRegister(0x9ac0e23c, 0xfac0e219, "cS3DTextureBindingFactory");
    createAndRegister(0x9ac0e23d, 0xfac0e21a, nullptr);                   // the material manager

    // the UI script service takes the script resource factory
    {
        cRZAutoRefCount<cIGZUnknown> scripts;
        if (framework.GetSystemService(0x5a356e15, 0xfa3562fa, scripts.AsOutParam()) && scripts && startupHooks_.registerScriptResourceFactory)
            startupHooks_.registerScriptResourceFactory(scripts.get());
    }

    // the WWW system gets the HTTP cache directory
    {
        cRZAutoRefCount<cIGZUnknown> www = create(0x849b190a, 0x449b1912, "cGZWWWSystem");
        if (www) {
            RegisterAsService(framework, www.get());
            if (startupHooks_.configureWWWSystem) {
                cRZString cacheDirectory;
                GetHTTPCacheDirectory(cacheDirectory);
                startupHooks_.configureWWWSystem(www.get(), cacheDirectory);
            }
        }
    }
    if (auto events = build("cSC4AudioAEMSEventSystem")) RegisterAsService(framework, events.get());
}

// The original looks up the file system service, then reads the Directories/Data and Directories/PlugIn entries of
// the application .ini and checks that those directories exist (and the file system's own defaults otherwise); the
// outcome is never used, so only the "already done" flag has an effect here.
void cSC4App::FindAndPossiblyFixIniFiles() {
    if (iniFilesChecked_) return;
    iniFilesChecked_ = true;
}

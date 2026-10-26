#include "gz/RZServices.h"

#include "gz/GZCOMDirector.h"
#include "gz/cIGZFrameWork.h"

namespace RZ {

namespace {
cIGZFileSystem* g_fs; cIGZMessageServer* g_ms; cIGZMessageServer2* g_ms2; cIGZUnknown *g_ts, *g_rdd, *g_gs; cIGZWinMgr* g_wm;
gz::cIGZWinCtrlMgr* g_wcm; cIGZUnknown *g_ss, *g_sm, *g_hm, *g_fo; cIGZLanguageManager* g_lm;

// RZGetFramework()->GetSystemService(service, iid, &p), the reference released again; null without a framework
void* Lookup(void*& cache, std::uint32_t serviceId, std::uint32_t iid) {
    if (cache) return cache;
    cIGZFrameWork* framework = RZGetFramework();
    void* found = nullptr;
    if (framework) framework->GetSystemService(serviceId, iid, &found);
    cache = found;
    if (found) static_cast<cIGZUnknown*>(found)->Release();
    return cache;
}
template <class T> T* Typed(T*& cache, std::uint32_t serviceId, std::uint32_t iid) {
    void* slot = cache;
    Lookup(slot, serviceId, iid);
    cache = static_cast<T*>(slot);
    return cache;
}
}  // namespace

cIGZFileSystem* FS() { return Typed(g_fs, 0x2073215d, 0xe0203660); }
cIGZMessageServer* MS() { return Typed(g_ms, 0x64063387, 0xc1085722); }
cIGZMessageServer2* MS2() { return Typed(g_ms2, 0x04fa845b, 0x652294c7); }
cIGZUnknown* TS() { return Typed(g_ts, 0x8bf5e368, 0x8bf5e36d); }
cIGZUnknown* RDD() { return Typed(g_rdd, 0x8414a2c2, 0x6054a2fb); }
cIGZUnknown* GS() { return Typed(g_gs, 0xc416025c, 0x0073283c); }
cIGZWinMgr* WM() { return Typed(g_wm, 0xa417445e, 0x000005a4); }
gz::cIGZWinCtrlMgr* WCM() { return Typed(g_wcm, 0xc2c2eb0f, 0x22c2eb1f); }
cIGZUnknown* SS() { return Typed(g_ss, 0x23cecba0, 0x106077d2); }
cIGZUnknown* SM() { return Typed(g_sm, 0x1af85bfb, 0x3af85bb7); }
__attribute__((weak)) cIGZUnknown* HM() { return Typed(g_hm, 0x1b2ce671, 0xbb1bd9a8); }   // the Hit manager has its own typed accessor (audio/HitMan.cpp)
cIGZUnknown* FO() { return Typed(g_fo, 0x20673960, 0x00673960); }
cIGZLanguageManager* LM() { return Typed(g_lm, 0x441e5070, 0x0054b7d5); }

// RZ::ADir / RZ::DDir: the file system service's application / data directory as a string
void ADir(cIGZString& out) { out.AssignFromCString(FS()->AppDirectory()); }
void DDir(cIGZString& out) { out.AssignFromCString(FS()->DataDirectory()); }

void ResetCaches() {
    g_fs = nullptr; g_ms = nullptr; g_ms2 = nullptr; g_ts = nullptr; g_rdd = nullptr; g_gs = nullptr; g_wm = nullptr;
    g_wcm = nullptr; g_ss = nullptr; g_sm = nullptr; g_hm = nullptr; g_fo = nullptr; g_lm = nullptr;
}

}  // namespace RZ

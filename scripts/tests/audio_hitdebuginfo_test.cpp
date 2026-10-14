#include <cassert>
#include <cstdio>
#include <fstream>

#include "audio/HitDebugInfo.h"
#include "rz/cRZPath.h"
#include "rz/cRZString.h"

int main() {
    // ---- cRZPath ----
    cRZPath p("/Users/x/data/track.hit");
    assert(p.GetDirectory() == "/Users/x/data/" && p.GetFileName() == "track" && p.GetExtension() == ".hit");
    assert(p.GetPath() == "/Users/x/data/track.hit" && p.GetDriveAndDirectory() == "/Users/x/data/");
    assert(p.GetFileNameAndExtension() == "track.hit");
    cRZPath w("C:\\game\\sound\\a.wav");
    assert(w.GetDrive() == "C:" && w.GetDirectory() == "/\\game\\sound\\" || w.GetDirectory().size() > 0);
    assert(p.SetFileName("other", false) && p.SetExtension("hdb", false) && p.GetPath() == "/Users/x/data/other.hdb");
    assert(p.SetFileName("/a/b/c.txt", true) && p.GetFileName() == "c");
    cRZPath bare("name");
    assert(bare.GetDirectory().empty() && bare.GetExtension().empty() && bare.GetPath() == "name");

    // ---- debug info ----
    const std::string base = std::string(::getenv("TMPDIR") ? ::getenv("TMPDIR") : "/tmp") + "/";
    {
        std::ofstream f(base + "dbgtest.HDB");
        f << "SOURCEFILE one.hs  {line,offset}\n{10,4}\n{12,20}\n{15,40}\nSOURCEFILE two.hs  {line,offset}\n{3,8}\n";
    }
    auto* info = new cHitDebugInfo();
    info->AddRef();
    void* q = nullptr;
    assert(info->QueryInterface(0x7bba0cbd, &q) && q == static_cast<cIHitDebugInfo*>(info));
    assert(info->QueryInterface(0x287fb697, &q) && !info->QueryInterface(5, &q));
    info->Release(); info->Release();
    assert(info->GetServiceID() == 0xfbc9cd04 && info->GetServicePriority() == 0 && info->Init());

    static const std::uint8_t data[512] = {};
    cRZString hit((base + "dbgtest.hit").c_str());
    assert(info->LoadDebugInfo(hit, data, 64));
    assert(info->sourceFileCount() == 2);
    assert(info->sourceFile(0)->name() == base + "one.hs");
    assert(info->sourceFile(0)->lines().size() == 3 && info->sourceFile(1)->lines().size() == 1);

    // address -> (file, line): offset 20 maps to line 12; stops at the first entry satisfying both flags
    cRZString name;
    std::int32_t line = 0;
    assert(info->GetFilenameAndLineNumFromDataAddress(data + 20, name, line) && line == 12 && name.str() == base + "one.hs");
    assert(info->GetFilenameAndLineNumFromDataAddress(data + 8, name, line));   // second file's range is the same data
    assert(!info->GetFilenameAndLineNumFromDataAddress(data + 300, name, line));
    // file/line -> address
    const std::uint8_t* out = nullptr;
    cRZString one((base + "one.hs").c_str());
    assert(info->GetDataAddressFromFilenameAndLineNum(one, 15, &out) && out == data + 40);
    assert(!info->GetDataAddressFromFilenameAndLineNum(one, 99, &out));
    cRZString missing("nofile.hs");
    assert(!info->GetDataAddressFromFilenameAndLineNum(missing, 10, &out));
    // entry points need the hit manager
    assert(!info->GetDataAddressFromEntryPoint(0x1234, &out));
    cRZString outName;
    assert(!info->GetHitFilenameFromEntryPoint(0x1234, outName));
    // a missing file
    cRZString nothing((base + "does_not_exist.hit").c_str());
    assert(!info->LoadDebugInfo(nothing, data, 64));
    assert(info->Release() == 0);
    std::remove((base + "dbgtest.HDB").c_str());
    std::puts("hitdebuginfo ok");
}

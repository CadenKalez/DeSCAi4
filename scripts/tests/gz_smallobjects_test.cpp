#include <cstdio>
#include <vector>

#include "fake_winmgr.h"
#include "gz/SmallObjects.h"
#include "rz/cRZString.h"
#include "s3d/MeshAndMaterial.h"
#include "sc4/ColorCustomizer.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

struct Thing final : cIGZUnknown {
    int refs = 1;
    bool QueryInterface(std::uint32_t iid, void** out) override { if (iid != 0x77) return false; *out = this; return true; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
struct Spy final : cIGZWinSpyTarget {
    int seen = 0;
    void OnSpyMessage(cIGZWin*, cGZMessage&) override { ++seen; }
};
struct Dlg final : cGZWinDlg {
    using cGZWinDlg::cGZWinDlg;
    cIGZWin* equivalent = nullptr;
    cIGZWin* IsChildKeyEquivalent(std::uint32_t, std::uint32_t) override { return equivalent; }
};
struct Target final : cGZWin {
    explicit Target(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    int got = 0;
    bool DoMessage(cGZMessage&) override { ++got; return true; }
};
struct Variant final : cIGZVariant {
    // only the three members the customizer uses
};

int main() {
    Thing thing;
    auto* clip = new cGZClipboardManagerClipboardData(5, &thing);
    clip->AddRef();
    void* out = nullptr;
    CHECK(clip->GetDataType() == 5 && clip->GetData() == &thing && clip->GetData(0x77, &out) && !clip->GetData(0x78, &out));
    CHECK(clip->QueryInterface(0x07ec0006, &out) && !clip->QueryInterface(2, &out));
    clip->Release();
    clip->Release();
    CHECK(thing.refs == 0);   // the clipboard data released its reference when it died

    FakeMgr mgr;
    auto* dlg = new Dlg(&mgr);
    dlg->AddRef();
    auto* child = new Target(&mgr);
    child->AddRef();
    CHECK(!dlg->GZOnKeyDown(13, 0));
    dlg->equivalent = child;
    CHECK(dlg->GZOnKeyDown(13, 0) && mgr.focus == child);
    CHECK(dlg->GZOnCharacter('a') && dlg->GZOnKeyUp(1, 0));
    dlg->GZEndDialog(42);
    CHECK(mgr.dialogs.size() == 1 && mgr.dialogs[0].code == 0xf && mgr.dialogs[0].window == dlg && mgr.dialogs[0].result == 42);
    CHECK(dlg->QueryInterface(0xc12e57e2, &out) && dlg->QueryInterface(0x22ba0121, &out) && !dlg->QueryInterface(9, &out));
    child->Release();
    dlg->Release();

    auto* watched = new Target(&mgr);
    watched->AddRef();
    Spy spy;
    auto* filter = new cGZWinSpyMessageFilter(watched, &spy);
    filter->AddRef();
    cGZMessage probe{kGZMsgCommand, 1, 2, 3};
    CHECK(!filter->DoMessage(watched, probe) && spy.seen == 1);
    filter->Detach();
    CHECK(!filter->DoMessage(watched, probe) && spy.seen == 1);
    CHECK(filter->QueryInterface(0xc6ae7085, &out) && !filter->QueryInterface(5, &out));
    filter->Release();
    watched->Release();

    auto* meshes = new s3d::cS3DMeshAndMaterial();
    meshes->AddRef();
    meshes->Meshes().resize(3);
    meshes->Materials().resize(2);
    meshes->Mesh(2)[0] = 7;
    CHECK(meshes->MeshCount() == 3 && meshes->Mesh(2)[0] == 7 && meshes->Material(1).size() == 0x50);
    CHECK(meshes->QueryInterface(0x26a68448, &out) && !meshes->QueryInterface(4, &out));
    meshes->Release();
    meshes->Release();
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}

#include <cstdio>

#include "fake_filebrowser.h"
#include "sc4/WinLoadHeightField.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Host final : ILoadHeightFieldHost {
    FakeFileBrowser* browser = nullptr; std::uint32_t gutters[3] = {};
    cIGZWinFileBrowser* CreateFileBrowser() override { return browser; }
    void SetBrowserGutters(cIGZWinFileBrowser*, std::uint32_t a, std::uint32_t b, std::uint32_t c) override { gutters[0] = a; gutters[1] = b; gutters[2] = c; }
    std::string FilterSetTitle() override { return "Images"; }
};
cGZMessage Cmd(std::uint64_t sender, std::uint64_t id) { cGZMessage m; m.type = kGZMsgCommand; m.param1 = sender; m.param2 = id; return m; }
}

int main() {
    FakeMgr2 m; Host host; SetLoadHeightFieldHost(&host);
    auto* w = new cSC4WinLoadHeightField(&m); w->AddRef();
    CHECK(w->GetID() == 0xea9660f3);
    auto* bw = new cGZWin(&m); bw->AddRef();
    FakeFileBrowser fb(bw); host.browser = &fb;

    cRZString dir("/maps");
    w->SetInitialDirectory(dir);   // before Init: remembered only
    CHECK(fb.calls.empty());
    CHECK(w->Init());
    CHECK(w->GetL() == 20 && w->GetT() == 20 && w->GetR() == 424 && w->GetB() == 288);
    CHECK(bw->GetID() == 0x9aedef7c && bw->GetL() == 8 && bw->GetT() == 8 && bw->GetR() == 396 && bw->GetB() == 260);
    CHECK(host.gutters[0] == 4 && host.gutters[1] == 4 && host.gutters[2] == 4);
    CHECK(fb.filters.size() == 6 && fb.filters[0] == "*.bmp" && fb.filters[1] == "*.png" && fb.filters[2] == "*.gif" &&
          fb.filters[3] == "*.jpg" && fb.filters[4] == "*.jpeg" && fb.filters[5] == "*.tga");
    CHECK(fb.filterSetText == "Images" && fb.set == 0 && fb.defaultDir == "/maps");
    CHECK(w->GetChildWindowFromID(0x9aedef7c) == bw);

    // choosing a file ends the dialog with 1; a directory does nothing; OK on a directory falls through
    fb.selection.kind = 1;
    cGZMessage chosen; chosen.type = kFileBrowserMsgSelectionChosen;
    CHECK(w->DoMessage(chosen) && m.dialogs.empty());
    fb.selection.kind = 2;
    CHECK(w->DoMessage(chosen) && m.dialogs.size() == 1 && m.dialogs[0].code == 0xf && m.dialogs[0].result == 1 && m.dialogs[0].window == w);
    cGZMessage ok = Cmd(0x287259f6, 0xbaa55d58);
    w->DoMessage(ok); CHECK(m.dialogs.size() == 2 && m.dialogs[1].result == 1);
    fb.selection.kind = 1;
    w->DoMessage(ok); CHECK(m.dialogs.size() == 2);
    cGZMessage cancel = Cmd(0x287259f6, 0xbaa55d59);
    w->DoMessage(cancel); CHECK(m.dialogs.size() == 3 && m.dialogs[2].result == 0);
    cGZMessage other = Cmd(1, 0xbaa55d59);
    w->DoMessage(other); CHECK(m.dialogs.size() == 3);
    // escape
    CHECK(w->GZOnKeyDown(0x1b, 0)); CHECK(m.dialogs.size() == 4 && m.dialogs[3].result == 0);
    CHECK(!w->GZOnKeyDown('a', 0) && m.dialogs.size() == 4);

    FileBrowserSelection sel;
    fb.selection.kind = 2;
    CHECK(w->GetSelection(sel) && sel.kind == 2);
    cRZString out;
    w->GetInitialDirectory(out);
    CHECK(std::string(out.ToChar()) == "/tmp/cur");
    cRZString d2("/elsewhere");
    w->SetInitialDirectory(d2);
    CHECK(fb.currentDir == "/elsewhere");

    fb.currentDir = "/final";
    w->Shutdown();
    CHECK(fb.refs == 0);
    cRZString out2; w->GetInitialDirectory(out2);
    CHECK(std::string(out2.ToChar()) == "/final");
    CHECK(!w->GetSelection(sel));
    w->Release();
    bw->Release();
    SetLoadHeightFieldHost(nullptr);
    if (!failures) std::printf("winloadheightfield OK\n");
    return failures;
}

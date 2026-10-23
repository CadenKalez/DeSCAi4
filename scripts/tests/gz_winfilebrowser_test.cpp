// cGZWinFileBrowser (recovered part): filters and filter sets, selection and directory operations on a real temporary
// directory, selection handlers and their messages, flags, layout, field formats, background, FitStringIntoWidth.
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "fake_filebrowser_buffers.h"
#include "fake_winmgr.h"
#include "gz/WinFileBrowser.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
namespace fs = std::filesystem;

namespace {
struct Target final : cGZWin {
    std::vector<cGZMessage> got;
    // The selection message points at a FileBrowserSelection on the sender's stack: it is only valid while the
    // message is being handled, so the receiver copies it here (reading it later would be a dangling read).
    struct SelectionCopy { std::uint32_t kind = 0, field = 0; std::string name, fullPath; };
    std::vector<SelectionCopy> selections;
    explicit Target(cIGZWinMgr* m) : cGZWin(m, nullptr) {}
    bool DoMessage(cGZMessage& m) override {
        got.push_back(m);
        if (m.type == kFileBrowserMsgSelectionChanged && m.param2) {
            const auto* s = reinterpret_cast<const FileBrowserSelection*>(m.param2);
            selections.push_back({s->kind, s->field, s->name ? s->name->str() : "", s->fullPath ? s->fullPath->str() : ""});
        }
        return true;
    }
};
struct Combo final : IFileBrowserCombo {
    std::vector<std::pair<std::string, int>> items;
    int selected = -1;
    void SetItemText(const cIGZString& t, std::uint32_t i) override { items.push_back({t.ToChar(), int(i)}); }
    void SetSelectedIndex(std::uint32_t i, bool) override { selected = int(i); }
};
struct Edit final : IFileBrowserTextEdit {
    std::string text;
    void GetText(cRZString& out) override { out.AssignFromCString(text.c_str()); }
    void SetText(const char* t, std::uint32_t n, bool) override { text.assign(t, n); }
};
struct Grid final : IFileBrowserGrid {
    std::vector<std::string> names;
    int scrolled = -1, firstRow = -1;
    bool visible = false;
    void GetCellUsedRange(cRZRect& r) override { r = {0, 0, 3, int(names.size()) - 1}; }
    bool GetCellTextValue(int, int row, cRZString& out) override { if (row >= int(names.size())) return false; out.AssignFromCString(names[row].c_str()); return true; }
    void ScrollToRow(int r) override { scrolled = r; }
    bool IsCellVisible(int, int) override { return visible; }
    void SetFirstVisibleCell(int, int r) override { firstRow = r; }
    void ClearSelection() override {}
    cIGZWin* AsIGZWin() override { return nullptr; }
};
struct Host final : IFileBrowserHost {
    std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { return 0xff000000u | (r << 16) | (g << 8) | b; }
    void NativeToRGB(std::uint32_t n, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) override { r = n >> 16; g = n >> 8; b = n; }
    cRZString ellipsis{"~"};
    bool haveEllipsis = false;
    const cIGZString* DefaultText(std::uint32_t) override { return haveEllipsis ? &ellipsis : nullptr; }
};
}  // namespace

int main() {
    FakeMgr mgr;
    Host host;
    auto* fb = new cGZWinFileBrowser(&mgr);
    fb->AddRef();
    fb->SetHost(&host);
    Target notify(&mgr);
    fb->SetNotificationTarget(&notify);

    // ---- identity / refcount ----
    void* q = nullptr;
    CHECK(fb->QueryInterface(kGZIID_cIGZWinFileBrowser, &q) && q == static_cast<cIGZWinFileBrowser*>(fb));
    CHECK(fb->Release() == 1);
    CHECK(fb->AsIGZWin() == static_cast<cGZWin*>(fb));
    CHECK(fb->GetFlags() == 0xd && fb->IsFlagSet(4) && !fb->IsFlagSet(2));
    CHECK(fb->GetRowHeight() == 20 && fb->GetDriveListSize() == 8);
    cRZString t;
    fb->GetParentDirectoryText(t);
    CHECK(t.str() == "..");

    // ---- filters ----
    FakeCountedBuffer icon;
    Combo combo;
    fb->SetFilterCombo(&combo);
    cRZString allf("*"), txt("*.txt");
    CHECK(fb->AddFilter(allf, nullptr, 7, 0));
    CHECK(fb->AddFilter(txt, &icon, 8, 0));
    CHECK(icon.refs == 1);
    CHECK(fb->FilterCount(0) == 2 && fb->FilterSetCount() == 1);
    cRZString got; cIGZBuffer* gi = nullptr; std::uint32_t gid = 0;
    CHECK(fb->GetFilter(1, got, gi, gid, 0xffffffff) && got.str() == "*.txt" && gi == &icon && gid == 8);
    CHECK(!fb->GetFilter(2, got, gi, gid, 0) && !fb->GetFilter(0, got, gi, gid, 5));
    CHECK(fb->AddFilter(allf, nullptr, 9, 2));              // grows the set vector
    CHECK(fb->FilterSetCount() == 3);
    cRZString desc("Text files");
    CHECK(fb->AddFilterSetDescription(desc, 1));
    CHECK(combo.items.size() == 1 && combo.items[0].first == "Text files" && combo.items[0].second == 1);
    CHECK(fb->SetCurrentFilterSet(2) && combo.selected == 2 && fb->CurrentFilterSet() == 2);
    CHECK(!fb->SetCurrentFilterSet(3));
    CHECK(!fb->RemoveFilter(99u, 0));
    CHECK(fb->RemoveFilter(8u, 0) && icon.refs == 0 && fb->FilterCount(0) == 1);
    CHECK(fb->RemoveFilter(allf, 0) && fb->FilterCount(0) == 0);
    CHECK(!fb->RemoveFilter(allf, 0));
    fb->AddFilter(txt, &icon, 1, 1);
    fb->RemoveAllFilters();
    CHECK(fb->FilterSetCount() == 0 && icon.refs == 0 && fb->CurrentFilterSet() == 0);

    // ---- directories on disk ----
    fs::path root = fs::path(getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp") / "winfb_test";
    fs::remove_all(root);
    fs::create_directories(root / "a" / "b");
    std::string r = root.string() + "/";
    fb->InitKnownInitialDirectory();                       // no drive list: "/"
    cRZString defd; fb->GetDefaultDirectoryLocation(defd);
    CHECK(!fb->SetDefaultDirectoryLocation(cRZString("/definitely/not/here")));
    CHECK(fb->SetDefaultDirectoryLocation(cRZString((r + "a/").c_str())));
    cRZString cur; fb->GetCurrentDirectoryLocation(cur);
    CHECK(cur.str() == r + "a/");
    fb->GetDefaultDirectoryLocation(cur);                  // same body (quirk)
    CHECK(cur.str() == r + "a/");
    fb->ClearRelayout();
    CHECK(fb->SetCurrentDirectoryLocation(cRZString((r + "a/b/").c_str())) && fb->NeedsRelayout());
    CHECK(fb->CurrentDirectory() == r + "a/b/");
    notify.got.clear();
    CHECK(fb->GoToParentDirectory());
    CHECK(fb->CurrentDirectory() == r + "a/");
    CHECK(!notify.got.empty() && notify.got.back().type == kFileBrowserMsgPathChanged);   // UpdatePath announced it
    CHECK(!fb->ChangeDirectory(cRZString((r + "zzz/").c_str())));
    CHECK(fb->ChangeDirectory(cRZString((r + "a/b/").c_str())) && fb->CurrentDirectory() == r + "a/b/");

    // new directory: "New Folder", then "New Folder (2)", "(3)"
    CHECK(fb->CreateNewDirectory() && fs::is_directory(root / "a" / "b" / "New Folder"));
    CHECK(fb->CreateNewDirectory() && fs::is_directory(root / "a" / "b" / "New Folder (2)"));
    CHECK(fb->CreateNewDirectory() && fs::is_directory(root / "a" / "b" / "New Folder (3)"));
    fb->SetNewDirectoryName(cRZString("Docs"));
    Grid grid;
    grid.names = {"x", "Docs", "y"};
    fb->SetGrid(&grid);
    CHECK(fb->CreateNewDirectory() && fs::is_directory(root / "a" / "b" / "Docs"));
    CHECK(grid.scrolled == 1 && grid.firstRow == 1);       // scrolled to the new entry and made visible
    grid.visible = true; grid.firstRow = -1;
    fb->CreateNewDirectory();
    CHECK(grid.firstRow == -1);
    CHECK(fb->GetRowOfEntryByName(cRZString("Docs")) == 1);
    CHECK(fb->GetRowOfEntryByName(cRZString("nope")) == 0);
    fb->SetFileListLayout(5, 1, 0);                        // layout without a name column falls back
    CHECK(fb->GetRowOfEntryByName(cRZString("Docs")) == 1);

    // delete: only below the current directory, never the directory itself
    CHECK(!fb->DeleteDirectory(cRZString((r + "a/").c_str())));
    CHECK(!fb->DeleteDirectory(cRZString((r + "a/b/").c_str())));
    // Quirk (cRZDirectory::RemoveDirectoryA answers true when rmdir FAILS, 0x100033f08): a successful delete reports false,
    // a failed one (non-empty directory) reports true and runs UpdatePath.
    CHECK(!fb->DeleteDirectory(cRZString((r + "a/b/Docs").c_str())) && !fs::exists(root / "a" / "b" / "Docs"));
    std::ofstream(root / "a" / "b" / "New Folder" / "keep") << "x";
    CHECK(fb->DeleteDirectory(cRZString((r + "a/b/New Folder").c_str())) && fs::exists(root / "a" / "b" / "New Folder"));
    CHECK(!fb->DeleteDirectory(cRZString("/nonexistent/x")));

    // selection by path
    std::ofstream(root / "a" / "b" / "f.txt") << "x";
    CHECK(!fb->SetSelection(cRZString("/no/such/dir/file.txt")));
    CHECK(fb->SetSelection(cRZString((r + "a/b/f.txt").c_str())));
    FileBrowserSelection sel;
    Edit edit;
    CHECK(fb->GetSelectionInternal(sel) && sel.kind == 2 && sel.name->str() == "f.txt" && sel.fullPath->str() == r + "a/b/f.txt" && sel.directory->str() == r + "a/b/");
    fb->SetFileNameEdit(&edit);
    edit.text = "other.bin";
    CHECK(fb->GetSelection(sel) && sel.name->str() == "other.bin" && sel.fullPath->str() == r + "a/b/other.bin");
    fb->SetDefaultSelection(1, cRZString("name"), cRZString(".ext"));
    std::uint32_t dk = 0; cRZString dn, dummy("keep");
    CHECK(fb->GetDefaultSelection(dk, dn, dummy) && dk == 1 && dn.str() == "name.ext" && dummy.str() == "keep");

    // ---- entry handlers ----
    auto addEntry = [&](std::uint32_t type, const char* name, std::uint32_t field) {
        auto* e = new cGZWinFileBrowser::FileListEntry;
        e->type = type; e->name.AssignFromCString(name); e->field = field;
        fb->FileEntries().push_back(e);
    };
    addEntry(kFileEntryParent, "..", 0);
    addEntry(kFileEntryDirectory, "New Folder", 0);
    addEntry(kFileEntryFile, "f.txt", 77);
    addEntry(0x1234, "weird", 0);
    fb->SetFileNameEdit(&edit);
    notify.got.clear();
    CHECK(fb->HandleSelectionChange(2));
    CHECK(fb->SelectedRow() == 2 && edit.text == "f.txt");
    CHECK(notify.got.size() == 1 && notify.got[0].type == kFileBrowserMsgSelectionChanged && notify.got[0].param1 == fb->GetID());
    {
        CHECK(notify.selections.size() == 1);
        const auto& s = notify.selections.back();
        CHECK(s.kind == 2 && s.field == 77 && s.name == "f.txt" && s.fullPath == fb->CurrentDirectory() + "f.txt");
    }
    CHECK(fb->HandleSelectionChange(1));
    fb->GetSelectionInternal(sel);
    CHECK(sel.kind == 1 && sel.name->str() == "New Folder/" && sel.fullPath->str() == fb->CurrentDirectory() + "New Folder/");
    CHECK(fb->HandleSelectionChange(0));
    fb->GetSelectionInternal(sel);
    CHECK(sel.kind == 0 && sel.name->str() == "../");
    CHECK(!fb->HandleSelectionChange(3) && !fb->HandleSelectionChange(9) && !fb->HandleSelectionChange(-1));
    notify.got.clear();
    CHECK(fb->HandleSelectionDoubleClick(2));
    CHECK(notify.got.size() == 1 && notify.got[0].type == kFileBrowserMsgSelectionChosen);
    CHECK(!fb->HandleSelectionDoubleClick(3));
    // double click on a directory enters it; on the parent goes up
    std::string before = fb->CurrentDirectory();
    CHECK(fb->HandleFileListDoubleClick(1) && fb->CurrentDirectory() == before + "New Folder/");
    CHECK(fb->HandleFileListDoubleClick(0) && fb->CurrentDirectory() == before);
    CHECK(fb->HandleFileListDoubleClick(2));               // a file: nothing happens, still true
    CHECK(!fb->HandleFileListDoubleClick(40));

    // ---- drive list click rebuilds the path ----
    auto addDrive = [&](const char* n, int row) {
        auto* d = new cGZWinFileBrowser::DriveListEntry;
        d->name.AssignFromCString(n); d->row = row;
        fb->DriveEntries().push_back(d);
    };
    addDrive("", 0);
    addDrive((root.string().substr(1)).c_str(), 1);
    CHECK(fb->HandleDriveListClick(1));
    CHECK(fb->CurrentDirectory() == "/" + root.string().substr(1) + "/");
    fb->ClearDirectoryList();
    CHECK(fb->DriveEntries().empty());
    fb->InitKnownInitialDirectory();
    fb->ClearFileList();
    CHECK(fb->FileEntries().empty());

    // ---- flags ----
    fb->ClearRelayout();
    fb->SetFlag(2);                                        // private-buffer bit: window flags change
    CHECK(fb->IsFlagSet(2) && fb->GetFlag(0x10000) && !fb->GetFlag(0x20000) && !fb->GetFlag(0x40000));
    fb->ClearFlag(2);
    CHECK(!fb->IsFlagSet(2) && fb->GetFlag(0x10000) && fb->GetFlag(0x20000) && fb->GetFlag(0x40000));   // 0x10000 is never cleared again (kept)
    notify.got.clear();
    fb->ClearFlag(4);                                      // bit 4 shown/hidden changes the listing: UpdatePath runs
    CHECK(!notify.got.empty() && notify.got.back().type == kFileBrowserMsgPathChanged);
    fb->SetFlag(4);
    notify.got.clear();
    fb->SetFlags(fb->GetFlags());                          // unchanged: nothing
    CHECK(notify.got.empty());

    // ---- layout ----
    {
        std::uint32_t cols[3] = {1, 2, 0};
        CHECK(fb->SetFileListLayout(cols, 3));
        std::uint32_t out[6] = {}, n = 6;
        CHECK(fb->GetFileListLayout(out, n) && n == 3 && out[0] == 1 && out[1] == 2 && out[2] == 0);
        std::uint32_t a, b, c;
        fb->GetFileListLayout(a, b, c);
        CHECK(a == 1 && b == 2 && c == 5);   // the walk is one pass over the slots: slot 0 (position 2) is passed before it is wanted (kept)
        std::uint32_t bad[2] = {0, 2};                     // no name column
        CHECK(!fb->SetFileListLayout(bad, 2));
        CHECK(fb->ColumnCount() == 2 && fb->Column(0).enabled && fb->Column(1).enabled && fb->Column(1).position == 1);
        CHECK(!fb->SetFileListLayout(nullptr, 0));
        std::uint32_t big[7] = {1, 9, 3, 4, 5, 0, 2};      // ids >= 6 are ignored
        CHECK(fb->SetFileListLayout(big, 7) && fb->Column(2).position == 6);
    }
    fb->SetFileSort(3, 1); fb->SetDirectorySort(2);
    std::uint32_t k, o, d; CHECK(fb->GetFileSort(k, o) && k == 3 && o == 1 && fb->GetDirectorySort(d) && d == 2);

    // ---- formats, grid lines, gutters, misc ----
    FakeCharFont font;
    fb->SetNameFieldFont(&font);
    CHECK(font.refs == 1 && fb->GetNameFieldFont() == &font);
    fb->SetNameFieldFont(&font);
    CHECK(font.refs == 1);
    fb->SetNameFieldFont(nullptr);
    CHECK(font.refs == 0);
    fb->SetTypeFieldColors(2, 0x123); fb->SetDriveFieldColors(1, 0x456); fb->SetNameFieldColors(0, 0x789);
    CHECK(fb->GetTypeFieldColors(2) == 0x123 && fb->GetDriveFieldColors(1) == 0x456 && fb->GetNameFieldColors(0) == 0x789);
    fb->SetNameFieldAlignment(-1); CHECK(fb->GetNameFieldAlignment() == -1);
    fb->SetTypeFieldAlignment(2); fb->SetDriveFieldAlignment(1);
    CHECK(fb->GetTypeFieldAlignment() == 2 && fb->GetDriveFieldAlignment() == 1);
    fb->SetHighlightColor(0xaa, 0xbb);
    std::uint32_t hf, hb;
    fb->GetHighlightColor(hf, hb);
    CHECK(hf == 0xaa && hb == 0xbb && fb->GetTypeFieldColors(4) == 0xaa && fb->GetDriveFieldColors(5) == 0xbb);
    bool on; std::uint32_t col;
    fb->SetLineGrid(true, 0x55); fb->GetLineGrid(on, col); CHECK(on && col == 0x55);
    fb->SetLineGrid(false, 0x55); fb->GetLineGrid(on, col); CHECK(!on && col == 0x40000000);
    fb->SetColumnGrid(true, 0x66); fb->GetColumnGrid(on, col); CHECK(on && col == 0x66);
    fb->SetGutters(1, 2, 3); std::uint32_t g1, g2, g3; fb->GetGutters(g1, g2, g3); CHECK(g1 == 1 && g2 == 2 && g3 == 3);
    fb->SetDriveListSize(2); CHECK(fb->GetDriveListSize() == 4);
    fb->SetDriveListSize(12); CHECK(fb->GetDriveListSize() == 12);
    fb->SetRowHeight(30); fb->SetIconColumnWidth(5); fb->SetTypeColumnWidth(6);
    CHECK(fb->GetRowHeight() == 30 && fb->GetIconColumnWidth() == 5 && fb->GetTypeColumnWidth() == 6);
    fb->SetOutlineUse(true); CHECK(fb->GetOutlineUse());
    fb->SetDefaultFont(&font); CHECK(fb->GetDefaultFont() == &font && font.refs == 0);
    fb->SetParentDirectoryText(cRZString("Up")); fb->GetParentDirectoryText(t); CHECK(t.str() == "Up");
    fb->SetIconBuffer(3, &icon); CHECK(icon.refs == 1 && fb->IconBuffer(3) == &icon);
    fb->SetIconBuffer(3, nullptr); CHECK(icon.refs == 0);
    fb->SetIconBuffer(7, &icon); CHECK(icon.refs == 0);   // out of range ignored
    fb->SetButtonBuffer(2, &icon); CHECK(icon.refs == 1);
    fb->SetBackgroundImage(&icon); CHECK(icon.refs == 2 && fb->GetBackgroundImage() == &icon);
    CHECK(!fb->GetBackgroundTransparent());
    fb->SetBackgroundColor(0xff102030);
    CHECK(fb->GetBackgroundColor() == 0xff101010);        // quirk: the red byte for all three channels
    fb->SetBackgroundTransparent(true);
    CHECK(fb->GetBackgroundTransparent() && fb->GetFlag(0x20000) && fb->GetFlag(0x40000));

    // ---- FitStringIntoWidth ----
    {
        FakeCharFont f; f.charWidth = 10;
        cRZString s("abcdefghij");                        // 100 px
        fb->FitStringIntoWidth(s, 100, &f);  CHECK(s.str() == "abcdefghij");
        s.AssignFromCString("abcdefghij");
        fb->FitStringIntoWidth(s, 60, &f);                // "..." is 30 px: 3 chars + "..."
        CHECK(s.str() == "abc...");
        host.haveEllipsis = true;
        s.AssignFromCString("abcdefghij");
        fb->FitStringIntoWidth(s, 50, &f);                // "~" is 10 px: 4 chars + "~"
        CHECK(s.str() == "abcd~");
        s.AssignFromCString("abcdefghij");
        fb->FitStringIntoWidth(s, 5, &f);                 // nothing fits
        CHECK(s.str().empty());
        s.AssignFromCString("ab");
        fb->FitStringIntoWidth(s, 20, nullptr);           // no font: untouched
        CHECK(s.str() == "ab");
        cRZString e;
        fb->FitStringIntoWidth(e, 20, &f);
        CHECK(e.str().empty());
        host.haveEllipsis = false;
        s.AssignFromCString("abcdefghij");
        fb->FitStringIntoWidth(s, 25, &f);                // width 25 < ... (30): everything dropped
        CHECK(s.str().empty());
    }

    // ---- EndDialog ----
    fb->EndDialog(5);                                     // not modal and without a parent: nothing posted
    fb->SetFlag(std::uint32_t(0x1000), true);
    fb->EndDialog(5);
    CHECK(mgr.dialogs.size() == 1 && mgr.dialogs[0].code == 0xf && mgr.dialogs[0].window == static_cast<cGZWin*>(fb) && mgr.dialogs[0].result == 5);

    fs::remove_all(root);
    fb->Release();
    if (failures) { std::printf("gz_winfilebrowser FAILED (%d)\n", failures); return 1; }
    std::printf("gz_winfilebrowser OK\n");
    return 0;
}

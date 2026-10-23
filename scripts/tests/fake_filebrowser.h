// A recording cIGZWinFileBrowser for dialog tests (the real cGZWinFileBrowser needs the whole control set).
#pragma once
#include <string>
#include <vector>

#include "fake_winmgr2.h"
#include "gz/WinFileBrowser.h"

struct FakeFileBrowser final : cIGZWinFileBrowser {
    cGZWin* window; int refs = 1;
    std::vector<std::string> filters, calls; std::string defaultDir, currentDir = "/tmp/cur", filterSetText; std::uint32_t set = 99;
    FileBrowserSelection selection; cRZString selPath{"x"};
    explicit FakeFileBrowser(cGZWin* w) : window(w) {}
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    cIGZWin* AsIGZWin() override { return window; }
    void EndDialog(std::uint32_t) override {}
    void SetFlag(int) override {}
    void SetFlags(int) override {}
    void ClearFlag(int) override {}
    bool IsFlagSet(int) override { return false; }
    int GetFlags() override { return 0; }
    void* GetChildControl(std::uint32_t) override { return nullptr; }
    void EnableChildControl(std::uint32_t, bool) override {}
    void SetIconBuffer(std::uint32_t, cIGZBuffer*) override {}
    void SetButtonBuffer(std::uint32_t, cIGZBuffer*) override {}
    bool GetDefaultDirectoryLocation(cIGZString& out) const override { out.AssignFromCString(defaultDir.c_str()); return true; }
    bool SetDefaultDirectoryLocation(const cIGZString& p) override { defaultDir = p.ToChar(); calls.push_back("setdefault"); return true; }
    bool GetCurrentDirectoryLocation(cIGZString& out) const override { out.AssignFromCString(currentDir.c_str()); return true; }
    bool SetCurrentDirectoryLocation(const cIGZString& p) override { currentDir = p.ToChar(); calls.push_back("setcurrent"); return true; }
    bool GetSelection(FileBrowserSelection& out) override { out = selection; return true; }
    bool SetSelection(const cIGZString&) override { return true; }
    bool GetFilter(std::uint32_t, cIGZString&, cIGZBuffer*&, std::uint32_t&, std::uint32_t) const override { return false; }
    bool AddFilter(const cIGZString& p, cIGZBuffer*, std::uint32_t, std::uint32_t) override { filters.push_back(p.ToChar()); return true; }
    bool AddFilterSetDescription(const cIGZString& t, std::uint32_t) override { filterSetText = t.ToChar(); return true; }
    bool RemoveFilter(std::uint32_t, std::uint32_t) override { return true; }
    bool RemoveFilter(const cIGZString&, std::uint32_t) override { return true; }
    void RemoveAllFilters() override {}
    bool SetCurrentFilterSet(std::uint32_t s) override { set = s; return true; }
    bool GetDefaultSelection(std::uint32_t&, cIGZString&, cIGZString&) const override { return true; }
    bool SetDefaultSelection(std::uint32_t, const cIGZString&, const cIGZString&) override { return true; }
    bool SetNewDirectoryName(const cIGZString&) override { return true; }
    bool GoToParentDirectory() override { return true; }
    bool ChangeDirectory(const cIGZString&) override { return true; }
    bool CreateNewDirectory() override { return true; }
    bool DeleteDirectory(const cIGZString&) override { return true; }
};

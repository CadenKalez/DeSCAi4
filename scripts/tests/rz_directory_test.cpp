// cRZDirectory: listing, pseudo entries, string lists, recursion, directory creation and path splitting.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "rz/cRZDirectory.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static void Touch(const std::string& path) { std::ofstream(path) << "x"; }

int main() {
    char templ[] = "/tmp/rzdirXXXXXX";
    const std::string root = mkdtemp(templ);
    // path splitting
    {
        char buffers[4][256];
        int count = 2;
        CHECK(SplitDirectory("/Users/x/data", buffers, count) && count == 2);
        CHECK(std::string(buffers[0]) == "data/" && std::string(buffers[1]) == "/Users/x/");
        count = 2;
        CHECK(SplitDirectory("/a/b/", buffers, count) && std::string(buffers[0]) == "b/" && std::string(buffers[1]) == "/a/");
        count = 4;
        CHECK(SplitDirectory("/a/b/c", buffers, count) && count == 3 && std::string(buffers[0]) == "c/" && std::string(buffers[1]) == "b/" &&
              std::string(buffers[2]) == "a/");
        count = 2;
        CHECK(!SplitDirectory("", buffers, count) && count == 0);
        cRZString name, directory;
        CHECK(cRZDirectory::SplitDirectoryPath(cRZString("/m/n/o"), name, directory) && std::string(name.ToChar()) == "o/" &&
              std::string(directory.ToChar()) == "/m/n/");
    }
    // creation: every missing level, then the existing path is fine
    CHECK(cRZDirectory::CreateDirectoryA(cRZString((root + "/p/q/r").c_str())));
    CHECK(cRZDirectory::DoesDirectoryExist(cRZString((root + "/p/q/r").c_str())) && cRZDirectory::CreateDirectoryA(cRZString((root + "/p/q").c_str())));
    Touch(root + "/a.txt");
    Touch(root + "/b.dat");
    Touch(root + "/p/q/r/deep.txt");
    mkdir((root + "/sub").c_str(), 0777);
    Touch(root + "/sub/inner.txt");

    cRZDirectory dir(cRZString(root.c_str()), true, false, false);
    CHECK(dir.Path() == root + "/");
    // a.txt, b.dat, p, sub (order depends on the file system)
    CHECK(dir.GetDirectoryEntryCount() == 4);
    CHECK(dir.DoesDirectoryListContainEntry(cRZString("a.txt")) && dir.DoesDirectoryListContainEntry(cRZString("sub")));
    CHECK(dir.GetNthEntry(0) != nullptr && dir.GetNthEntry(4) == nullptr);

    std::list<cRZString> names;
    CHECK(dir.ReadDirectoryFileEntriesIntoStringList(names) && names.size() == 2);
    names.clear();
    dir.ReadDirectoryDirectoryEntriesIntoStringList(names, 0);
    CHECK(names.size() == 2);
    names.clear();
    dir.ReadDirectoryDirectoryEntriesIntoStringList(names, 3);       // with "." and ".."
    CHECK(names.size() == 4 && std::string(names.front().ToChar()) == "." );

    // filters
    dir.SetNewDirectoryEntryFilter(cRZString("*.txt"), true);
    CHECK(dir.GetDirectoryEntryCount() == 1);
    dir.SetNewDirectoryEntryFilter(cRZString(""), true);
    CHECK(dir.Filter() == "*" && dir.GetDirectoryEntryCount() == 4);

    // pseudo entries are added in front when requested
    cRZDirectory withDots(cRZString(root.c_str()), true, true, true);
    CHECK(withDots.GetDirectoryEntryCount() >= 4 + 0);
    CHECK(withDots.DoesDirectoryListContainEntry(cRZString(".")) && withDots.DoesDirectoryListContainEntry(cRZString("..")));

    // recursion lists the files of every level as full paths
    std::list<cRZString> all;
    cRZDirectory walker(cRZString(root.c_str()), false, false, false);
    CHECK(walker.ReadDirectoryFileEntriesIntoStringListRecursive(cRZString((root + "/").c_str()), all, true));
    CHECK(all.size() == 4);              // a.txt, b.dat, p/q/r/deep.txt, sub/inner.txt
    CHECK(cRZDirectory::DoesAnyEntryExistThatMatchesPattern(cRZString((root + "/*.dat").c_str())));
    CHECK(!cRZDirectory::DoesAnyEntryExistThatMatchesPattern(cRZString((root + "/*.zzz").c_str())));

    std::list<cRZString> sorted = {cRZString("pear"), cRZString("apple"), cRZString("fig")};
    cRZDirectory::AlphabetizeEntryList(sorted);
    CHECK(std::string(sorted.front().ToChar()) == "apple" && std::string(sorted.back().ToChar()) == "pear");

    // rmdir fails on a non-empty directory: the original reports that as true
    CHECK(cRZDirectory::RemoveDirectoryA(cRZString((root + "/sub").c_str())));
    std::string cleanup = "rm -rf " + root;
    (void)std::system(cleanup.c_str());
    if (failures == 0) std::puts("rz_directory: all passed");
    return failures ? 1 : 0;
}

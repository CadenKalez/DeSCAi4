#include <cstdio>
#include <cstring>
#include "rz/PathUtils.h"
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static std::string Join(const char* path, const char* base) {
    std::string out;
    return Fullpath(out, path, base) ? out : std::string("<fail>");
}
int main() {
    cRZString a, b, out;
    a.AssignFromCString("/Users/me/file.txt");
    CHECK(IsFullPath(a));
    a.AssignFromCString("/Users/./me");
    CHECK(!IsFullPath(a));
    a.AssignFromCString("relative/path");
    CHECK(!IsFullPath(a));

    CHECK(Join("sub/file.txt", "/base") == "/base/sub/file.txt");
    CHECK(Join("a/./b/../c", "/base/") == "/base/a/c");
    CHECK(Join("dir/", "/base") == "/base/dir/");
    CHECK(Join("../x", "/base/y") == "/base/x");
    b.AssignFromCString("/root");
    a.AssignFromCString("sub/../other");
    CHECK(ResolvePath(out, a, &b) && std::strcmp(out.ToChar(), "/root/other") == 0);
    a.AssignFromCString("/abs/path");
    CHECK(ResolvePath(out, a, &b) && std::strcmp(out.ToChar(), "/abs/path") == 0);

    cRZString ntfs;
    ntfs.AssignFromCString("NTFS");
    a.AssignFromCString("report.txt");
    CHECK(IsFileNameValid(a, nullptr));
    a.AssignFromCString("a?b.txt");
    CHECK(!IsFileNameValid(a, nullptr));
    a.AssignFromCString("CON.txt");
    CHECK(IsFileNameValid(a, nullptr) && !IsFileNameValid(a, &ntfs));
    a.AssignFromCString("com1");
    CHECK(!IsFileNameValid(a, &ntfs));
    a.AssignFromCString(" . ");
    CHECK(!IsFileNameValid(a, nullptr));
    a.AssignFromCString("my|bad*name?.txt");
    CHECK(MakeFileNameValid(a, out, nullptr) && std::strcmp(out.ToChar(), "mybadname.txt") == 0);

    CHECK(MatchPattern("hello.txt", "*.txt") && MatchPattern("hello.txt", "h?llo.*") && !MatchPattern("hello.txt", "*.dat"));
    CHECK(MatchPattern("abc", "a*") && MatchPattern("", "*") && !MatchPattern("abc", "abcd"));
    a.AssignFromCString("README.MD");
    b.AssignFromCString("*.md");
    CHECK(WildcardMatch(a, b, false) && !WildcardMatch(a, b, true));
    std::puts(failures ? "rz_pathutils: FAILED" : "rz_pathutils ok");
    return failures != 0;
}

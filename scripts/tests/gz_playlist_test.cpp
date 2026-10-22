// Tests cGZPlayList: text helpers, path resolution and the three playlist formats loaded from real files.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "gz/PlayList.h"

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static std::string Resolve(const char* base, const char* path) {
    cRZString out;
    cGZPlayList::MakeAbsolutePath(cRZString(base), cRZString(path), out);
    return out.ToChar();
}

static std::filesystem::path Write(const char* name, const std::string& content) {
    const auto p = std::filesystem::temp_directory_path() / name;
    std::ofstream(p, std::ios::binary) << content;
    return p;
}

static bool OpenFile(cGZPlayList& list, const std::filesystem::path& p) { return list.Open(cRZString(p.string().c_str())); }

int main() {
    // ---- FindNotOneOf / FindOneOf
    {
        std::uint32_t i = 99;
        CHECK(cGZPlayList::FindNotOneOf(cRZString("  \tabc"), cRZString(" \t"), i) && i == 3);
        CHECK(!cGZPlayList::FindNotOneOf(cRZString("   "), cRZString(" "), i) && i == 3);
        CHECK(!cGZPlayList::FindNotOneOf(cRZString(""), cRZString(" "), i) && i == 0);
        CHECK(cGZPlayList::FindNotOneOf(cRZString("abc"), cRZString(""), i) && i == 0);   // an empty set matches nothing
        CHECK(cGZPlayList::FindOneOf(cRZString("ab\r\ncd"), cRZString("\r\n"), i) && i == 2);
        CHECK(!cGZPlayList::FindOneOf(cRZString("abcd"), cRZString("\r\n"), i) && i == 4);
    }
    // ---- GetLine
    {
        cRZString text("  \r\n first line \r\n\r\n  second\nthird");
        cRZString line;
        CHECK(cGZPlayList::GetLine(text, line) && std::string(line.ToChar()) == "first line");
        CHECK(cGZPlayList::GetLine(text, line) && std::string(line.ToChar()) == "second");
        CHECK(cGZPlayList::GetLine(text, line) && std::string(line.ToChar()) == "third" && text.Strlen() == 0);   // no final line end: consumed
        CHECK(!cGZPlayList::GetLine(text, line));
        cRZString blank("  \r\n ");
        CHECK(!cGZPlayList::GetLine(blank, line));
    }
    // ---- segments
    {
        cRZString src("abc\\def\\g"), seg;
        CHECK(cGZPlayList::GrabSegment(src, seg, 0) && std::string(seg.ToChar()) == "abc\\" && std::string(src.ToChar()) == "def\\g");
        CHECK(cGZPlayList::GrabSegment(src, seg, 0) && std::string(seg.ToChar()) == "def\\");
        CHECK(cGZPlayList::GrabSegment(src, seg, 0) && std::string(seg.ToChar()) == "g" && src.Strlen() == 0);
        cRZString shortSrc("a");
        CHECK(!cGZPlayList::GrabSegment(shortSrc, seg, 2));
        cRZString p1("C:\\a\\b\\");
        CHECK(cGZPlayList::StripEndSegment(p1) && std::string(p1.ToChar()) == "C:\\a\\");
        CHECK(cGZPlayList::StripEndSegment(p1) && std::string(p1.ToChar()) == "C:\\");
        cRZString p2("nodir");
        CHECK(cGZPlayList::StripEndSegment(p2) && p2.Strlen() == 0);   // no separator: emptied
        cRZString root, spec;
        root = cRZString("C:\\music\\x.mp3");
        CHECK(cGZPlayList::IsSpec(root, spec) && std::string(spec.ToChar()) == "C:\\" && std::string(root.ToChar()) == "music\\x.mp3");
        cRZString rel("music\\x.mp3");
        CHECK(!cGZPlayList::IsSpec(rel, spec));
    }
    // ---- MakeAbsolutePath
    CHECK(Resolve("C:\\music\\", "song.mp3") == "C:\\music\\song.mp3");
    CHECK(Resolve("C:\\music\\", "sub\\song.mp3") == "C:\\music\\sub\\song.mp3");
    CHECK(Resolve("C:\\music\\", ".\\song.mp3") == "C:\\music\\song.mp3");
    CHECK(Resolve("C:\\music\\a\\", "..\\x\\y.mp3") == "C:\\music\\x\\y.mp3");
    CHECK(Resolve("C:\\music\\a\\b\\", "...\\y.mp3") == "C:\\music\\y.mp3");
    CHECK(Resolve("C:\\music\\", "D:\\q\\r.mp3") == "D:\\q\\r.mp3");
    CHECK(Resolve("C:\\music\\", "\\\\srv\\share\\f.mp3") == "\\\\srv\\share\\f.mp3");
    CHECK(Resolve("", "a\\b.mp3") == "a\\b.mp3");
    {
        cRZString inPlace("..\\z.mp3");
        cGZPlayList::MakeAbsolutePath(cRZString("C:\\a\\b\\"), inPlace, inPlace);   // output may alias the input
        CHECK(std::string(inPlace.ToChar()) == "C:\\a\\z.mp3");
    }

    // ---- plain m3u: the first line is an entry, titles are the file names without extension
    {
        const auto f = Write("sc4_pl_plain.m3u", "one\\a.mp3\r\n\r\nC:\\x\\b.ogg\n  c.mp3  ");
        cGZPlayList list;
        CHECK(OpenFile(list, f));
        CHECK(list.Entries().size() == 3);
        if (list.Entries().size() == 3) {
            CHECK(std::string(list.Entries()[0].path.ToChar()) == "one\\a.mp3" && std::string(list.Entries()[0].title.ToChar()) == "one\\a");   // GetPathName splits on '/' only
            CHECK(std::string(list.Entries()[1].path.ToChar()) == "C:\\x\\b.ogg" && std::string(list.Entries()[1].title.ToChar()) == "C:\\x\\b");
            CHECK(std::string(list.Entries()[2].path.ToChar()) == "c.mp3" && list.Entries()[2].length == 0);
        }
        std::filesystem::remove(f);
    }
    // ---- extended m3u
    {
        const auto f = Write("sc4_pl_ext.m3u", "#EXTM3U\r\n#EXTINF:123,First Song\r\nd\\one.mp3\r\n#EXTINF:45,Second\r\ntwo.mp3\r\n");
        cGZPlayList list;
        CHECK(OpenFile(list, f));
        CHECK(list.Entries().size() == 2);
        if (list.Entries().size() == 2) {
            CHECK(list.Entries()[0].length == 123 && std::string(list.Entries()[0].title.ToChar()) == "First Song");
            CHECK(std::string(list.Entries()[0].path.ToChar()) == "d\\one.mp3");
            CHECK(list.Entries()[1].length == 45 && std::string(list.Entries()[1].title.ToChar()) == "Second");
        }
        std::filesystem::remove(f);
        // a line that is not #EXTINF stops the parse; entries read so far stay
        const auto g = Write("sc4_pl_ext2.m3u", "#EXTM3U\n#EXTINF:1,A\na.mp3\nB.mp3\n#EXTINF:2,C\nc.mp3\n");
        cGZPlayList stopped;
        CHECK(!OpenFile(stopped, g));   // Open returns the parser's result
        CHECK(stopped.Entries().size() == 1);
        std::filesystem::remove(g);
    }
    // ---- PLS: NumberOfEntries at the end completes the last entry; without it the last entry is dropped
    {
        const auto f = Write("sc4_pl_a.pls", "[playlist]\r\nFile1=x\\one.mp3\r\nTitle1=One\r\nLength1=60\r\nFile2=two.mp3\r\nTitle2=Two\r\nLength2=-1\r\nNumberOfEntries=2\r\nVersion=2\r\n");
        cGZPlayList list;
        CHECK(OpenFile(list, f));
        CHECK(list.Entries().size() == 2);
        if (list.Entries().size() == 2) {
            CHECK(std::string(list.Entries()[0].path.ToChar()) == "x\\one.mp3" && std::string(list.Entries()[0].title.ToChar()) == "One" && list.Entries()[0].length == 60);
            CHECK(std::string(list.Entries()[1].path.ToChar()) == "two.mp3" && std::string(list.Entries()[1].title.ToChar()) == "Two");
            CHECK(list.Entries()[1].length == std::uint32_t(-1));
        }
        std::filesystem::remove(f);
        const auto g = Write("sc4_pl_b.pls", "[playlist]\nFile1=a.mp3\nTitle1=A\nLength1=1\nFile2=b.mp3\nTitle2=B\nLength2=2\n");
        cGZPlayList dropped;
        CHECK(!OpenFile(dropped, g));
        CHECK(dropped.Entries().size() == 1);   // quirk: File2 is never completed
        std::filesystem::remove(g);
    }
    // ---- unreadable file
    {
        cGZPlayList list;
        CHECK(!list.Open(cRZString("/nonexistent/dir/none.m3u")));
    }
    std::printf(g_fail ? "FAILED %d\n" : "gz_playlist OK\n", g_fail);
    return g_fail ? 1 : 0;
}

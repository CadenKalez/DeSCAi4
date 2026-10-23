// cRZURL: parsing, rebuilding, relative resolution, canonicalization and percent-decoding.
// The expectations below come from the decompiled code (0x10023e7e0..0x100241890); the quirks are
// asserted on purpose, see src/rz/URL.h for why they are kept.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "rz/URL.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {

std::string S(const cRZString& s) { return s.str(); }

// The recovered object keeps the original field offsets; check the ones exposed by the getters.
std::uintptr_t Off(const void* base, const void* member) {
    return reinterpret_cast<std::uintptr_t>(member) - reinterpret_cast<std::uintptr_t>(base);
}

std::string Canon(const char* text) {
    cRZString s(text);
    cRZURL::CanonicalizePath(s);
    return s.str();
}

std::string Decode(const char* text) {
    cRZString out;
    cRZURL::ConvertPathFromURLToLocalForm(cRZString(text), out);
    return out.str();
}

}  // namespace

int main() {
    CHECK(sizeof(cRZString) == 0x28);
    {
        cRZURL layout;
        CHECK(Off(&layout, &layout.GetFullURL()) == 0x08);
        CHECK(Off(&layout, &layout.GetScheme()) == 0x30);
        CHECK(Off(&layout, &layout.GetUsername()) == 0x58);
        CHECK(Off(&layout, &layout.GetPassword()) == 0x80);
        CHECK(Off(&layout, &layout.GetServer()) == 0xa8);
        CHECK(Off(&layout, &layout.GetAbsolutePath()) == 0xd0);
        CHECK(Off(&layout, &layout.GetParameters()) == 0xf8);
        CHECK(Off(&layout, &layout.GetQuery()) == 0x120);
        CHECK(Off(&layout, &layout.GetFragment()) == 0x148);
    }

    // --- plain HTTP URL: every component, and the full URL is not rewritten by the getters ---------
    {
        cRZURL url(cRZString("http://www.example.com:8080/dir/file.html?x=1#frag"));
        CHECK(S(url.GetFullURL()) == "http://www.example.com:8080/dir/file.html?x=1#frag");
        CHECK(url.GetURLType() == cRZURL::kTypeHTTP);
        CHECK(S(url.GetScheme()) == "http");
        CHECK(S(url.GetUsername()).empty());
        CHECK(S(url.GetPassword()).empty());
        CHECK(S(url.GetServer()) == "www.example.com");
        CHECK(url.GetPort() == 8080);
        CHECK(S(url.GetAbsolutePath()) == "/dir/file.html");
        CHECK(S(url.GetParameters()).empty());
        CHECK(S(url.GetQuery()) == "?x=1");
        CHECK(S(url.GetFragment()) == "#frag");
        CHECK(S(url.GetFullURL()) == "http://www.example.com:8080/dir/file.html?x=1#frag");
        CHECK(url.MakeURLStringFromComponents());
        CHECK(S(url.GetFullURL()) == "http://www.example.com:8080/dir/file.html?x=1#frag");
    }

    // --- empty path defaults to "/" for every type except the relative one -------------------------
    {
        cRZURL url(cRZString("http://host"));
        CHECK(url.MakeURLStringFromComponents());   // forces the lazy parse, then rebuilds
        CHECK(S(url.GetFullURL()) == "http://host/");
        CHECK(S(url.GetServer()) == "host");
        CHECK(S(url.GetAbsolutePath()) == "/");

        cRZURL relative(cRZString(""));
        CHECK(relative.GetURLType() == cRZURL::kTypeRelative);
        CHECK(S(relative.GetAbsolutePath()).empty());
    }

    // --- quirk: "https" is typed as http because the scheme matcher returns on the "http" prefix ----
    {
        cRZURL url(cRZString("https://secure.example.com/a"));
        CHECK(url.GetURLType() == cRZURL::kTypeHTTP);
        CHECK(S(url.GetScheme()) == "https");
        CHECK(S(url.GetServer()) == "secure.example.com");
        CHECK(S(url.GetAbsolutePath()) == "/a");
    }

    // --- file URLs: authority is dropped, the whole rest is the path -------------------------------
    {
        cRZURL url(cRZString("file:///Users/me/f.txt"));
        CHECK(url.GetURLType() == cRZURL::kTypeFILE);
        CHECK(S(url.GetScheme()) == "file");
        CHECK(S(url.GetServer()).empty());
        CHECK(S(url.GetAbsolutePath()) == "/Users/me/f.txt");
        CHECK(url.MakeURLStringFromComponents());
        CHECK(S(url.GetFullURL()) == "file:///Users/me/f.txt");

        // quirk: the "localhost" branch of ParseURLFILE is dead; it stays part of the path.
        cRZURL localhost(cRZString("file://localhost/x"));
        CHECK(localhost.GetURLType() == cRZURL::kTypeFILE);
        CHECK(S(localhost.GetServer()).empty());
        CHECK(S(localhost.GetAbsolutePath()) == "localhost/x");
    }

    // --- quirk: an empty FTP path is assigned to the working string ("/") and then cleared, so
    // m_absolutePath stays empty (0x1002415bc..0x1002415f4 + 0x10024165c) ----------------------
    {
        cRZURL url(cRZString("ftp://host"));
        CHECK(S(url.GetAbsolutePath()).empty());
        CHECK(url.MakeURLStringFromComponents());
        CHECK(S(url.GetFullURL()) == "ftp://host");
    }

    // --- quirk: userinfo URLs lose the "//" while consuming the '@' ------------------------------
    {
        cRZURL url(cRZString("ftp://user:pass@ftp.example.com/pub/file"));
        CHECK(url.GetURLType() == cRZURL::kTypeFTP);
        CHECK(S(url.GetUsername()) == "//user");
        CHECK(S(url.GetPassword()) == "pass");
        CHECK(S(url.GetServer()).empty());                          // ParseServerAndPort never sees "//"
        CHECK(S(url.GetAbsolutePath()) == "ftp.example.com/pub/file");
        CHECK(url.GetPort() == 0);
        // quirk: rebuilding writes ':' + the username again (never the password).
        CHECK(url.MakeURLStringFromComponents());
        CHECK(S(url.GetFullURL()) == "ftp:////user://user@ftp.example.com/pub/file");
    }

    // --- quirk: the "mailto:" prefix sets the scheme but not the type ------------------------------
    {
        cRZURL url(cRZString("mailto:bob@example.com"));
        CHECK(url.GetURLType() == cRZURL::kTypeNone);
        CHECK(S(url.GetScheme()) == "mailto");
        CHECK(S(url.GetAbsolutePath()).empty());
        CHECK(S(url.GetFullURL()) == "mailto:bob@example.com");   // never modified by the parse
        CHECK(S(url.GetServer()).empty());
    }

    // --- generic "scheme:rest" without "//" becomes type 1 with the rest as the path --------------
    {
        cRZURL url(cRZString("news:comp.lang.c"));
        CHECK(url.GetURLType() == cRZURL::kTypeUnknownScheme);
        CHECK(S(url.GetScheme()) == "news");
        CHECK(S(url.GetAbsolutePath()) == "comp.lang.c");
        CHECK(url.MakeURLStringFromComponents());
        CHECK(S(url.GetFullURL()) == "news://comp.lang.c");        // builder always adds "//" for type != 6
    }

    // --- news / nntp / irc via "//" ---------------------------------------------------------------
    {
        // quirk: news/nntp do not run the server parser; the authority stays inside the path.
        cRZURL news(cRZString("news://news.example.com/comp.lang.c"));
        CHECK(news.GetURLType() == cRZURL::kTypeNews);
        CHECK(S(news.GetScheme()) == "news");
        CHECK(S(news.GetServer()).empty());
        CHECK(S(news.GetAbsolutePath()) == "//news.example.com/comp.lang.c");

        cRZURL nntp(cRZString("nntp://news.example.com/alt.test"));
        CHECK(nntp.GetURLType() == cRZURL::kTypeNews);
        CHECK(S(nntp.GetServer()).empty());
        CHECK(S(nntp.GetAbsolutePath()) == "//news.example.com/alt.test");

        cRZURL irc(cRZString("irc://irc.example.com/channel"));
        CHECK(irc.GetURLType() == cRZURL::kTypeIRC);
        CHECK(S(irc.GetServer()) == "irc.example.com");
        CHECK(S(irc.GetAbsolutePath()) == "/channel");
    }

    // --- unknown scheme followed by "//" parses as a relative URL (type 9) ------------------------
    {
        cRZURL url(cRZString("xyz://host/path"));
        CHECK(url.GetURLType() == cRZURL::kTypeRelative);
        CHECK(S(url.GetScheme()) == "xyz");
        CHECK(S(url.GetServer()) == "host");
        CHECK(S(url.GetAbsolutePath()) == "/path");
    }

    // --- the cRZString constructor enables explicit schemes; SetFullURL keeps the guess mode ------
    {
        cRZString text("www.example.com/page");
        cRZURL guessed;
        guessed.SetFullURL(text, true);
        CHECK(guessed.GetURLType() == cRZURL::kTypeHTTP);
        CHECK(S(guessed.GetServer()) == "www.example.com");
        CHECK(S(guessed.GetAbsolutePath()) == "/page");

        cRZURL explicitURL(text);
        CHECK(explicitURL.GetURLType() == cRZURL::kTypeRelative);
        CHECK(S(explicitURL.GetServer()).empty());
        CHECK(S(explicitURL.GetAbsolutePath()) == "www.example.com/page");

        cRZString colon("a:b");
        cRZURL explicitScheme(colon);
        CHECK(explicitScheme.GetURLType() == cRZURL::kTypeUnknownScheme);
        CHECK(S(explicitScheme.GetScheme()) == "a");
        CHECK(S(explicitScheme.GetAbsolutePath()) == "b");

        cRZURL guessedScheme;
        guessedScheme.SetFullURL(colon, true);
        CHECK(guessedScheme.GetURLType() == cRZURL::kTypeNone);
        CHECK(S(guessedScheme.GetScheme()).empty());
        CHECK(S(guessedScheme.GetAbsolutePath()).empty());
    }

    // --- "URL:" prefix is stripped --------------------------------------------------------------
    {
        cRZURL url(cRZString("URL:http://host/x"));
        CHECK(url.GetURLType() == cRZURL::kTypeHTTP);
        CHECK(S(url.GetScheme()) == "http");
        CHECK(S(url.GetServer()) == "host");
        CHECK(S(url.GetAbsolutePath()) == "/x");
    }

    // --- quirk: GetPort does not trigger the lazy parse ------------------------------------------
    {
        cRZURL url(cRZString("http://host:99/x"));
        CHECK(url.GetPort() == 0);
        CHECK(S(url.GetServer()) == "host");
        CHECK(url.GetPort() == 99);

        cRZURL noDigits(cRZString("http://host:/x"));
        CHECK(noDigits.GetURLType() == cRZURL::kTypeHTTP);
        CHECK(S(noDigits.GetServer()) == "host");
        CHECK(noDigits.GetPort() == 0);
        CHECK(S(noDigits.GetAbsolutePath()) == "/x");
    }

    // --- parameters, query and fragment splitting ------------------------------------------------
    {
        cRZURL url(cRZString("http://host/path;param?q=1#frag"));
        CHECK(S(url.GetServer()) == "host");
        CHECK(S(url.GetAbsolutePath()) == "/path");
        CHECK(S(url.GetParameters()) == ";param");
        CHECK(S(url.GetQuery()) == "?q=1");
        CHECK(S(url.GetFragment()) == "#frag");

        cRZURL simple(cRZString("http://host/path;param"));
        CHECK(S(simple.GetAbsolutePath()) == "/path");
        CHECK(S(simple.GetParameters()) == ";param");
        CHECK(S(simple.GetQuery()).empty());
        CHECK(S(simple.GetFragment()).empty());

        // quirk: with only a query (or fragment) the path is not defaulted and stays empty
        cRZURL queryOnly(cRZString("http://host?q=1"));
        CHECK(S(queryOnly.GetServer()) == "host");
        CHECK(S(queryOnly.GetQuery()) == "?q=1");
        CHECK(S(queryOnly.GetAbsolutePath()).empty());
    }

    // --- percent decoding (quirk: an invalid escape becomes one space) ---------------------------
    {
        CHECK(Decode("a%20b/%41%zz%2") == "a b/A %2");
        CHECK(Decode("plain") == "plain");
        CHECK(Decode("%2f") == "/");
        CHECK(Decode("%") == "%");
        CHECK(Decode("%4") == "%4");
    }

    // --- CanonicalizePath -----------------------------------------------------------------------
    {
        CHECK(Canon("/a/./b") == "/a/b");
        CHECK(Canon("/a/b/../c") == "/a/c");
        CHECK(Canon("/a/b/..") == "/a/");              // only the segment is removed, the "/" stays
        CHECK(Canon("a/..") == "");
        CHECK(Canon("/..") == "/..");              // shorter than four bytes: untouched
        CHECK(Canon(".") == "");
        CHECK(Canon("./a") == "a");
        CHECK(Canon("././a") == "./a");            // quirk: the restart offset skips the new leading "./"
        CHECK(Canon("a//b") == "a//b");            // "//" is never collapsed
        CHECK(Canon("/a/b/../../c") == "/c");
        CHECK(Canon("a/b/../..") == "");
        CHECK(Canon("a/./b/./c") == "a/b/c");
        CHECK(Canon("a/././b") == "a/./b");        // quirk: the search restarts past the removed "./"
        CHECK(Canon("a/../b") == "b");
        CHECK(Canon("/a/b/../") == "/a/");
    }

    // --- ChangeURL: relative reference against a base URL ---------------------------------------
    {
        cRZURL base(cRZString("http://host/a/b.html"));
        cRZURL url;
        CHECK(url.ChangeURL(cRZString("sub/file.html"), &base));
        CHECK(S(url.GetFullURL()) == "http://host/a/sub/file.html");
        CHECK(url.GetURLType() == cRZURL::kTypeRelative);
        CHECK(S(url.GetScheme()) == "http");
        CHECK(S(url.GetServer()) == "host");
        CHECK(S(url.GetAbsolutePath()) == "/a/sub/file.html");
    }
    {
        cRZURL base(cRZString("http://host/a/b/c.html"));
        cRZURL url;
        CHECK(url.ChangeURL(cRZString("../d.html"), &base));
        CHECK(S(url.GetFullURL()) == "http://host/a/d.html");
    }
    {
        // network-path reference keeps its own server but inherits the scheme
        cRZURL base(cRZString("http://host/a/b.html"));
        cRZURL url;
        CHECK(url.ChangeURL(cRZString("//other/x"), &base));
        CHECK(S(url.GetFullURL()) == "http://other/x");
    }
    {
        // fragment-only reference: path/query inherited, the new fragment wins
        cRZURL base(cRZString("http://host/a/b.html?q=1#f"));
        cRZURL url;
        CHECK(url.ChangeURL(cRZString("#new"), &base));
        CHECK(S(url.GetFullURL()) == "http://host/a/b.html?q=1#new");
    }

    // --- ChangeURL: absolute reference, empty reference and empty base ---------------------------
    {
        cRZURL base(cRZString("http://host/a/b.html"));
        cRZURL url;
        CHECK(url.ChangeURL(cRZString("ftp://other/x"), &base));
        CHECK(S(url.GetFullURL()) == "ftp://other/x");      // quirk: stored verbatim, not parsed
        CHECK(url.GetURLType() == cRZURL::kTypeFTP);        // parsed lazily on the first getter
        CHECK(S(url.GetServer()) == "other");

        cRZURL emptyRef;
        CHECK(emptyRef.ChangeURL(cRZString(""), &base));
        CHECK(S(emptyRef.GetFullURL()) == "http://host/a/b.html");
        CHECK(emptyRef.GetURLType() == cRZURL::kTypeHTTP);

        cRZURL emptyBase;
        cRZURL parsed;
        CHECK(parsed.ChangeURL(cRZString("http://h/x"), &emptyBase));
        CHECK(S(parsed.GetFullURL()) == "http://h/x");
        CHECK(S(parsed.GetServer()) == "h");

        // the cRZString overload of the base also works
        cRZString baseText("http://host/a/b.html");
        cRZURL byString;
        CHECK(byString.ChangeURL(cRZString("c.html"), &baseText));
        CHECK(S(byString.GetFullURL()) == "http://host/a/c.html");
    }

    // --- SetFullURL: invalidate=false keeps the stale cached components --------------------------
    {
        cRZURL url(cRZString("http://host/a"));
        CHECK(S(url.GetServer()) == "host");
        cRZString other("http://othersrv/b");
        url.SetFullURL(other, false);
        CHECK(S(url.GetFullURL()) == "http://othersrv/b");
        CHECK(S(url.GetServer()) == "host");         // stale, as in the original
        url.SetFullURL(other, true);
        CHECK(S(url.GetServer()) == "othersrv");
    }

    // --- operator= copies every member, including the cached parse ------------------------------
    {
        cRZURL source(cRZString("http://host:7/p?q#f"));
        CHECK(source.GetURLType() == cRZURL::kTypeHTTP);
        cRZURL copy;
        copy = source;
        CHECK(S(copy.GetFullURL()) == "http://host:7/p?q#f");
        CHECK(copy.GetPort() == 7);                  // copied cache, not a reparse
        CHECK(S(copy.GetServer()) == "host");
        cRZURL& self = copy;
        copy = self;
        CHECK(S(copy.GetFullURL()) == "http://host:7/p?q#f");
    }

    // --- ParseURL consumes the working string ---------------------------------------------------
    {
        cRZString text("ftp://h/pub/x;type=i");
        cRZURL url;
        CHECK(url.ParseURL(text));
        CHECK(text.str().empty());
        CHECK(url.GetURLType() == cRZURL::kTypeFTP);
        CHECK(S(url.GetServer()) == "h");
        CHECK(S(url.GetAbsolutePath()) == "/pub/x");
        CHECK(S(url.GetParameters()) == ";type=i");
    }

    if (failures == 0) std::printf("rz_url: all checks passed\n");
    return failures == 0 ? 0 : 1;
}

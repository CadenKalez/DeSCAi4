// cRZCookieManager test — the URL cookie store of the network stack (src/rz/CookieManager.h).
// Every assertion below pins a behaviour read from the binary; the quirky ones carry the address in a comment.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>

#include "rz/CookieManager.h"
#include "rz/URL.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

namespace {

std::string Slurp(const char* path) {
    std::string text;
    if (std::FILE* file = std::fopen(path, "rb")) {
        char buffer[4096];
        std::size_t count;
        while ((count = std::fread(buffer, 1, sizeof buffer, file)) != 0) text.append(buffer, count);
        std::fclose(file);
    }
    return text;
}

// The cRZHTTPClient::TimeStringToSeconds seam (0x100635cb6): one date in 2038, everything else unparseable.
class FakeHTTPHost : public ICookieHTTPHost {
public:
    int calls = 0;
    std::int32_t TimeStringToSeconds(const cRZString& text) override {
        ++calls;
        if (text.str() == "Sat, 01 Jan 2038 00:00:00 GMT") return 0x7fffffff;
        return 0;
    }
};

std::uint32_t Now32() { return static_cast<std::uint32_t>(std::time(nullptr)); }

}  // namespace

int main() {
    // --- construction defaults (+0x08/+0x0c/+0x14/+0x40) ------------------------------------------------
    {
        cRZCookieManager manager;
        cRZString path;
        CHECK(manager.GetCookieDataFilePath(path));
        CHECK(path.str() == "CookieData.ini");
        CHECK(manager.MaxCookies() == 0x4000);
        CHECK(manager.CookieCount() == 0);
        CHECK(!manager.IsInitialized());
        CHECK(manager.ReferenceCount() == 0);
        cRZString other("some/other.ini");
        CHECK(manager.SetCookieDataFilePath(other));
        CHECK(manager.CookieDataFilePath().str() == "some/other.ini");
        CHECK(manager.GetCookieDataFilePath(path) && path.str() == "some/other.ini");
    }

    // --- AddCookie(text, url) + GetCookieTextForURL from the URL -----------------------------------------
    const cRZString url("http://www.maxis.com/news/index.html");
    {
        cRZCookieManager manager;
        CHECK(manager.AddCookie("name=value; Domain=.maxis.com; Path=/news; Version=1", &url));
        CHECK(manager.CookieCount() == 1);
        CHECK(manager.CookieAt(0).field0 == "name=value; Domain=.maxis.com; Path=/news; Version=1");
        CHECK(manager.CookieAt(0).field60 == ".maxis.com");
        CHECK(manager.CookieAt(0).field78 == "/news");
        CHECK(manager.CookieAt(0).attributes.size() == 4);
        CHECK(FieldsOf(manager.CookieAt(0)).version == 1);
        CHECK(FieldsOf(manager.CookieAt(0)).expiry == 2);   // session cookie marker
        CHECK(!IsSecureCookie(manager.CookieAt(0)));

        // Cookie text: attribute pairs joined with "; ", special attributes get a '$', and a Version that is not
        // first is moved to the front: "$version=<v>; " is inserted at 0 and the separator that was appended for
        // this attribute is dropped again by the resize(size - 2) at 0x10062c3c9.
        std::string text;
        CHECK(manager.GetCookieTextForURL(url, text));
        CHECK(text == "$Version=1; name=value; $Domain=.maxis.com; $Path=/news");

        // The cRZString overload builds a cRZURL and forwards to the cRZURL one.
        std::string viaString;
        CHECK(manager.GetCookieTextForURL(cRZString("http://www.maxis.com/news/index.html"), viaString));
        CHECK(viaString == text);

        // --- GetCookieDataForURL (both overloads and the index) ---
        CookieData data;
        CHECK(manager.GetCookieDataForURL(url, data, 0));
        CHECK(data.field0 == "name=value; Domain=.maxis.com; Path=/news; Version=1");
        CHECK(FieldsOf(data).version == 1);
        CHECK(!manager.GetCookieDataForURL(url, data, 1));
        CHECK(manager.GetCookieDataForURL(cRZString("http://www.maxis.com/news/other.html"), data, 0));
        CHECK(!manager.GetCookieDataForURL(cRZString("http://www.maxis.com/sport/x"), data, 0));   // path prefix
        CHECK(!manager.GetCookieDataForURL(cRZString("http://other.com/news/x"), data, 0));        // domain suffix
        CookieData* raw = nullptr;
        cRZURL parseable(url);
        CHECK(manager.GetCookieDataForURL(parseable, &raw, 0));
        CHECK(raw == &manager.CookieAt(0));
        CHECK(!manager.GetCookieDataForURL(parseable, &raw, 1));

        // --- EnumCookies ---
        CookieData one[1];
        std::uint32_t count = 1;
        CHECK(manager.EnumCookies(one, count) && count == 1 && one[0].field0 == manager.CookieAt(0).field0);
        CookieData many[2];
        count = 0x100;   // clamped to the store size
        CHECK(manager.EnumCookies(many, count) && count == 1);
        CHECK(manager.RemoveAllCookies() && manager.CookieCount() == 0);
        count = 0;
        CHECK(manager.EnumCookies(many, count) && count == 0);
    }

    // --- Cookie text built with Version first (the clean RFC 2109 form) + skipped attributes ---------------
    {
        cRZCookieManager manager;
        CHECK(manager.AddCookie(
            "Version=1; a=1; Domain=.maxis.com; Path=/; Comment=hello; CommentURL=http://c/; "
            "Expires=Sat, 01 Jan 2038 00:00:00 GMT", nullptr));
        std::string text;
        CHECK(manager.GetCookieTextForURL(cRZString("http://www.maxis.com/x"), text));
        CHECK(text == "$Version=1; a=1; $Domain=.maxis.com; $Path=/");
        // Comment/CommentURL/Expires are skipped, the rest are case-insensitive matches on the stored spelling.
        CHECK(manager.CookieAt(0).field30 == "hello");
        CHECK(manager.CookieAt(0).field48 == "http://c/");
        CHECK(FieldsOf(manager.CookieAt(0)).expiry > Now32());   // "Expires" parsed through the fallback (no host)

        // A second cookie joins with "; " but its Version attribute is dropped: the version latch is per call.
        CHECK(manager.AddCookie("Version=1; b=2; Domain=.maxis.com; Path=/", nullptr));
        CHECK(manager.GetCookieTextForURL(cRZString("http://www.maxis.com/x"), text));
        CHECK(text == "$Version=1; a=1; $Domain=.maxis.com; $Path=/; b=2; $Domain=.maxis.com; $Path=/");
    }

    // --- domain rules of AddCookie ---------------------------------------------------------------
    {
        cRZCookieManager manager;
        CHECK(!manager.AddCookie("a=b; Domain=.com; Path=/", &url));            // no second '.' after the leading one
        CHECK(!manager.AddCookie("a=b; Domain=.maxis.com.; Path=/", &url));     // trailing '.'
        CHECK(!manager.AddCookie("a=b; Domain=.other.com; Path=/", &url));      // not a suffix of the server
        CHECK(!manager.AddCookie("a=b; Domain=.maxis.com; Path=/news/latest", &url));   // path is not a prefix
        CHECK(manager.AddCookie("a=b; Domain=maxis.com; Path=/", &url));        // ParseCookieText adds the '.'
        CHECK(manager.CookieAt(0).field60 == ".maxis.com");
        // ParseCookieText turns "Domain=maxis.com" into ".maxis.com", and the suffix search of AddCookie then
        // fails for the bare host, so such a cookie is never accepted for "maxis.com" itself (0x10062d099 adds
        // the dot, 0x10062b72c searches for the dotted form).
        const cRZString plainURL("http://maxis.com/x");
        CHECK(!manager.AddCookie("c=d; Domain=maxis.com; Path=/", &plainURL));
        // A server without a dot derives an empty domain, and an empty domain never matches (0x10062b628).
        const cRZString localURL("http://localhost/x");
        CHECK(!manager.AddCookie("e=f", &localURL));
        // A null URL skips the check entirely.
        CHECK(manager.AddCookie("g=h", nullptr));
        CHECK(manager.CookieCount() == 2);
    }

    // --- derived Domain/Path are appended to the cookie text (0x10062d328 / 0x10062d3df) -----------------
    {
        cRZCookieManager manager;
        CookieData cookie;
        cookie.field0 = "plain=1";
        CHECK(manager.ParseCookieText(cookie, &url));
        CHECK(cookie.field60 == ".maxis.com");
        CHECK(cookie.field78 == "/news/index.html");
        CHECK(cookie.field0 == "plain=1;  domain=.maxis.com; path=/news/index.html");

        CookieData noURL;
        noURL.field0 = "plain=1";
        CHECK(manager.ParseCookieText(noURL, nullptr));
        CHECK(noURL.field60.empty());
        CHECK(noURL.field78 == "/");
        CHECK(noURL.field0 == "plain=1; path=/");
    }

    // --- equivalent cookies are replaced, expired ones are dropped after the erase -------------------------
    {
        cRZCookieManager manager;
        CHECK(manager.AddCookie("id=1", nullptr));
        CHECK(manager.CookieCount() == 1);
        CHECK(manager.CookieAt(0).field0 == "id=1; path=/");
        CHECK(manager.AddCookie("id=1", nullptr));
        CHECK(manager.CookieCount() == 1);   // same text/domain/path -> erased and re-appended
        CHECK(manager.GetEquivalentCookieDataIndex(manager.CookieAt(0)) == 0);

        CookieData first;
        first.field0 = "id=2";
        first.attributes.push_back({"id", "2"});
        first.field60 = ".maxis.com";
        first.field78 = "/";
        FieldsOf(first).expiry = Now32() + 1000;
        CookieData second = first;
        second.attributes.push_back({"extra", "yes"});
        cRZCookieManager fresh;
        CHECK(fresh.AddCookie(first, nullptr));
        CHECK(fresh.CookieCount() == 1);
        CHECK(fresh.AddCookie(second, nullptr));       // attributes are not part of the identity
        CHECK(fresh.CookieCount() == 1);
        CHECK(fresh.CookieAt(0).attributes.size() == 2);
        CookieData third = second;
        third.attributes.push_back({"more", "1"});
        CHECK(fresh.GetEquivalentCookieDataIndex(third) == 0);

        // Quirk (0x10062b8xx then 0x10062b93e): the equivalent record is erased *before* the expiry test, so an
        // expired duplicate removes the stored cookie and is not added itself.
        CookieData expired = first;
        FieldsOf(expired).expiry = 1;
        CHECK(!fresh.AddCookie(expired, nullptr));
        CHECK(fresh.CookieCount() == 0);
    }

    // --- Discard means expiry 1, which AddCookie refuses (0x10062d0d1 + 0x10062b93e) ---------------------
    {
        cRZCookieManager manager;
        CHECK(!manager.AddCookie("d=1; Discard; Domain=.maxis.com; Path=/", nullptr));
        CHECK(manager.CookieCount() == 0);
    }

    // --- Expires through the cRZHTTPClient seam, Max-Age, ports ------------------------------------------
    {
        FakeHTTPHost host;
        SetCookieHTTPHost(&host);
        cRZCookieManager manager;
        CookieData dated;
        dated.field0 = "e=1; Expires=Sat, 01 Jan 2038 00:00:00 GMT";
        CHECK(manager.ParseCookieText(dated, nullptr));
        CHECK(FieldsOf(dated).expiry == 0x7fffffffu);
        CHECK(host.calls == 1);
        CookieData unparsed;
        unparsed.field0 = "e=1; Expires=garbage";
        CHECK(manager.ParseCookieText(unparsed, nullptr));
        CHECK(FieldsOf(unparsed).expiry > Now32());   // now + one day (0x10062d165)
        CHECK(host.calls == 2);

        const std::uint32_t before = Now32();
        CookieData aged;
        aged.field0 = "m=1; Max-Age=3600";
        CHECK(manager.ParseCookieText(aged, nullptr));
        CHECK(FieldsOf(aged).expiry >= before + 3600 && FieldsOf(aged).expiry <= before + 3660);

        // Port list quirk (0x10062cfc7..0x10062d21d): an n-value list keeps the first n-1 values.
        const cRZString portURL("http://www.maxis.com:8080/x");
        CookieData twoPorts;
        twoPorts.field0 = "p=1; Domain=.maxis.com; Path=/; Port=8080,9090";
        CHECK(manager.ParseCookieText(twoPorts, &portURL));
        CHECK(FieldsOf(twoPorts).ports[0] == 8080 && FieldsOf(twoPorts).ports[1] == 0);
        CookieData onePort;
        onePort.field0 = "p=1; Domain=.maxis.com; Path=/; Port=9090";
        CHECK(manager.ParseCookieText(onePort, &portURL));
        CHECK(FieldsOf(onePort).ports[0] == 0);   // a single port becomes the empty list
        CookieData emptyPort;
        emptyPort.field0 = "p=1; Domain=.maxis.com; Path=/; Port=";
        CHECK(manager.ParseCookieText(emptyPort, &portURL));
        // 0x10062d1c7: an empty value stores the default 80; cRZURL::GetPort does not trigger the lazy parse
        // (URL.h), so the URL's own port is still unread here and the fallback always wins.
        CHECK(FieldsOf(emptyPort).ports[0] == 80 && FieldsOf(emptyPort).ports[1] == 0);
        SetCookieHTTPHost(nullptr);
        CookieData noHost;
        noHost.field0 = "e=1; Expires=garbage";
        CHECK(manager.ParseCookieText(noHost, nullptr));
        CHECK(FieldsOf(noHost).expiry > Now32());   // same fallback without a host
    }

    // --- the port list decides AddCookie (0x10062ba29..0x10062ba56) --------------------------------------
    {
        cRZCookieManager manager;
        const cRZString onPort("http://www.maxis.com:8080/x");
        const cRZString otherPort("http://www.maxis.com:9090/x");
        const cRZString defaultPort("http://www.maxis.com/x");
        CHECK(manager.AddCookie("q=1; Domain=.maxis.com; Path=/; Port=8080,9090", &onPort));
        CHECK(!manager.AddCookie("r=2; Domain=.maxis.com; Path=/; Port=8080,9090", &otherPort));
        CHECK(!manager.AddCookie("s=3; Domain=.maxis.com; Path=/; Port=8080,9090", &defaultPort));
        CHECK(manager.CookieCount() == 1);
    }

    // --- Secure cookies are never handed out: cRZURL never reports type 3 (0x10062c4a4 + URL.h) -----------
    {
        cRZCookieManager manager;
        CHECK(manager.AddCookie("a=b; Domain=.maxis.com; Path=/; Secure", nullptr));
        CHECK(manager.CookieCount() == 1);
        CHECK(IsSecureCookie(manager.CookieAt(0)));
        CookieData data;
        CHECK(!manager.GetCookieDataForURL(cRZString("http://www.maxis.com/x"), data, 0));
        CHECK(!manager.GetCookieDataForURL(cRZString("https://www.maxis.com/x"), data, 0));   // type 2, not 3
        manager.CookieAt(0).flags = 0;
        CHECK(manager.GetCookieDataForURL(cRZString("http://www.maxis.com/x"), data, 0));
    }

    // --- RemoveExpiredCookies / RemoveOldestCookies ------------------------------------------------------
    {
        cRZCookieManager manager;
        CHECK(manager.AddCookie("live=1", nullptr));
        CHECK(manager.AddCookie("doomed=2", nullptr));
        CHECK(manager.CookieCount() == 2);
        CHECK(manager.RemoveExpiredCookies() == 0);   // both are session cookies (expiry 2)
        manager.CookieAt(1).flags = 0;
        FieldsOf(manager.CookieAt(1)).expiry = 1;     // in the past, and not the session marker
        CHECK(manager.RemoveExpiredCookies() == 1);
        CHECK(manager.CookieCount() == 1);
        CHECK(manager.CookieAt(0).field0 == "live=1; path=/");
        // PARTIAL: the removal loop is missing from the binary; the port must return without spinning.
        CHECK(manager.RemoveOldestCookies(2, nullptr) == 0);
        const std::string filter = ".maxis.com";
        CHECK(manager.RemoveOldestCookies(2, &filter) == 0);
    }

    // --- INI persistence: plain text, session cookies skipped, encrypted form -----------------------------
    char path[] = "/tmp/rz_cookiemanager_testXXXXXX";
    const int fd = mkstemp(path);
    CHECK(fd >= 0);
    if (fd >= 0) close(fd);
    {
        cRZCookieManager writer;
        CHECK(writer.SetCookieDataFilePath(cRZString(path)));
        CHECK(writer.CookieCount() == 0);
        CHECK(writer.AddCookie("persist=1; Domain=.maxis.com; Path=/; Max-Age=3600", nullptr));
        CHECK(writer.CookieCount() == 1);
        CHECK(writer.WriteCookieDataFile());
        const std::string plain = Slurp(path);
        CHECK(plain.find("[CookieData]") != std::string::npos);
        CHECK(plain.find("CookieData00000=persist=1; Domain=.maxis.com; Path=/; Max-Age=3600") != std::string::npos);
        CHECK(plain.find("[Admin]") == std::string::npos);

        CHECK(writer.RemoveAllCookies() && writer.CookieCount() == 0);
        CHECK(writer.ReadCookieDataFile());
        CHECK(writer.CookieCount() == 1);
        CHECK(writer.CookieAt(0).field0 == "persist=1; Domain=.maxis.com; Path=/; Max-Age=3600");

        // Init reads the file only while the store is empty; Shutdown writes and clears (0x10062b07c/0x10062b0ae).
        cRZCookieManager reader;
        CHECK(reader.SetCookieDataFilePath(cRZString(path)));
        CHECK(reader.CookieCount() == 1);
        CHECK(reader.Init());
        CHECK(reader.IsInitialized());
        CHECK(reader.CookieCount() == 1);
        CHECK(reader.Shutdown());
        CHECK(!reader.IsInitialized());
        CHECK(reader.CookieCount() == 0);
        CHECK(Slurp(path).find("CookieData00000=") != std::string::npos);   // Shutdown persisted it again
    }
    {
        // A session cookie (expiry 2) is never written (0x10062e00c).
        cRZCookieManager writer;
        CHECK(writer.SetCookieDataFilePath(cRZString(path)));
        CHECK(writer.RemoveAllCookies());
        CHECK(writer.AddCookie("session=1; Domain=.maxis.com; Path=/", nullptr));
        CHECK(writer.CookieCount() == 1);
        CHECK(writer.WriteCookieDataFile());
        CHECK(Slurp(path).find("CookieData00000") == std::string::npos);
        cRZCookieManager reader;
        CHECK(reader.SetCookieDataFilePath(cRZString(path)));
        CHECK(reader.CookieCount() == 0);
    }
    {
        // Encrypted form: an [Admin] Binary=1 marker plus hex-encoded values (EncryptString is the identity).
        cRZCookieManager writer;
        CHECK(writer.SetCookieDataFilePath(cRZString(path)));
        CHECK(writer.RemoveAllCookies());
        writer.SetShouldEncryptCookieDataFile(true);
        CHECK(writer.AddCookie("secret=1; Domain=.maxis.com; Path=/; Max-Age=3600", nullptr));
        CHECK(writer.WriteCookieDataFile());
        const std::string encrypted = Slurp(path);
        CHECK(encrypted.find("[Admin]") != std::string::npos);
        CHECK(encrypted.find("Binary=1") != std::string::npos);
        CHECK(encrypted.find("secret=1") == std::string::npos);   // stored as hex
        cRZCookieManager reader;
        CHECK(reader.SetCookieDataFilePath(cRZString(path)));
        CHECK(reader.CookieCount() == 1);
        CHECK(reader.CookieAt(0).field0 == "secret=1; Domain=.maxis.com; Path=/; Max-Age=3600");
        // ClearCookieDataFile removes the file and reports success when there is none.
        CHECK(reader.ClearCookieDataFile());
        CHECK(reader.ClearCookieDataFile());
    }
    unlink(path);

    // --- AddRef / Release (heap: Release deletes the object when the count drops below two) ---------------
    {
        cRZCookieManager* manager = new cRZCookieManager();
        CHECK(manager->AddRef() == 1);
        CHECK(manager->ReferenceCount() == 1);
        CHECK(manager->AddRef() == 2);
        CHECK(manager->Release() == 1);
        CHECK(manager->Release() == 0);   // deletes itself
    }

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("rz_cookiemanager: all checks passed\n");
    return 0;
}

// cRZCookieManager — the URL cookie store of the network stack (23 member functions + ctor/3 dtors/AddRef/Release,
// 0x10062ae90..0x10062e13b, vtable 0x100ad98d0, RTTI "cRZCookieManager"). A cookie is a sc4::CookieData record
// (parsed from a "Set-Cookie" text); the store is a flat vector, persisted in an INI file.
//
// Original layout (0x68 bytes, no base class: slot 0 is the destructor, so this interface has no QueryInterface):
//   +0x00 vptr
//   +0x08 bool                 mInitialized        Init/Shutdown latch
//   +0x0c uint32               mRefCount
//   +0x10 bool                 mShouldEncrypt      SetShouldEncryptCookieDataFile; makes the INI values binary
//   +0x14 uint32               mMaxCookies         0x4000 (AddCookie trims through RemoveOldestCookies above it)
//   +0x18 cIGZCriticalSection  mLock               ConstructRecursiveMutexWrapper @0x10003b2f8, 0x10 bytes embedded
//   +0x28 std::vector<CookieData> mCookies         begin/end/capacity, 0xe0-byte records
//   +0x40 cRZString            mCookieDataFilePath "CookieData.ini" (kDefaultCookieDataIniFileName @0x100b12de0)
//
// Vtable order (first slot 0x100ad98e0): [0] dtor, [1] deleting dtor, [2] Init, [3] Shutdown,
// [4] AddRef, [5] Release, [6] SetShouldEncryptCookieDataFile, [7] +0x38 GetCookieDataFilePath,
// [8] 0x40 SetCookieDataFilePath, [9] 0x48 AddCookie(std::string, url), [10] 0x50 AddCookie(CookieData, url),
// [11] 0x58 RemoveAllCookies, [12] 0x60 GetCookieTextForURL(cRZString, std::string&),
// [13] 0x68 GetCookieTextForURL(cRZURL&, std::string&), [14] 0x70 EnumCookies, [15] 0x78 ParseCookieText,
// [16] 0x80 GetCookieDataForURL(cRZString, CookieData&, index), [17] 0x88 GetCookieDataForURL(cRZURL&, CookieData&, index),
// [18] 0x90 ClearCookieDataFile, [19] 0x98 ReadCookieDataFile, [20] 0xa0 WriteCookieDataFile.
// Non-virtual internals: GetEquivalentCookieDataIndex 0xbb00, RemoveOldestCookies 0xbd46,
// GetCookieDataForURL(cRZURL&, CookieData**, index) 0xc476, RemoveExpiredCookies 0xd70a, EncryptString 0xd896,
// DecryptString 0xd8f0.
//
// The INI file has two shapes, both under the section "CookieData" (kDefaultCookieDataIniFileSection @0x100b12de8):
//  * plain: one "CookieData%05u=<raw cookie text>" string entry per cookie;
//  * encrypted (mShouldEncrypt): an extra section [Admin] with Binary=1, and every cookie value is written with
//    WriteBinaryEntry after EncryptString. ReadCookieDataFile picks the binary form when [Admin] Binary exists.
//
// Quirks kept on purpose (see the addresses; the test asserts each one it can reach):
//  * EncryptString/DecryptString are the identity (0xd896/0xd8f0 copy the string); the INI binary form is only a
//    hex encoding of the raw text. mShouldEncrypt therefore decides the storage form, not secrecy.
//  * ParseCookieText rewrites the raw text (CookieData::field0) by appending ";  domain=<domain>" and
//    "; path=<path>" (note the two spaces, 0x1009f9cd2/0x1009f9cdd) whenever those attributes were derived from the
//    URL instead of being present. The stored text round-trips through the INI file because the extra fields then
//    parse as ordinary attributes.
//  * The derived domain is not the server: the leading labels are stripped up to the first '.' (so
//    "www.maxis.com" becomes ".maxis.com"); a server without a dot yields an empty domain and nothing is appended
//    (0x10062d2e6..0x10062d326).
//  * ParseCookieText resets the numeric block to version 0 and expiry 2 (0x10062cbbd). Expiry 2 is the "session
//    cookie" marker: AddCookie accepts it and RemoveExpiredCookies skips it, so it never expires — but
//    WriteCookieDataFile only persists cookies whose expiry is greater than the current time, so session cookies
//    are never written to the file (0x10062e00c).
//  * The Port attribute parser stores only the first n-1 values of an n-value list and then a 0 terminator, so a
//    one-port list becomes an empty list (0x10062cfc7..0x10062d21d). An empty or missing Port list means "any port".
//  * Attribute matching for Domain/Path/Port/Secure/Version/Discard/Expires/Max-Age is case-insensitive (stricmp);
//    Comment/CommentURL keep the value verbatim.
//  * GetCookieDataForURL (the CookieData** finder) requires a non-empty domain that is a suffix of the server, a
//    non-empty path that is a prefix of the URL path, and either an empty or a matching Port list. Unlike AddCookie it
//    does not require the domain to start before the server's first '.', does not reject a trailing '.', and has no
//    minimum length (0x10062c476; AddCookie's stricter rules are at 0x10062b621..0x10062ba6b).
//  * A "Secure" cookie is only returned when the URL type is 3, but cRZURL never reports type 3 ("https:" parses as
//    type 2, see URL.h), so secure cookies are never handed out (0x10062c4a4).
//  * AddCookie(CookieData) with a *non-null* URL accepts the cookie only when the URL matches (path prefix, domain
//    suffix + first-dot rule, port list); every mismatch returns false without adding. A null URL skips the checks.
//  * AddCookie(CookieData) with no attributes re-parses field0 through ParseCookieText and recurses (0x10062b55a..).
//    A text that yields no attribute at all (e.g. "\"\"") therefore recurses forever; not reached by the test.
//  * AddCookie replaces an equivalent cookie (same text + domain + path, see GetEquivalentCookieDataIndex) by erasing
//    it and then *appending* the new record, and trims through RemoveOldestCookies(4, nullptr) once the store holds
//    mMaxCookies records (erase 0x10062b811..0x10062b936, trim 0x10062b988..0x10062b9a0).
//  * WriteCookieDataFile deletes the file first (ClearCookieDataFile through slot 0x90) and only then writes, so a
//    failing write loses the previous contents (0x10062ddd8).
//  * GetCookieTextForURL builds the RFC 2109 Cookie header text: attribute pairs joined with "; ", the special
//    attributes prefixed with '$' ("$domain=", "$path=", "$port=", "$version="), Comment/CommentURL/Discard/Expires/
//    Max-Age/Secure skipped, and a Version attribute that is not the first one moved to the front by inserting
//    "$version=<v>; " at position 0 and then shrinking the result by two characters, which drops the "; " separator
//    that was appended for that attribute (0x10062c194..0x10062c3e7).
//  * ParseCookieText's "Port=" with an empty value stores the default port 80, never the URL's port: cRZURL::GetPort
//    is a plain getter and does not trigger the lazy parse (0x10062d1c7, see URL.h).
//
// Seams: everything else is a recovered neighbour — cRZString, cRZURL, cRZFile (FileExists/Remove), cRZIniFile
// (ReadEntry/WriteEntry/ReadBinaryEntry/WriteBinaryEntry), cRZCriticalSection, sc4::HTTPMessage
// (GetSeparatedStringList) and sc4::CookieData. Only the "Expires" date conversion of the original is unrecovered:
// cRZHTTPClient::TimeStringToSeconds (0x100635cb6) is reached through ICookieHTTPHost.
#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "rz/cRZCriticalSection.h"
#include "rz/cRZString.h"
#include "sc4/MiscTypes.h"

class cRZURL;

// The cookie record of the original network stack lives in sc4/MiscTypes.h (CookieData, 0xe0 bytes). Its number
// block was recovered unnamed; this alias and the view below name the fields the cookie manager uses.
using CookieData = sc4::CookieData;

// Named view of CookieData::numbers (+0x90..+0xd8): int32 version, uint32 expiry, then the 0-terminated port list.
// The Secure flag is CookieData::flags (+0xd8).
struct CookieFields {
    std::int32_t version;   // +0x90
    std::uint32_t expiry;   // +0x94 (unix seconds; 1 = Discard, 2 = session cookie)
    std::int32_t ports[16]; // +0x98
};
static_assert(sizeof(CookieFields) == sizeof(CookieData::numbers), "CookieFields overlays CookieData::numbers");
inline CookieFields& FieldsOf(CookieData& cookie) { return *reinterpret_cast<CookieFields*>(cookie.numbers); }
inline const CookieFields& FieldsOf(const CookieData& cookie) {
    return *reinterpret_cast<const CookieFields*>(cookie.numbers);
}
inline bool IsSecureCookie(const CookieData& cookie) { return cookie.flags != 0; }

// A neighbour the cookie store cannot do without yet: cRZHTTPClient (src has no HTTP client).
class ICookieHTTPHost {
public:
    virtual ~ICookieHTTPHost() = default;
    // cRZHTTPClient::TimeStringToSeconds(cRZString const&) @ 0x100635cb6: seconds since 1970 for an HTTP date, 0 when
    // the text is not a date (ParseCookieText then falls back to now + 1 day).
    virtual std::int32_t TimeStringToSeconds(const cRZString& text) = 0;
};
ICookieHTTPHost* CookieHTTPHost();
void SetCookieHTTPHost(ICookieHTTPHost* host);

class cRZCookieManager {
public:
    // +0x14: AddCookie trims through RemoveOldestCookies once the store holds this many records.
    static constexpr std::uint32_t kMaxCookieCount = 0x4000;

    // ctor 0x10062ae90 (complete-object ctor 0x10062ae90 forwards to it), dtors 0x10062afc6 / 0x10062b044 / 0x10062b04e.
    cRZCookieManager();
    virtual ~cRZCookieManager();

    // --- vtable, in slot order (vtable 0x100ad98d0, first slot 0x100ad98e0) ---
    virtual bool Init();                                                     // 0x10062b07c
    virtual bool Shutdown();                                                 // 0x10062b0ae
    virtual std::uint32_t AddRef();                                          // 0x10062b10e
    virtual std::uint32_t Release();                                         // 0x10062b14c
    virtual void SetShouldEncryptCookieDataFile(bool encrypt);               // 0x10062b19e
    virtual bool GetCookieDataFilePath(cRZString& path);                     // 0x10062b1a8
    virtual bool SetCookieDataFilePath(const cRZString& path);               // 0x10062b206
    virtual bool AddCookie(const std::string& text, const cRZString* url);   // 0x10062b300
    virtual bool AddCookie(const CookieData& cookie, const cRZString* url);  // 0x10062b3d6
    virtual bool RemoveAllCookies();                                         // 0x10062bf5a
    virtual bool GetCookieTextForURL(const cRZString& url, std::string& text);   // 0x10062bfb8
    virtual bool GetCookieTextForURL(cRZURL& url, std::string& text);            // 0x10062c02a
    virtual bool EnumCookies(CookieData* out, std::uint32_t& count);         // 0x10062c93a
    virtual bool ParseCookieText(CookieData& cookie, const cRZString* url);  // 0x10062ca7c
    virtual bool GetCookieDataForURL(const cRZString& url, CookieData& out, std::uint32_t index);   // 0x10062d592
    virtual bool GetCookieDataForURL(cRZURL& url, CookieData& out, std::uint32_t index);            // 0x10062d610
    virtual bool ClearCookieDataFile();                                      // 0x10062d94a
    virtual bool ReadCookieDataFile();                                       // 0x10062d9b6
    virtual bool WriteCookieDataFile();                                      // 0x10062ddac

    // --- non-virtual internals ---
    // Index of the cookie with the same text, domain and path (0xffffffff when none); both records must have attributes.
    std::uint32_t GetEquivalentCookieDataIndex(const CookieData& cookie);    // 0x10062bb00
    // Removes at least `count` cookies (see the PARTIAL note in CookieManager.cpp) and returns how many expired ones
    // the sweep took. `domainFilter` keeps only cookies whose domain contains the filter.
    std::uint32_t RemoveOldestCookies(std::uint32_t count, const std::string* domainFilter);   // 0x10062bd46
    // The `index`-th cookie that matches `url`; the pointer is into the store, valid until the next change.
    bool GetCookieDataForURL(cRZURL& url, CookieData** out, std::uint32_t index);   // 0x10062c476
    // Drops every expired cookie and returns how many were removed. 0x10062d70a
    std::int32_t RemoveExpiredCookies();
    bool EncryptString(const std::string& text, std::string& encrypted);      // 0x10062d896 (identity)
    bool DecryptString(const std::string& encrypted, std::string& text);      // 0x10062d8f0 (identity)

    // --- test/diagnostic access to the original's fields ---
    bool IsInitialized() const { return mInitialized; }
    std::uint32_t ReferenceCount() const { return mRefCount; }
    std::uint32_t MaxCookies() const { return mMaxCookies; }
    std::size_t CookieCount() const { return mCookies.size(); }
    const CookieData& CookieAt(std::size_t index) const { return mCookies[index]; }
    CookieData& CookieAt(std::size_t index) { return mCookies[index]; }
    const cRZString& CookieDataFilePath() const { return mCookieDataFilePath; }

private:
    bool mInitialized = false;               // +0x08
    std::uint32_t mRefCount = 0;             // +0x0c
    bool mShouldEncrypt = false;             // +0x10
    std::uint32_t mMaxCookies = kMaxCookieCount;   // +0x14
    cRZCriticalSection mLock;                // +0x18 (an embedded cIGZCriticalSection in the original)
    std::vector<CookieData> mCookies;        // +0x28
    cRZString mCookieDataFilePath;           // +0x40
};

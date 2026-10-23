// cRZURL — URL parser / builder of the RZ layer, used by the network file cache and the HTML hooks.
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (33 symbols, 0x10023e7e0..0x100241890 plus the
// out-of-line destructor at 0x100569cbc).  Addresses of every member are noted below / in URL.cpp.
//
// Original layout (0x174 bytes of data, padded by the compiler to a multiple of 8):
//   +0x00 bool        m_explicitSchemeOnly   set only by cRZURL(cRZString const&); when false a
//                                            scheme-less URL is guessed from www./ftp./news./nntp./irc.
//   +0x01 bool        m_parsed               lazy-parse cache flag (Get* re-parse while false)
//   +0x04 int32       m_type                 kType* below
//   +0x08 cRZString   m_fullURL
//   +0x30 cRZString   m_scheme
//   +0x58 cRZString   m_username
//   +0x80 cRZString   m_password
//   +0xa8 cRZString   m_server
//   +0xd0 cRZString   m_absolutePath
//   +0xf8 cRZString   m_parameters          includes the leading ';' as parsed
//   +0x120 cRZString  m_query               includes the leading '?'
//   +0x148 cRZString  m_fragment            includes the leading '#'
//   +0x170 uint32     m_port
//
// The original is a plain value class with no vtable; cRZString (0x28 bytes: vptr + std::string +
// refcount) already exists in src/rz, so no host seam is needed.
//
// Quirks kept on purpose (see the test for each assertion):
//  * "https://..." is typed as http (kTypeHTTP): the scheme matcher tests the "http" prefix first and
//    returns, so the "https" branch is unreachable (0x100240305..0x100240312).
//  * A leading "mailto:" is taken before scheme matching: m_scheme becomes "mailto" and the prefix is
//    consumed, but m_type stays 0 and m_parsed stays false (0x1002400db..0x100240843).  ParseURLMailTo
//    (type 6) is therefore dead in this build.
//  * Userinfo ("//user:pass@host") keeps the "//" inside m_username, and consuming through '@' also eats
//    the "//", so ParseServerAndPort never sees it and m_server stays empty (0x100240b2b..0x100240b8d).
//  * MakeURLStringFromComponents writes ':' + *username* again instead of the password (0x100240df6..).
//  * GetPort does not trigger the lazy parse (it is a 3-instruction getter at 0x10023f082).
//  * ChangeURL with a reference that carries its own scheme only copies the string; nothing is parsed
//    and all components stay cleared (0x10023f32a..0x10023f33b).
//  * ChangeURL clears only fullURL/scheme/password/server/path before parsing; username, parameters,
//    query and fragment keep their previous values until ParseURL clears them (0x10023f0e5..0x10023f146).
//  * An empty FTP path assigns "/" to the working string and then clears it, so m_absolutePath stays
//    empty (0x1002415bc..0x1002415f4 + 0x10024165c).  ParseURLFILE's "localhost" check is dead because
//    ParseServerAndPort returns before setting m_server for type FILE.
//  * ConvertPathFromEndodedForm(typo is the original symbol) turns an invalid %XX into one space and
//    swallows three characters; a '%' with fewer than two bytes left is copied literally.
//  * CanonicalizePath removes "./" runs whose '.' is not itself preceded by '.', then one trailing '.',
//    then "/seg/../" runs, then one trailing "/.."; it never collapses "//".
#pragma once
#include <cstddef>
#include <cstdint>

#include "rz/cRZString.h"

class cRZURL {
public:
    // Values of m_type; 0 means "not parsed successfully" and keeps the lazy parse armed.
    enum Type : std::int32_t {
        kTypeNone = 0,           // ParseURL did not accept the text (relative under some callers)
        kTypeUnknownScheme = 1,  // "scheme:rest" without "//": the rest becomes m_absolutePath
        kTypeHTTP = 2,
        kTypeHTTPS = 3,          // never produced, see the header comment
        kTypeFILE = 4,
        kTypeFTP = 5,
        kTypeMailTo = 6,         // never produced, see the header comment
        kTypeNews = 7,           // news and nntp
        kTypeIRC = 8,
        kTypeRelative = 9,       // no scheme; parsed like an HTTP reference (server/path/query/...)
    };

    cRZURL();                                        // @ 0x10023e7e0 (+ complete 0x10023e90c)
    cRZURL(const cRZString& url);                    // @ 0x10023e916 (+ complete 0x10023ea66)
    cRZURL& operator=(const cRZURL& other);          // @ 0x10023ea7a
    ~cRZURL();                                       // @ 0x100569cbc

    // Getters.  All but GetFullURL/GetPort lazily parse a copy of m_fullURL into *this when m_parsed
    // is false (the original methods are const and still mutate the object).
    std::int32_t GetURLType() const;                 // @ 0x10023eb58
    const cRZString& GetFullURL() const;             // @ 0x10023ebe4 (no lazy parse)
    const cRZString& GetScheme() const;              // @ 0x10023ebee
    const cRZString& GetUsername() const;            // @ 0x10023ec7e
    const cRZString& GetPassword() const;            // @ 0x10023ed0e
    const cRZString& GetServer() const;              // @ 0x10023ed9e
    const cRZString& GetAbsolutePath() const;        // @ 0x10023ee32
    const cRZString& GetParameters() const;          // @ 0x10023eec6
    const cRZString& GetQuery() const;               // @ 0x10023ef5a
    const cRZString& GetFragment() const;            // @ 0x10023efee
    std::uint32_t GetPort() const;                   // @ 0x10023f082 (does not parse)

    void SetFullURL(const cRZString& url, bool invalidateParse);   // @ 0x10023f098
    // Resolve `url` against `baseURL` (a bare URL string) and reparse; see URL.cpp for the merge rules.
    bool ChangeURL(const cRZString& url, const cRZString* baseURL);   // @ 0x10023f0b6
    bool ChangeURL(const cRZString& url, const cRZURL* baseURL);      // @ 0x10023ff88
    // Parse `url` into the components, consuming the scheme/host/path prefixes.  @ 0x10023f754
    bool ParseURL(cRZString& url);
    static bool CanonicalizePath(cRZString& path);                    // @ 0x10023f914
    bool MakeURLStringFromComponents();                               // @ 0x10023fcbe

    // Percent-decode `path`; the typo in the original symbol is kept.
    static bool ConvertPathFromURLToLocalForm(const cRZString& path, cRZString& out);   // @ 0x1002417fa
    static bool ConvertPathFromEndodedForm(const cRZString& encoded, cRZString& out);   // @ 0x100241890

private:
    void EnsureParsed() const;
    bool ParseScheme(cRZString& url);                     // @ 0x10023ffbc
    bool ParseUsernameAndPassword(cRZString& url);        // @ 0x100240a58
    bool ParseServerAndPort(cRZString& url);              // @ 0x100240c1a
    bool ParseURLHTTP(cRZString& url);                    // @ 0x100240ff6
    bool ParseURLFILE(cRZString& url);                    // @ 0x100241424
    bool ParseURLFTP(cRZString& url);                     // @ 0x100241508
    bool ParseURLMailTo(cRZString& url);                  // @ 0x100241698
    bool ParseURLNNTP(cRZString& url);                    // @ 0x100241748

    // Offsets of the original are preserved because cRZString is 0x28 bytes, like the libc++ one.
    bool m_explicitSchemeOnly = false;   // +0x00
    bool m_parsed = false;               // +0x01
    std::int32_t m_type = 0;             // +0x04
    cRZString m_fullURL;                 // +0x08
    cRZString m_scheme;                  // +0x30
    cRZString m_username;                // +0x58
    cRZString m_password;                // +0x80
    cRZString m_server;                  // +0xa8
    cRZString m_absolutePath;            // +0xd0
    cRZString m_parameters;              // +0xf8
    cRZString m_query;                   // +0x120
    cRZString m_fragment;                // +0x148
    std::uint32_t m_port = 0;            // +0x170
};

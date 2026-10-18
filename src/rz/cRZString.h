// cRZString — reference-counted string implementing cIGZString on top of std::string.
// Recovered from Sim City 4 Deluxe Editionsub 1.2.1 (vtable 0x100a67880).
// Original layout (0x28 bytes): +0x00 vptr, +0x08 libc++ std::string (24 B, SSO), +0x20 refcount.
//
// Known gaps (tracked in notes/ledger.csv):
//  * Case-insensitive operations fold ASCII only. The original falls back to a UTF-16
//    round-trip (ConvertStringEncoding + RZTolowerWImp/RZToupperWImp) at the first byte >= 0x7f.
//  * Sprintf uses plain vsnprintf; the original also measures %s widths in UTF-8 glyphs
//    (GetCharLength/GetGlyphCountFromString).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "gz/cIGZString.h"

class cRZString : public cIGZString {
public:
    cRZString() = default;
    cRZString(const char* s) : str_(s ? s : "") {}  // original dereferences null; we treat it as empty
    cRZString(const cIGZString& other) : str_(other.Data(), other.Strlen()) {}
    cRZString(const cRZString& other) : cIGZString(), str_(other.str_) {}  // refcount is not copied
    // Substring constructor: (source, pos, count); count may be npos.
    cRZString(const cRZString& src, std::size_t pos, std::size_t count) : str_(src.str_, pos, count) {}
    cRZString& operator=(const cRZString& other) { str_ = other.str_; return *this; }
    ~cRZString() override = default;

    // --- cIGZUnknown ---
    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return ++refCount_; }
    // Destroys the object when the count drops to 0 (or was never taken): `delete this`.
    std::uint32_t Release() override;

    // --- cIGZString ---
    void AssignFromCString(const char* s) override;
    void AssignFromChars(const char* s, std::uint32_t length) override;
    const char* ToChar() const override { return str_.c_str(); }
    const char* Data() const override { return str_.c_str(); }
    std::uint32_t Strlen() const override { return static_cast<std::uint32_t>(str_.size()); }
    bool IsEqualToStringPointer(const cIGZString* other, bool caseSensitive) const override { return IsEqualToStringRef(*other, caseSensitive); }
    bool IsEqualToStringRef(const cIGZString& other, bool caseSensitive) const override;
    bool IsEqualToChars(const char* s, std::uint32_t length, bool caseSensitive) const override;
    int CompareToString(const cIGZString& other, bool caseSensitive) const override;
    int CompareToChars(const char* s, std::uint32_t length, bool caseSensitive) const override;
    cIGZString& AssignFromIGZString(const cIGZString& other) override { Copy(other); return *this; }
    void Copy(const cIGZString& other) override { AssignFromChars(other.Data(), other.Strlen()); }
    void Resize(std::uint32_t newLength) override { str_.resize(newLength, '\0'); }
    cIGZString& AppendChars(const char* s, std::uint32_t length) override { str_.append(s, length); return *this; }
    cIGZString& AppendString(const cIGZString& other) override { str_.append(other.ToChar(), other.Strlen()); return *this; }
    cIGZString& InsertChars(std::uint32_t pos, const char* s, std::uint32_t length) override { str_.insert(pos, s, length); return *this; }
    cIGZString& InsertString(std::uint32_t pos, const cIGZString& other) override { str_.insert(pos, other.ToChar(), other.Strlen()); return *this; }
    cIGZString& ReplaceRangeWithChars(std::size_t pos, const char* s, std::size_t removeCount) override { str_.replace(pos, removeCount, s); return *this; }
    cIGZString& ReplaceRangeWithString(std::size_t pos, const cIGZString& other) override { str_.replace(pos, other.Strlen(), other.ToChar()); return *this; }
    cIGZString& Erase(std::uint32_t pos, std::uint32_t count) override { str_.erase(pos, count); return *this; }
    std::size_t FindChars(const char* s, std::size_t pos, bool caseSensitive) const override;
    std::size_t FindString(const cIGZString& s, std::size_t pos, bool caseSensitive) const override { return FindChars(s.ToChar(), pos, caseSensitive); }
    std::size_t ReverseFindChars(const char* s, std::size_t pos, bool caseSensitive) const override;
    std::size_t ReverseFindString(const cIGZString& s, std::size_t pos, bool caseSensitive) const override { return ReverseFindChars(s.ToChar(), pos, caseSensitive); }
    cIGZString* Sprintf(const char* format, ...) override;

    // --- non-virtual helpers ---
    // Removes through the first occurrence of `delimiters` (as a substring) and returns the
    // text before it; if absent returns an empty string and leaves *this alone. Null/empty
    // `delimiters` returns a copy of the whole string.
    cRZString SplitToken(const char* delimiters);
    // Discards through the first `delimiter`; false (and clears the string) if none.
    bool DiscardThroughDelimiter(char delimiter);
    // Moves the token before `delimiter` into `token` and removes it plus the delimiter. If
    // there is no delimiter the whole remainder goes to `token`, *this becomes empty, false.
    bool SplitTokenAtDelimiter(char delimiter, cRZString& token);
    // Like the above but skips runs of consecutive delimiters; false when nothing is left.
    bool SplitTokenSeparated(char delimiter, cRZString& token);

    cRZString& Strncpy(const char* s, std::size_t maxLength);
    cRZString& Strcat(const char* s) { str_.append(s); return *this; }
    cRZString Left(std::size_t count) const { return Mid(0, count); }
    cRZString Mid(std::size_t pos, std::size_t count) const;
    cRZString Right(std::size_t count) const;
    bool BeginsWith(const char* s, std::size_t length) const;
    bool EndsWith(const char* s, std::size_t length) const;
    void MakeLower();
    void MakeUpper();
    void LTrim();  // strips leading spaces and tabs only
    void RTrim();
    void Trim() { LTrim(); RTrim(); }

    const std::string& str() const noexcept { return str_; }
    std::string& str() noexcept { return str_; }

private:
    std::string str_;
    std::uint32_t refCount_ = 0;
};

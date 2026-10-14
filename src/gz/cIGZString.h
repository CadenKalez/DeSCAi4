// cIGZString — abstract string interface. Pure-virtual order is the vtable order recovered
// from cRZString (slots 5..29, byte offsets +0x28..+0xe8); do not reorder.
#pragma once
#include <cstddef>
#include <cstdint>

#include "gz/cIGZUnknown.h"

class cIGZString : public cIGZUnknown {
public:
    // [5] +0x28, [6] +0x30. A null `s` clears the string.
    virtual void FromChar(const char* s) = 0;
    virtual void FromChar(const char* s, std::uint32_t length) = 0;
    // [7] +0x38, [8] +0x40: both return the character data; [9] +0x48: length in bytes.
    virtual const char* ToChar() const = 0;
    virtual const char* Data() const = 0;
    virtual std::uint32_t Strlen() const = 0;
    // [10..12] equality; `length == 0xFFFFFFFF` means "strlen(s)".
    virtual bool IsEqual(const cIGZString* other, bool caseSensitive) const = 0;
    virtual bool IsEqual(const cIGZString& other, bool caseSensitive) const = 0;
    virtual bool IsEqual(const char* s, std::uint32_t length, bool caseSensitive) const = 0;
    // [13..14] <0, 0, >0. Compares the common prefix first, then lengths.
    virtual int CompareTo(const cIGZString& other, bool caseSensitive) const = 0;
    virtual int CompareTo(const char* s, std::uint32_t length, bool caseSensitive) const = 0;
    // [15] operator= and [16] Copy both replace the content.
    virtual cIGZString& operator=(const cIGZString& other) = 0;
    virtual void Copy(const cIGZString& other) = 0;
    // [17] pads with '\0' when growing.
    virtual void Resize(std::uint32_t newLength) = 0;
    // [18..21]
    virtual cIGZString& Append(const char* s, std::uint32_t length) = 0;
    virtual cIGZString& Append(const cIGZString& other) = 0;
    virtual cIGZString& Insert(std::uint32_t pos, const char* s, std::uint32_t length) = 0;
    virtual cIGZString& Insert(std::uint32_t pos, const cIGZString& other) = 0;
    // [22..23] Replace(pos, n, s): replaces n characters at pos with the NUL-terminated `s`
    // (the third parameter of the original is the number of characters REMOVED, not inserted).
    virtual cIGZString& Replace(std::size_t pos, const char* s, std::size_t removeCount) = 0;
    virtual cIGZString& Replace(std::size_t pos, const cIGZString& other) = 0;
    // [24]
    virtual cIGZString& Erase(std::uint32_t pos, std::uint32_t count) = 0;
    // [25..28] return npos (size_t(-1)) when not found.
    virtual std::size_t Find(const char* s, std::size_t pos, bool caseSensitive) const = 0;
    virtual std::size_t Find(const cIGZString& s, std::size_t pos, bool caseSensitive) const = 0;
    virtual std::size_t RFind(const char* s, std::size_t pos, bool caseSensitive) const = 0;
    virtual std::size_t RFind(const cIGZString& s, std::size_t pos, bool caseSensitive) const = 0;
    // [29] +0xe8
    virtual cIGZString* Sprintf(const char* format, ...) = 0;
};

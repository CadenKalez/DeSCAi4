#include "rz/StringConversion.h"

#include <cstring>
#include <vector>

#include "rz/TextEncoding.h"

// The original converts short strings through a 0x200-byte stack buffer and longer ones through a heap buffer of
// six bytes per source byte; a vector of that size plays both roles.

unsigned ConvertStringEncoding(const void* source, unsigned size, int sourcePage, cIGZString& out) {   // 0x10001aa53
    if (sourcePage != -2) {
        if (sourcePage == -1) sourcePage = GetCurrentSystemCodePage();
        if (sourcePage != 8) {
            unsigned capacity = size * 6;
            std::vector<char> buffer(capacity < 0x200 ? 0x200 : capacity);
            buffer[0] = 0;
            const unsigned consumed = ConvertStringEncoding(source, size, sourcePage, buffer.data(), capacity, 8);
            out.AssignFromChars(buffer.data(), capacity);
            return consumed;
        }
    }
    out.AssignFromChars(static_cast<const char*>(source), size);
    return size;
}

unsigned ConvertStringEncoding(const cIGZString& text, void* out, unsigned& capacity, int destinationPage) {   // 0x10001b41d
    if (destinationPage != -2) {
        if (destinationPage == -1) destinationPage = GetCurrentSystemCodePage();
        if (destinationPage != 8) return ConvertStringEncoding(text.Data(), text.Strlen(), 8, out, capacity, destinationPage);
    }
    if (text.Strlen() < capacity) capacity = text.Strlen();
    std::memcpy(out, text.Data(), capacity);
    return capacity;
}

bool ConvertStringEncoding(const cIGZString& text, int destinationPage, cIGZString& out) {   // 0x10001b4d2
    if (destinationPage != -2) {
        if (destinationPage == -1) destinationPage = GetCurrentSystemCodePage();
        if (destinationPage == 0x10) return false;
        if (destinationPage != 8) {
            const unsigned size = text.Strlen();
            unsigned capacity = size * 6;
            std::vector<char> buffer(capacity < 0x200 ? 0x200 : capacity);
            buffer[0] = 0;
            const unsigned consumed = ConvertStringEncoding(text.Data(), size, 8, buffer.data(), capacity, destinationPage);
            out.AssignFromChars(buffer.data(), capacity);
            return consumed == size;
        }
    }
    if (&text != &out) out.AssignFromChars(text.ToChar(), text.Strlen());
    return true;
}

bool ConvertStringEncoding(const cIGZString& text, cIGZString& out, int sourcePage) {   // 0x10001b619
    if (sourcePage != -2) {
        if (sourcePage == -1) sourcePage = GetCurrentSystemCodePage();
        if (sourcePage != 8) {
            const unsigned size = text.Strlen();
            unsigned capacity = size * 6;
            std::vector<char> buffer(capacity < 0x200 ? 0x200 : capacity);
            buffer[0] = 0;
            const unsigned consumed = ConvertStringEncoding(text.Data(), size, sourcePage, buffer.data(), capacity, 8);
            out.AssignFromChars(buffer.data(), capacity);
            return consumed == size;
        }
    }
    if (&text != &out) out.AssignFromChars(text.ToChar(), text.Strlen());
    return true;
}

void ConvertUTF8StringToDestinationFormat(cIGZString& text) {   // 0x100067354
    const int page = GetCurrentFrameworkCodePage();
    if (page == 0x10 || page == 8) return;
    char buffer[0x100];
    unsigned capacity = sizeof(buffer);
    ConvertStringEncoding(text.Data(), text.Strlen(), 8, buffer, capacity, page == 0 ? 0 : page);
    text.AssignFromChars(buffer, capacity);
}

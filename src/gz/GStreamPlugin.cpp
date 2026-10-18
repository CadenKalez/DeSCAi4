#include "gz/GStreamPlugin.h"

#include <cstring>
#include <new>

void* _galloc(int size) {
    std::size_t bytes = static_cast<std::size_t>(-1);
    if (size > -2) bytes = static_cast<std::size_t>(static_cast<long>(size));
    return ::operator new[](bytes);
}

bool _gfree(void* block) {
    if (block) ::operator delete[](block);
    return true;
}

int _gread(cIGZFile* stream, void* buffer, int count) {
    if (count < 0) return 0;
    std::uint32_t n = static_cast<std::uint32_t>(count);
    stream->ReadWithCount(buffer, n);
    return static_cast<int>(n);
}

int _gwrite(cIGZFile* stream, const void* buffer, int count) {
    if (count < 0) return 0;
    std::uint32_t n = static_cast<std::uint32_t>(count);
    stream->WriteWithCount(buffer, n);
    return static_cast<int>(n);
}

bool _gseek(cIGZFile* stream, int position) { return stream->Seek(position, 0) == static_cast<std::uint32_t>(position); }

std::uint32_t _glen(cIGZFile* stream) { return stream->Length(); }

std::uint32_t _gtell(cIGZFile* stream) { return stream->Seek(0, 1); }

std::uint8_t* NULL_about() {
    auto* d = static_cast<std::uint8_t*>(::operator new[](0x100));
    std::memset(d + 0x10, 0, 0xf0);
    auto put64 = [&](std::size_t at, std::uint64_t v) { std::memcpy(d + at, &v, 8); };
    auto put32 = [&](std::size_t at, std::uint32_t v) { std::memcpy(d + at, &v, 4); };
    auto put16 = [&](std::size_t at, std::uint16_t v) { std::memcpy(d + at, &v, 2); };
    put64(0x00, 0x1004e554c4cull);
    put64(0x08, 0x100000000159ull);
    put64(0x14, 0x6400000000ull);
    put32(0x1c, 0);
    put64(0x82, 0x73776572646e41ull);   // "Andrews"
    put64(0x7c, 0x6e41206e696c6f43ull);  // "Colin An"
    put16(0x9c, 0x30);                   // "0"
    d[0xa8] = 0;
    put32(0xa4, 0x6c6c754e);             // "Null"
    put32(0xac, 0x746f4e);               // "Not"
    put64(0xbc, 0x656d694720746f4eull);  // "Not Gime"
    put16(0xc4, 0x78);                   // "x"
    return d;
}

int NULL_is(GSTREAM*) { return 0; }
int NULL_open(GINSTANCE**, GSTREAM*, const char*, bool) { return 0; }
int NULL_info(GINSTANCE*, int) { return 0; }
int NULL_read(GINSTANCE*, GINFO*, char*, int) { return 0; }
int NULL_close(GINSTANCE*) { return 0; }
int NULL_wopen(GINSTANCE**, GSTREAM*, const char*, int) { return 0; }
int NULL_write(GINSTANCE*, const GINFO*, char*, int) { return 0; }
int NULL_wclose(GINSTANCE*) { return 0; }

// RefPack (QFS) helpers of the persistence layer (0x1003da6f0, 0x1003da73f, 0x1003da9e8): a 5-byte header (0x10 0xfb and
// the 24-bit big-endian uncompressed size), then LZ77 commands; the stream ends with a literal-only command (0xfc..0xff).
//   0x00..0x7f  2-byte copy  : up to 3 literals, copy 3..10 from 1..1024 back
//   0x80..0xbf  3-byte copy  : up to 3 literals, copy 4..67 from 1..16384 back
//   0xc0..0xdf  4-byte copy  : up to 3 literals, copy 5..1028 from 1..131072 back
//   0xe0..0xfb  literal run of 4..112 bytes;  0xfc..0xff  end, with 0..3 trailing literals
#pragma once
#include <cstddef>
#include <cstdint>

// The uncompressed size recorded in the header of `packed`. Headers with magic 0x10fb put it at byte 2, the others (flag
// byte 1, an extra 24-bit compressed size) at byte 5. @ 0x1003da6f0
std::uint32_t sizeref(const std::uint8_t* packed);

// Decompresses into `out` (which must hold sizeref() bytes). Returns the uncompressed size; `consumed` (when given)
// receives how many input bytes were read. A null `packed` returns 0 and consumes nothing. @ 0x1003da73f
std::uint32_t decoderef(const std::uint8_t* packed, std::ptrdiff_t* consumed, std::uint8_t* out);

// Compresses `size` bytes into `out` (room for size * 1.5 + 1024 bytes is enough) and returns the packed length including
// the header, or 5 when the work tables cannot be allocated. `quick` != 0 (pointer, may be null) inserts only the first
// position of each match into the hash chains, which is faster and compresses a little worse. @ 0x1003da9e8
std::size_t encoderef(const std::uint8_t* in, long size, std::uint8_t* out, const int* quick);

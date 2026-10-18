#include <cstdio>

#include "gz/cGZDBRecordBitmapRes.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    cGZDBRecordBitmapRes record;
    CHECK(!record.Init(nullptr, 5));
    std::uint32_t size = 99;
    CHECK(record.GetSize() == 0 && record.GetSize(size) && size == 0 && record.GetPosition() == 0);
    std::uint8_t out[4];
    CHECK(!record.GetFieldVoid(out, 1) && !record.SkipBytes(2) && record.GetPosition() == 0);

    auto data = std::make_unique<std::uint8_t[]>(6);
    for (int i = 0; i < 6; ++i) data[i] = std::uint8_t(10 + i);
    record.SetData(std::move(data), 6);
    CHECK(record.GetSize() == 6);
    CHECK(record.GetFieldVoid(out, 3) && out[0] == 10 && out[2] == 12 && record.GetPosition() == 3);
    CHECK(!record.GetFieldVoid(out, 4) && record.GetPosition() == 3);       // not enough left
    CHECK(record.GetFieldVoid(out, 3) && out[0] == 13 && record.GetPosition() == 6);
    CHECK(!record.GetFieldVoid(out, 1));
    CHECK(record.SkipBytes(100) && record.GetPosition() == 106);            // no bound check
    CHECK(!record.SetFieldVoid(out, 1) && !record.SetSize(10) && !record.SeekAbsolute(0) && !record.SeekRelative(1));
    std::uint32_t value = 0;
    record.SetData(std::make_unique<std::uint8_t[]>(4), 4);
    CHECK(record.GetFieldUint32(value) && value == 0);
    std::printf(failures ? "FAILED\n" : "gz_dbrecordbitmapres ok\n");
    return failures != 0;
}

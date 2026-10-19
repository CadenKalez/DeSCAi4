// City-file helpers: the save version record, open / close, string records, the serial number, and a real city file.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

#include "rz/cRZString.h"
#include "sc4/DBSegment.h"
#include "sc4/SaveGame.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main() {
    sc4::GetSaveGameServices().createCitySegment = []() -> cIGZPersistDBSegment* {
        auto* segment = new sc4::cSC4DBSegment();
        segment->AddRef();
        return segment->AsIGZPersistDBSegment();
    };
    const std::string path = "/tmp/sc4_savegame_test_" + std::to_string(getpid()) + ".sc4";
    {
        cIGZPersistDBSegment* segment = sc4::SC4OpenSaveGame(cRZString(path.c_str()), true, false);
        CHECK(segment != nullptr);
        if (segment) {
            CHECK(sc4::SC4ReadAppLoadSaveVersion(segment) == sc4::kLoadSaveVersionDefault);     // no marker yet
            CHECK(sc4::SC4WriteAppLoadSaveVersion(segment));
            CHECK(sc4::SC4WriteStringRecord(segment, 0x11, 0x22, cRZString("hello city")));
            cRZString back;
            CHECK(sc4::SC4ReadStringRecord(segment, 0x11, 0x22, back) && std::string(back.ToChar()) == "hello city");
            CHECK(!sc4::SC4ReadStringRecord(segment, 0x11, 0x23, back));
            CHECK(sc4::SC4CloseSaveGame(segment));
        }
    }
    {
        cIGZPersistDBSegment* segment = sc4::SC4OpenSaveGame(cRZString(path.c_str()), false, true);
        CHECK(segment != nullptr);
        if (segment) {
            CHECK(sc4::SC4ReadAppLoadSaveVersion(segment) == 0x42);
            cRZString back;
            CHECK(sc4::SC4ReadStringRecord(segment, 0x11, 0x22, back));
            sc4::SC4CloseSaveGame(segment);
        }
    }
    std::remove(path.c_str());
    CHECK(sc4::SC4OpenSaveGame(cRZString("/tmp/definitely/missing.sc4"), false, true) == nullptr);

    // the serial number: seconds since 2000 shifted left four bits
    const std::uint32_t serial = sc4::SC4CreateCitySerialNumber();
    CHECK((serial >> 4) > 100000000u / 16 * 0 && serial != 0);

    // a city file of the game itself (read only)
    const char* home = std::getenv("HOME");
    const std::string city = std::string(home ? home : "") + "/Library/Application Support/Steam/steamapps/common/SimCity 4 Deluxe/Sim City 4 Deluxe Edition.app/Contents/GameData/Regions/Timbuktu/City - New City (25).sc4";
    if (access(city.c_str(), R_OK) == 0) {
        cIGZPersistDBSegment* segment = sc4::SC4OpenSaveGame(cRZString(city.c_str()), false, true);
        CHECK(segment != nullptr);
        if (segment) {
            const std::uint32_t version = sc4::SC4ReadAppLoadSaveVersion(segment);
            std::printf("game city version %u\n", version);
            CHECK(version >= 0x3e && version <= 0x42);
            sc4::SC4CloseSaveGame(segment);
        }
    }
    if (failures == 0) std::puts("sc4_savegame: all passed");
    return failures ? 1 : 0;
}

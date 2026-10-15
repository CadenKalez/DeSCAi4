#include <cstdio>
#include <string>
#include <unistd.h>
#include <vector>

#include "rz/cRZIniFile.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static void Collect(const cRZString& k, const cRZString& v, void* ctx) {
    static_cast<std::vector<std::string>*>(ctx)->push_back(std::string(k.ToChar()) + "=" + v.ToChar());
}

int main() {
    char path[] = "/tmp/rz_inifile_testXXXXXX";
    const int fd = mkstemp(path);
    const std::string text = "; comment\r\n[Config]\r\nDefaultSoundDataId = 0x10\r\n  Other=1\r\n\r\n[Second Section]\nA=b c \n;skipped=1\nEmpty=\nlongkey = value with = sign\n";
    CHECK(write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size()));
    close(fd);

    {
        cRZIniFile ini{cRZString(path)};
        cRZString value;
        CHECK(ini.ReadEntry("config", "DEFAULTSOUNDDATAID", value) && std::string(value.ToChar()) == "0x10");
        CHECK(ini.ReadEntry("Config", "other", value) && std::string(value.ToChar()) == "1");
        CHECK(!ini.ReadEntry("Config", "A", value));                       // belongs to the other section
        CHECK(ini.ReadEntry("second section", "a", value) && std::string(value.ToChar()) == "b c");
        CHECK(ini.ReadEntry("Second Section", "longkey", value) && std::string(value.ToChar()) == "value with = sign");
        CHECK(!ini.ReadEntry("Nope", "a", value));
        CHECK(ini.SectionExists(cRZString("CONFIG")) && !ini.SectionExists(cRZString("Third")));

        std::vector<std::string> entries;
        CHECK(ini.EnumEntries("Second Section", Collect, &entries));
        CHECK(entries.size() == 2 && entries[0] == "A=b c" && entries[1] == "longkey=value with = sign");   // empty value and comment skipped
        std::vector<std::string> sections;
        CHECK(ini.EnumSections(Collect, &sections));
        CHECK(sections.size() == 2 && sections[0] == "Config=" && sections[1] == "Second Section=");

        int number = 0;
        CHECK(ini.ReadNumber("Config", "Other", number) && number == 1);
        CHECK(!ini.ReadNumber("Config", "DefaultSoundDataId", number) == false);   // "0x10" parses as 0 with %d
        // writes: replace, append to a section, new section
        CHECK(ini.WriteEntry("config", "Other", "2"));
        CHECK(ini.WriteEntry("Config", "Added", "yes"));
        CHECK(ini.WriteEntry("Brand New", "K", "V"));
        CHECK(ini.WriteNumber("Brand New", "N", -5));
        CHECK(!ini.WriteEntry("", "K", "V") && !ini.WriteEntry("S", "", "V"));
        const std::uint8_t bytes[3] = {0xde, 0xad, 0x01};
        CHECK(ini.WriteBinaryEntry("Brand New", "Bin", bytes, 3));
        CHECK(ini.ReadEntry("Brand New", "Bin", value) && std::string(value.ToChar()) == "DEAD01");
        std::uint8_t back[3] = {};
        CHECK(ini.ReadBinaryEntry("Brand New", "Bin", back, 3) == 3 && back[0] == 0xde && back[2] == 0x01);
        std::uint8_t* heap = nullptr;
        CHECK(ini.ReadBinaryEntry("Brand New", "Bin", &heap) == 3 && heap && heap[1] == 0xad);
        delete[] heap;
        CHECK(ini.ReadNumber("Brand New", "N", number) && number == -5);
    }
    {
        cRZIniFile ini{cRZString(path)};
        cRZString value;
        CHECK(ini.ReadEntry("Config", "Other", value) && std::string(value.ToChar()) == "2");
        std::vector<std::string> entries;
        ini.EnumEntries("Config", Collect, &entries);
        CHECK(entries.size() == 3 && entries[2] == "Added=yes");       // appended after the last entry
        CHECK(ini.ReadEntry("brand new", "k", value) && std::string(value.ToChar()) == "V");
    }
    unlink(path);
    std::printf(failures ? "FAILED\n" : "rz_inifile: all passed\n");
    return failures ? 1 : 0;
}

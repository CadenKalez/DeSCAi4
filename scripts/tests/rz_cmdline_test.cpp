// cRZCmdLine: tokenizing, joining, switches and edits.
#include <cstdio>
#include <cstring>

#include "rz/cRZCmdLine.h"

static int g_failed = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_failed; } } while (0)
#define CHECK_STR(actual, expected) do { const char* a_ = (actual); if (std::strcmp(a_, (expected)) != 0) { std::printf("FAIL line %d: \"%s\" != \"%s\"\n", __LINE__, a_, (expected)); ++g_failed; } } while (0)

int main() {
    cRZCmdLine* line = new cRZCmdLine("  game.exe  -w -r:1024x768 \"a b\" /Name:\"Big City\" -x\"quoted\" tail  ");
    line->AddRef();
    CHECK(line->argc() == 7);
    CHECK_STR(line->argv(0), "game.exe");
    CHECK_STR(line->argv(3), "a b");                  // enclosing quotes stripped
    CHECK_STR(line->argv(4), "/Name:\"Big City\"");   // quotes only stripped when they wrap the whole token
    CHECK_STR(line->argv(9), "");                     // out of range
    CHECK_STR((*line)[9].ToChar(), "");
    CHECK_STR((*line)[2].ToChar(), "-r:1024x768");
    cRZString text;
    CHECK(line->GetCommandLineText(text)); CHECK(std::strncmp(text.ToChar(), "  game.exe", 10) == 0);

    CHECK(line->Find(cRZString("GAME.EXE"), false) == 0 && line->Find(cRZString("GAME.EXE"), true) == -1);
    CHECK(line->Find(cRZString("tail"), true) == 6 && line->Find(cRZString("nope"), false) == -1);
    CHECK(line->FindSubString(cRZString("768"), true) == 2 && line->FindSubString(cRZString("NAME"), false) == 4);
    CHECK(line->FindSubString(cRZString("NAME"), true) == -1);

    cRZString value("untouched");
    CHECK(line->IsSwitchPresent('w', value, false) && std::strcmp(value.ToChar(), "untouched") == 0);   // bare switch
    CHECK(line->IsSwitchPresent('W', value, false));                                                    // either case
    CHECK(line->IsSwitchPresent('x', value, true) && std::strcmp(value.ToChar(), "quoted") == 0);
    CHECK(line->IsSwitchPresent('x', value, false) && std::strcmp(value.ToChar(), "\"quoted\"") == 0);
    CHECK(!line->IsSwitchPresent('n', value, false));                                                   // "/Name" is no letter switch
    CHECK(line->GetIndexOfSwitch('w', 0) == 1 && line->GetIndexOfSwitch('w', 2) == -1 && line->GetIndexOfSwitch('q', 0) == -1);
    CHECK(line->GetIndexOfSwitch('w', -5) == 1);

    CHECK(line->IsSwitchPresent(cRZString("r"), value, true) && std::strcmp(value.ToChar(), "1024x768") == 0);
    CHECK(line->IsSwitchPresent(cRZString("NAME"), value, true) && std::strcmp(value.ToChar(), "Big City") == 0);
    CHECK(line->IsSwitchPresent(cRZString("name"), value, false) && std::strcmp(value.ToChar(), "\"Big City\"") == 0);
    CHECK(!line->IsSwitchPresent(cRZString("missing"), value, true) && !line->IsSwitchPresent(cRZString(""), value, true));
    CHECK(line->GetIndexOfSwitch(cRZString("W"), 0) == 1 && line->GetIndexOfSwitch(cRZString("x"), 0) == -1 && line->GetIndexOfSwitch(cRZString("r:1024x768"), 0) == 2);
    CHECK(line->IsSwitchPresent(cRZString("w")) && line->IsSwitchPresent(cRZString("r")) && !line->IsSwitchPresent(cRZString("zzz")));

    // Copy and edit.
    cRZCmdLine* copy = new cRZCmdLine(*line);
    CHECK(copy->argc() == 7 && std::strcmp(copy->argv(3), "a b") == 0);
    CHECK(copy->InsertArgument(cRZString("new arg"), 1));
    CHECK(copy->argc() == 8 && std::strcmp(copy->argv(1), "new arg") == 0);
    cRZString joined; copy->GetCommandLineText(joined);
    CHECK(std::strncmp(joined.ToChar(), "game.exe \"new arg\" -w", 21) == 0);
    CHECK(copy->InsertArgument(cRZString("last"), 99) && std::strcmp(copy->argv(8), "last") == 0);
    CHECK(copy->InsertArgument(cRZString("first"), -1) && std::strcmp(copy->argv(0), "first") == 0);
    CHECK(copy->EraseArgument(0) && std::strcmp(copy->argv(0), "game.exe") == 0);
    CHECK(copy->EraseArgument(99) && copy->argc() == 8 && std::strcmp(copy->argv(7), "tail") == 0);   // clamps to the last
    CHECK(copy->Release() == 0);

    cRZCmdLine* none = new cRZCmdLine("   ");
    none->AddRef();
    CHECK(none->argc() == 0 && none->EraseArgument(0) && none->InsertArgument(cRZString("x"), 5) && none->argc() == 1);
    void* p = nullptr;
    CHECK(none->QueryInterface(0xa31e539c, &p) && p == static_cast<cIGZCmdLine*>(none)); none->Release();
    CHECK(!none->QueryInterface(7, &p));
    none->Release();

    // Joining quotes arguments with blanks.
    std::vector<cRZString> args{cRZString("a"), cRZString("b c"), cRZString("")};
    cRZString out;
    cRZCmdLine::JoinArguments(args, out);
    CHECK_STR(out.ToChar(), "a \"b c\" ");
    line->Release();
    std::printf(g_failed ? "FAILED (%d)\n" : "OK\n", g_failed);
    return g_failed ? 1 : 0;
}

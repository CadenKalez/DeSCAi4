// nSCRes: tokenising, expressions, commands, macros, namespaces and the file parser.
#include <cmath>
#include <cstdio>
#include <fstream>

#include "sc4/SCResParser.h"

using namespace nSCRes;
static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
#define THROWS(expr, code_) do { bool t_ = false; try { expr; } catch (const cParseError& e_) { t_ = e_.Code() == (code_); } CHECK(t_); } while (0)

static void Write(const char* path, const char* text) { std::ofstream(path) << text; }

int main() {
    // expressions
    CHECK(ParseFloat("1+2*3") == 7.0f);
    CHECK(ParseFloat("(1+2)*3") == 9.0f);
    CHECK(ParseFloat("2^3^2") == 512.0f);
    CHECK(ParseFloat("-3 + 5") == 2.0f);
    CHECK(ParseFloat("10%3") == 1.0f);
    CHECK(ParseFloat("sqrt(16)") == 4.0f);
    CHECK(std::fabs(ParseFloat("sind(90)") - 1.0f) < 1e-6f);
    CHECK(std::fabs(ParseFloat("datan2(1,1)") - 45.0f) < 1e-4f);
    CHECK(ParseFloat("0x10") == 16.0f);
    CHECK(ParseFloat("pow(2,10)") == 1024.0f);
    CHECK(ParseFloat("floor(2.7)+ceil(2.1)") == 5.0f);
    THROWS(ParseFloat(""), 1);
    THROWS(ParseFloat("1 +"), 1);
    THROWS(ParseFloat("foo"), 1);
    THROWS(ParseFloat("2 3"), 1);
    CHECK(ParseInt("2.5") == 2 && ParseInt("3.5") == 4 && ParseInt("-7") == -7);
    CHECK(ParseUint("12") == 12);
    CHECK(ParseBool("TRUE") && ParseBool("on") && !ParseBool("off") && !ParseBool("False") && !ParseBool("") && ParseBool("3") && !ParseBool("0"));
    const Color4 c = ParseColorAlpha("0.5 0.25 1");
    CHECK(c.r == 0.5f && c.g == 0.25f && c.b == 1.0f && c.a == 1.0f);
    CHECK(ParseVector3("1 2").z == 0.0f && ParseVector3("1 2").y == 2.0f);
    THROWS(ParseVector2("1 2 3"), 1);
    THROWS(ParseColor("1 2"), 1);

    static const cEnumParseInfo table[] = {{"red", 1}, {"green", 2}, {nullptr, 0}};
    CHECK(ParseEnum("green", table) == 2 && std::string(EnumToString(1, table)) == "red" && !EnumToString(9, table));
    THROWS(ParseEnum("blue", table), 4);

    // tokenising
    Args a;
    CHECK(SplitIntoArguments(a, "set  name \"two words\" [x y] tail # comment") == 5);
    CHECK(a[2] == "two words" && a[3] == "x y" && a[4] == "tail");
    CHECK(CountArguments("a 'b c' (d)") == 3);
    THROWS(SplitIntoArguments(a, "a \"b"), 5);
    THROWS(SplitIntoArguments(a, "a [b"), 6);
    CHECK(SplitIntoArguments(a, "# only a comment") == 0);
    Args sw{"cmd", "-a", "x", "y", "-b", "z"};
    CHECK(FindSwitchIndex(sw, "b") == 4 && FindSwitchIndex(sw, "q") == -1);
    std::vector<int> idx;
    CHECK(FindSwitchArgumentIndices(idx, sw, "a", 1) && idx.size() == 2 && sw[1] == "+a");
    CHECK(UnhandledSwitchesArePresent(sw));
    CHECK(FindSwitchArgumentIndices(idx, sw, "b", 1) && !UnhandledSwitchesArePresent(sw));
    std::string rest = "key=value=more";
    CHECK(NextToken(rest, "=") == "key" && rest == "value=more");

    // commands and variables
    cCommandParser parser;
    parser.Init();
    parser.ParseBegin();
    parser.ParseCommand("version 3");
    CHECK(parser.Version() == 3);
    parser.ParseCommand("set name hello");
    parser.ParseCommand("seti n 2+3");
    parser.ParseCommand("setf f 1.5*2");
    CHECK(std::string(parser.Variable("name")) == "hello" && std::string(parser.Variable("n")) == "5" && std::string(parser.Variable("f")) == "3");
    parser.ParseCommand("set copy $name");
    parser.ParseCommand("set pair \"$name world\"");
    CHECK(std::string(parser.Variable("copy")) == "hello" && std::string(parser.Variable("pair")) == "\"hello\" world");
    parser.ParseCommand("set braced \"x${name}y\"");
    CHECK(std::string(parser.Variable("braced")) == "xhelloy");
    parser.ParseCommand("setv3 v \"1 2 3\" -scale 2");
    CHECK(std::string(parser.Variable("v")) == "2 4 6");
    THROWS(parser.ParseCommand("set bad $nothing"), 1);
    THROWS(parser.ParseCommand("frobnicate"), 1);
    THROWS(parser.ParseCommand("end"), 1);

    // namespaces
    parser.ParseCommand("namespace ns");
    parser.ParseCommand("set inner 7");
    parser.ParseCommand("set seesOuter $name");
    CHECK(std::string(parser.Variable("inner")) == "7" && std::string(parser.Variable("seesOuter")) == "hello");
    parser.ParseCommand("end");
    CHECK(parser.Variable("inner") == nullptr);
    parser.PushNamespace("ns");
    CHECK(std::string(parser.Variable("inner")) == "7");
    parser.PopNamespace();
    parser.PurgeNamespace("ns:");
    parser.PushNamespace("ns");
    CHECK(parser.Variable("inner") == nullptr);
    parser.PopNamespace();

    // macros
    parser.ParseCommand("define greet who");
    parser.ParseCommand("set msg \"hi-%who\"");
    THROWS(parser.ParseCommand("define again x"), 1);
    parser.ParseCommand("enddef");
    parser.ParseCommand("create greet bob");
    CHECK(std::string(parser.Variable("msg")) == "hi-bob");
    THROWS(parser.ParseCommand("create greet a b"), 1);
    THROWS(parser.ParseCommand("create nothing a"), 1);
    parser.ParseCommand("define item \"i n\"");
    parser.ParseCommand("set \"item%{i}\" %i");
    parser.ParseCommand("enddef");
    parser.ParseCommand("arrayCreate item 3");
    CHECK(std::string(parser.Variable("item2")) == "2");
    parser.ParseEnd();

    // files: comments, includes, errors
    Write("/tmp/scres_a.txt", "version 2\nset one 1\n#< block\nset hidden 1\n#> set after 5\ninclude scres_b.txt\nsinclude scres_missing.txt\nset last $b\n");
    Write("/tmp/scres_b.txt", "set b 22\n");
    cFileParser files;
    cCommandParser* p2 = new cCommandParser;
    files.SetParser(p2);
    files.AddInputFilePath("/tmp/scres_a.txt", 0);
    CHECK(files.NumInputFiles() == 1 && files.ParseFilesModified());
    CHECK(files.ParseFiles());
    CHECK(std::string(files.ErrorString()) == "Successfully parsed 9 lines");
    CHECK(p2->Variable("hidden") == nullptr && std::string(p2->Variable("after")) == "5" && std::string(p2->Variable("last")) == "22");
    CHECK(!files.ParseFilesModified());
    Write("/tmp/scres_c.txt", "set ok 1\ninclude scres_missing.txt\n");
    cFileParser failing;
    cCommandParser* p3 = new cCommandParser;
    failing.SetParser(p3);
    failing.AddInputFilePath("/tmp/scres_c.txt", 0);
    bool threw = false;
    try { failing.ParseFiles(); } catch (const cParseError& e) { threw = std::string(e.What()).find("at scres_c.txt:2") != std::string::npos; }
    CHECK(threw);
    CHECK(files.DoMessage(cFileParser::kMessageFilesChanged) && files.ParseFilesModified());

    if (failures == 0) std::puts("sc4_scresparser: all passed");
    return failures ? 1 : 0;
}

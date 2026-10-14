#include "rz/cRZString.h"
#include "rz/cRZDate.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main() {
    cRZString s("Hello, World");
    assert(s.Strlen() == 12 && std::strcmp(s.ToChar(), "Hello, World") == 0 && s.Data() == s.ToChar());
    // equality / compare
    assert(s.IsEqualToChars("hello, world", 0xFFFFFFFFu, false) && !s.IsEqualToChars("hello, world", 0xFFFFFFFFu, true));
    assert(!s.IsEqualToChars("Hello, Worl", 0xFFFFFFFFu, true));
    assert(s.CompareToChars("Hello, World", 0xFFFFFFFFu, true) == 0);
    assert(s.CompareToChars("Hello, World!", 0xFFFFFFFFu, true) < 0);   // shorter sorts first
    assert(s.CompareToChars("Hello", 0xFFFFFFFFu, true) > 0);
    assert(s.CompareToChars("HELLO, WORLD", 0xFFFFFFFFu, false) == 0);
    assert(s.CompareToChars("Apple", 0xFFFFFFFFu, true) > 0 && s.CompareToChars("Zebra", 0xFFFFFFFFu, true) < 0);
    // find / rfind
    assert(s.FindChars("o", 0, true) == 4 && s.FindChars("o", 5, true) == 8 && s.FindChars("zzz", 0, true) == std::string::npos);
    assert(s.FindChars("WORLD", 0, false) == 7 && s.FindChars("WORLD", 0, true) == std::string::npos);
    assert(s.ReverseFindChars("o", 0xFFFFFFFFFFFFFFFFull, true) == 8 && s.ReverseFindChars("O", 0xFFFFFFFFFFFFFFFFull, false) == 8);
    // edits
    cRZString t("abcdef");
    t.InsertChars(3, "XYZ", 3); assert(t.str() == "abcXYZdef");
    t.Erase(0, 3);         assert(t.str() == "XYZdef");
    t.ReplaceRangeWithChars(1, "--", 2); assert(t.str() == "X--def");        // replace 2 chars at 1 with "--"
    t.AppendChars("!!", 1);     assert(t.str() == "X--def!");
    t.Resize(10);          assert(t.Strlen() == 10 && t.str()[9] == '\0');
    t.Resize(3);           assert(t.str() == "X--");
    t.AssignFromCString(nullptr);   assert(t.Strlen() == 0);
    // Sprintf
    cRZString f; f.Sprintf("%u/%u/%4u", 7u, 4u, 2005u); assert(f.str() == "7/4/2005");
    f.Sprintf("%s-%d", "ab", 42); assert(f.str() == "ab-42");
    // sub-strings
    cRZString m("0123456789");
    assert(m.Left(3).str() == "012" && m.Left(99).str() == "0123456789");
    assert(m.Mid(4, 3).str() == "456" && m.Mid(8, 99).str() == "89" && m.Mid(10, 1).str().empty());
    assert(m.Right(3).str() == "789" && m.Right(99).str() == "0123456789");
    assert(m.BeginsWith("012", 3) && !m.BeginsWith("013", 3) && m.EndsWith("789", 3) && !m.EndsWith("788", 3));
    // trim: only spaces and tabs
    cRZString tr(" \t hi \t\n "); tr.Trim(); assert(tr.str() == "hi \t\n");
    tr = cRZString("  x  "); tr.LTrim(); assert(tr.str() == "x  "); tr.RTrim(); assert(tr.str() == "x");
    // case
    cRZString c("MiXeD 123"); c.MakeLower(); assert(c.str() == "mixed 123"); c.MakeUpper(); assert(c.str() == "MIXED 123");
    // tokens
    cRZString p("one,two,,three");
    cRZString tok;
    assert(p.SplitTokenAtDelimiter(',', tok) && tok.str() == "one" && p.str() == "two,,three");
    assert(p.SplitTokenSeparated(',', tok) && tok.str() == "two" && p.str() == "three");
    assert(p.SplitTokenSeparated(',', tok) && tok.str() == "three" && p.str().empty());
    assert(!p.SplitTokenSeparated(',', tok));
    cRZString q("a--b--c");
    cRZString first = q.SplitToken("--"); assert(first.str() == "a" && q.str() == "b--c");
    assert(q.SplitToken("zz").str().empty() && q.str() == "b--c");
    assert(q.SplitToken(nullptr).str() == "b--c");
    cRZString r(",,x,y");
    assert(r.SplitTokenSeparated(',', tok) && tok.str() == "x" && r.str() == "y");
    cRZString d("k=v;rest");
    assert(d.DiscardThroughDelimiter('=') && d.str() == "v;rest");
    assert(!d.DiscardThroughDelimiter('#') && d.str().empty());
    // refcounting + QueryInterface (heap object)
    auto* h = new cRZString("heap");
    void* out = nullptr;
    assert(h->QueryInterface(kGZIID_cIGZString, &out) && out == h);   // AddRef -> 1
    assert(h->AddRef() == 2);
    assert(h->Release() == 1);
    assert(!h->QueryInterface(0x1234, &out));
    assert(h->Release() == 0);                                         // deleted
    // date <-> string link
    assert(cRZDate(7, 4, 2005).DateString(2) == "4/7/2005");
    std::puts("cRZString ok");
}

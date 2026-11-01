#include <ymir/util/string.hpp>
#include <cstdio>
#include <string>
int main() {
    static_assert(sizeof(wchar_t) == 2, "mimic PS5 wchar_t");
    const char *cases[] = {"", "ASCII text", "Ñandú café", "サターン", "セガサターン 🎮 emoji", "\xF0\x9F\x98\x80"};
    int bad = 0;
    for (const char *c : cases) {
        std::wstring w = util::StringToWString(c);
        std::string back = util::WStringToString(w);
        if (back != c) { printf("MISMATCH: '%s' -> '%s'\n", c, back.c_str()); bad++; }
    }
    // emoji must become a surrogate pair
    std::wstring w = util::StringToWString("\xF0\x9F\x98\x80");
    if (w.size() != 2 || (unsigned)w[0] != 0xD83D || (unsigned)w[1] != 0xDE00) { puts("surrogate FAIL"); bad++; }
    // malformed input -> empty
    if (!util::StringToWString("\xC3").empty() || !util::StringToWString("\xFF").empty()) { puts("malformed FAIL"); bad++; }
    printf("utf16 tests: %s\n", bad ? "FAIL" : "PASS");
    return bad;
}

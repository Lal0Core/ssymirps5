// The system area (IP.BIN) at the start of a Sega Saturn disc: its title, product number and region.
//
// A renamed backup keeps its disc's own header, so the title in it names the original game when the file name does
// not (fe_artmatch.h ResolveHeader). The layout, the first 256 bytes of the disc's first data sector:
//   $00  16 bytes  "SEGA SEGASATURN "
//   $20  10 bytes  the product number ("MK-81009", "T-12705G")
//   $40  10 bytes  the area codes: J Japan, T Asia (NTSC), U North America, B Brazil, K Korea, A East Asia (PAL),
//                  E Europe, L Latin America
//   $60 112 bytes  the game's title (often Shift-JIS on Japanese discs: only its ASCII part is kept)
// The data sector is at 0 of an .iso or a 2048-byte track, 16 bytes in for a raw 2352-byte one (MODE1/2352).
//
// From SSYmirPS5's Sega header reader (MIT), for the Saturn's header.
// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

namespace fe { namespace romheader {
struct Info
{
    std::string title;         // the title to look up: printable ASCII
    std::string other;         // the product number ("MK-81009"), "" if none
    uint8_t country = 0xFF;    // 0: Japan only; 1: any other region set
    int disc = 0, discs = 0;   // $38 "CD-2/3": disc 2 of 3 (0 when the field says nothing)
    std::string date;          // $30 "19980404" (yyyymmdd), "" if not digits
    std::string areas;         // $40 the area codes as written ("JTUE")
    bool japan() const { return country == 0; }
};
// Fills out with n bytes at offset; false if the image has fewer.
using Reader = std::function<bool(uint64_t offset, uint8_t* out, size_t n)>;

// Title bytes as text: printable ASCII kept, runs of spaces made one, anything else (Shift-JIS) a space. "" when
// fewer than 3 letters or digits are left.
inline std::string Title(const uint8_t* t, size_t n)
{
    std::string out;
    int letters = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned char c = t[i];
        const bool text = c >= 0x20 && c <= 0x7E;
        if (!text || c == ' ')
        {
            if (!out.empty() && out.back() != ' ') out += ' ';
            continue;
        }
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) ++letters;
        out += char(c);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return letters >= 3 ? out : std::string();
}
// The first 256 bytes of the system area: true when it is a Saturn disc's and has a title.
inline bool Parse(const uint8_t* h, Info* out)
{
    if (std::memcmp(h, "SEGA SEGASATURN", 15) != 0) return false;
    bool j = false, other = false;
    for (int i = 0; i < 10; ++i)
    {
        const char c = char(h[0x40 + i]);
        if (c == 'J') j = true;
        else if (c == 'T' || c == 'U' || c == 'B' || c == 'K' || c == 'A' || c == 'E' || c == 'L') other = true;
    }
    out->country = (j && !other) ? 0 : 1;
    out->title = Title(h + 0x60, 112);
    {
        std::string dt(reinterpret_cast<const char*>(h + 0x30), 8);
        bool digits = true;
        for (char c : dt)
            digits = digits && c >= '0' && c <= '9';
        out->date = digits ? dt : std::string();
        std::string ar;
        for (int i = 0; i < 10; ++i)
            if (h[0x40 + i] > 0x20 && h[0x40 + i] < 0x7F)
                ar += char(h[0x40 + i]);
        out->areas = ar;
    }
    {
        // $38, 8 bytes: "CD-1/2  " (the disc's number and how many the game has)
        char f[9] = {};
        memcpy(f, h + 0x38, 8);
        int a = 0, b = 0;
        if (sscanf(f, "CD-%d/%d", &a, &b) == 2 && a >= 1 && b >= a && b <= 9)
        {
            out->disc = a;
            out->discs = b;
        }
    }
    std::string product(reinterpret_cast<const char*>(h + 0x20), 10);
    while (!product.empty() && (product.back() == ' ' || product.back() == '\0')) product.pop_back();
    out->other = Title(reinterpret_cast<const uint8_t*>(product.data()), product.size()).empty() ? std::string() : product;
    // a title of one or two letters ("D") is no title to look up, but the disc's product number still names it
    return !out->title.empty() || !out->other.empty();
}
// The Saturn has no cartridge games.
inline bool ReadCart(const Reader&, uint64_t, Info*)
{
    return false;
}
// The first sector of a disc's data track: 2048-byte sectors (an .iso) have it at 0, raw 2352-byte ones 16 bytes in.
inline bool ReadDisc(const Reader& rd, Info* out)
{
    uint8_t h[256];
    if (rd(0, h, sizeof(h)) && Parse(h, out)) return true;
    return rd(16, h, sizeof(h)) && Parse(h, out);
}
}} // namespace fe::romheader

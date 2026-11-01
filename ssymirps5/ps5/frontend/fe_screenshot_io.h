// Full-screen PNG writer and screenshot hotkey. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>

namespace screenshot
{
// A release within 1 second opens the menu. Holding over 1 second captures once,
// immediately, with no upper time limit. A captured hold can never open the menu.
enum class TouchAction { None, Capture, Menu };
struct TouchGesture
{
    double started = -1;
    bool cancelled = false, captured = false;
    TouchAction Update(bool held, bool modifier, double now, bool captures_enabled)
    {
        if (held)
        {
            if (started < 0) started = now;
            cancelled = cancelled || modifier;
            if (!cancelled && !captured && captures_enabled && now - started > 1.0)
            { captured = true; return TouchAction::Capture; }
            return TouchAction::None;
        }
        TouchAction action = TouchAction::None;
        if (started >= 0 && !cancelled && !captured)
        {
            const double duration = now - started;
            if (duration >= 0 && duration <= 1.0) action = TouchAction::Menu;
            else if (duration > 1.0 && captures_enabled) action = TouchAction::Capture;
        }
        started = -1; cancelled = captured = false;
        return action;
    }
};
inline std::string SafeName(const std::string& name)
{
    std::string out;
    for (unsigned char c : name)
    {
        if (out.size() >= 100) break;
        out += c < 32 || c >= 127 || std::strchr("/\\:*?\"<>|", c) ? '_' : char(c);
    }
    if (out.empty() || out == "." || out == "..") out = "Captura";
    return out;
}
inline void Be32(unsigned char* p, uint32_t n)
{ p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }
inline bool Chunk(FILE* f, const char* type, const unsigned char* data, size_t n)
{
    unsigned char head[8], tail[4]; Be32(head, uint32_t(n)); std::memcpy(head+4,type,4);
    uLong crc=crc32(0, reinterpret_cast<const Bytef*>(type),4);
    if (n) crc=crc32(crc,data,uInt(n));
    Be32(tail,uint32_t(crc));
    return fwrite(head,1,8,f)==8 && (!n || fwrite(data,1,n,f)==n) && fwrite(tail,1,4,f)==4;
}
// Opaque RGB PNG, streaming rows: no extra full-frame compression buffer.
inline bool WritePng(FILE* f, const uint8_t* pixels, int w, int h, bool bgra)
{
    if (!f || !pixels || w<=0 || h<=0 || w>8192 || h>8192) return false;
    const unsigned char sig[]={137,80,78,71,13,10,26,10};
    unsigned char ihdr[13]={}; Be32(ihdr,uint32_t(w)); Be32(ihdr+4,uint32_t(h)); ihdr[8]=8; ihdr[9]=2;
    if (fwrite(sig,1,8,f)!=8 || !Chunk(f,"IHDR",ihdr,13)) return false;
    z_stream z={}; if (deflateInit(&z,Z_BEST_SPEED)!=Z_OK) return false;
    std::vector<unsigned char> row(size_t(w)*3+1), output(65536);
    bool ok=true;
    for (int y=0; y<h && ok; ++y)
    {
        row[0]=0;
        for (int x=0;x<w;++x)
        {
            const uint8_t* p=pixels+(size_t(y)*w+x)*4;
            row[size_t(x)*3+1]=p[bgra?2:0]; row[size_t(x)*3+2]=p[1]; row[size_t(x)*3+3]=p[bgra?0:2];
        }
        z.next_in=row.data(); z.avail_in=uInt(row.size());
        const int flush=y==h-1?Z_FINISH:Z_NO_FLUSH;
        int status=Z_OK;
        do {
            z.next_out=output.data(); z.avail_out=uInt(output.size());
            status=deflate(&z,flush);
            if (status!=Z_OK && status!=Z_STREAM_END) { ok=false; break; }
            const size_t n=output.size()-z.avail_out;
            if (n && !Chunk(f,"IDAT",output.data(),n)) { ok=false; break; }
        } while (z.avail_in || (flush==Z_FINISH && status!=Z_STREAM_END));
    }
    deflateEnd(&z);
    return ok && Chunk(f,"IEND",nullptr,0) && !ferror(f);
}
} // namespace screenshot

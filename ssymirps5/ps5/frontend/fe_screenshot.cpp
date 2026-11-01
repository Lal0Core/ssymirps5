// SPDX-License-Identifier: MIT
#include "fe_screenshot.h"
#include "fe_screenshot_io.h"
#include "fe_emu.h"
#include "fe_i18n.h"
#include "OrbisPaths.h"
#include "ProsperoNotify.h"
#include "ProsperoSce.h"
#include <atomic>
#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include "ProsperoThread.h"
#include <unistd.h>

namespace screenshot
{
namespace
{
std::atomic<bool> enabled{true}, pending{false}, busy{false};
TouchGesture touch;
std::atomic<bool> menu_requested{false};
struct Writer { ps5::BigThread thread; } writer;
}
uint32_t PollButtons(uint32_t raw, double now)
{
    constexpr uint32_t modifiers=SCE_PAD_BUTTON_UP|SCE_PAD_BUTTON_DOWN|SCE_PAD_BUTTON_LEFT|
        SCE_PAD_BUTTON_RIGHT|SCE_PAD_BUTTON_L3|SCE_PAD_BUTTON_R3;
    const TouchAction action=touch.Update((raw&SCE_PAD_BUTTON_TOUCH_PAD)!=0,
        (raw&modifiers)!=0,now,enabled.load());
    if (action==TouchAction::Menu) menu_requested=true;
    if (action==TouchAction::Capture)
    {
        bool expected=false;
        if (busy.compare_exchange_strong(expected,true)) pending=true;
    }
    return raw; // preserve touchpad + D-pad, L3 rewind and R3 fast-forward
}
bool TakeMenuRequest() { return menu_requested.exchange(false); }

void Configure(bool on) { enabled=on; }
bool Pending() { return pending.load(); }
void Fail()
{
    if(pending.exchange(false)) ProsperoNotify("%s",fe::T("Could not save screenshot"));
    busy=false;
}
void Submit(const uint8_t* pixels,int w,int h,bool bgra)
{
    if (!pending.exchange(false)) return;
    if (!pixels || w<=0 || h<=0 || w>8192 || h>8192)
    { pending=true; Fail(); return; }
    if (writer.thread.joinable()) writer.thread.join();
    // Get game context on the presentation thread, never from the writer thread.
    const std::string name=SafeName(emu::GameLoaded()?emu::GameName():"Biblioteca");
    const std::string dir=OrbisDir("screenshots");
    const std::string saved=fe::T("Screenshot saved"), failed=fe::T("Could not save screenshot");
    const time_t now=time(nullptr);
    tm date={}; localtime_r(&now,&date);
    char stamp[40]; strftime(stamp,sizeof(stamp),"_%Y-%m-%d_%H-%M-%S",&date);
    const std::string stem=name+stamp;
    std::vector<uint8_t> copy(pixels,pixels+size_t(w)*h*4);
    // Notify only after the actual composed frame has been copied, so the notice is not in the PNG.
    ProsperoNotify("%s",fe::T("Screenshot taken."));
    writer.thread=ps5::BigThread([copy=std::move(copy),dir,stem,saved,failed,w,h,bgra]() {
        bool ok=false;
        std::string path;
        if (OrbisMkdirs(dir))
            for (int i=0;i<10000;++i)
            {
                path=dir+"/"+stem+(i?"_"+std::to_string(i):"")+".png";
                const int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0644);
                if (fd<0) { if(errno==EEXIST) continue; break; }
                FILE* f=fdopen(fd,"wb");
                if (f) { ok=WritePng(f,copy.data(),w,h,bgra); if(fclose(f)!=0) ok=false; }
                else close(fd);
                if(!ok) unlink(path.c_str());
                break;
            }
        OrbisLog("[screenshot] %s: %s (%dx%d)",ok?"saved":"failed",path.c_str(),w,h);
        if(ok) ProsperoNotify("%s: %s",saved.c_str(),path.substr(path.find_last_of('/')+1).c_str());
        else ProsperoNotify("%s",failed.c_str());
        busy=false;
    }, 2 * 1024 * 1024);
    if (!writer.thread.joinable()) { ProsperoNotify("%s",failed.c_str()); busy=false; }
}
void Shutdown()
{
    enabled=false;
    if(writer.thread.joinable()) writer.thread.join();
    pending=false; busy=false; menu_requested=false;
}
} // namespace screenshot

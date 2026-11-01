// Screenshot regression tests. SPDX-License-Identifier: MIT
#include "fe_screenshot.h"
#include "fe_screenshot_io.h"
#include "ProsperoSce.h"
#include <cassert>
#include <cstdarg>
#include <filesystem>
#include <fstream>

namespace crashlog { void* ArmThread() { return nullptr; } void DisarmThread(void*) {} }
static std::string root;
static bool loaded=false;
static int notices=0;
static std::vector<std::string> messages;
std::string OrbisDir(const char* sub) { return root+"/"+sub; }
bool OrbisMkdirs(const std::string& p) { std::error_code ec; std::filesystem::create_directories(p,ec); return !ec; }
void OrbisLog(const char*,...) {}
void ProsperoNotify(const char* format,...) { char text[1024]; va_list args; va_start(args,format); vsnprintf(text,sizeof(text),format,args); va_end(args); messages.emplace_back(text); ++notices; }
namespace fe { const char* T(const char* s) { return s; } }
namespace emu { bool GameLoaded() { return loaded; } std::string GameName() { return "Test/Game:Saturn"; } }

int main(int argc,char** argv)
{
    assert(argc==2); root=argv[1];
    using namespace screenshot;
    TouchGesture gesture;
    auto act=[&](bool held,double at,bool mod=false,bool captures=true) {
        return gesture.Update(held,mod,at,captures);
    };
    using A=TouchAction;
    assert(act(true,0)==A::None); assert(act(false,0.5)==A::Menu);
    assert(act(true,2)==A::None); assert(act(false,3)==A::Menu); // exactly 1s is menu
    assert(act(true,4)==A::None); assert(act(true,5)==A::None);
    assert(act(true,5.001)==A::Capture); // capture while still held
    assert(act(true,6)==A::None); assert(act(true,20)==A::None);
    assert(act(false,21)==A::None); // no repeat or menu, even after a very long hold
    assert(act(true,22)==A::None); assert(act(true,25)==A::Capture); // no 2s upper bound
    assert(act(false,26)==A::None);
    assert(act(true,27)==A::None); assert(act(false,28.1)==A::Capture); // sparse polling fallback
    assert(act(true,29,false,false)==A::None); assert(act(true,35,false,false)==A::None);
    assert(act(false,36,false,false)==A::None); // disabled long hold does not open menu
    assert(act(true,37,false,false)==A::None); assert(act(false,38,false,false)==A::Menu);
    assert(act(true,39)==A::None); assert(act(true,39.2,true)==A::None);
    assert(act(true,42)==A::None); assert(act(false,43)==A::None); // modifier cancels actions
    const uint32_t touch=SCE_PAD_BUTTON_TOUCH_PAD;
    assert(SafeName("../Test:Game") == ".._Test_Game");
    assert(SafeName("")=="Captura");
    const uint8_t rgba[]={255,0,0,0, 0,255,0,255, 0,0,255,255, 255,255,255,255};
    const uint8_t bgra[]={0,0,255,255, 0,255,0,255, 255,0,0,255, 255,255,255,255};
    std::filesystem::create_directories(root);
    for (int format=0;format<2;++format)
    {
        FILE* f=fopen((root+(format?"/bgra.png":"/rgba.png")).c_str(),"wb");
        assert(f && WritePng(f,format?bgra:rgba,2,2,format)); assert(fclose(f)==0);
    }
    Configure(true);
    assert(PollButtons(SCE_PAD_BUTTON_OPTIONS,0)==SCE_PAD_BUTTON_OPTIONS);
    assert(PollButtons(SCE_PAD_BUTTON_L3,1)==SCE_PAD_BUTTON_L3); // rewind stays usable
    const uint32_t menu=SCE_PAD_BUTTON_L3|SCE_PAD_BUTTON_R3;
    assert(PollButtons(menu,2)==menu && !Pending() && !TakeMenuRequest()); // stick combination no longer requests a menu
    PollButtons(0,3);
    PollButtons(touch,4); PollButtons(touch,5); assert(!Pending());
    PollButtons(touch,5.1); assert(Pending()); // already requested while held
    Submit(rgba,2,2); Shutdown(); assert(notices==2);
    Configure(true); PollButtons(touch,5.8); assert(!Pending());
    PollButtons(0,5.9); assert(!Pending() && !TakeMenuRequest());
    loaded=true; Configure(true); PollButtons(touch,6); PollButtons(0,7.1);
    assert(Pending()); Submit(bgra,2,2,true); Shutdown(); assert(notices==4);
    Configure(true); PollButtons(touch,8); assert(!TakeMenuRequest()); PollButtons(0,9);
    assert(TakeMenuRequest() && !TakeMenuRequest() && !Pending()); // exact 1s opens menu once
    PollButtons(touch,10); PollButtons(touch,12); assert(Pending() && !TakeMenuRequest());
    Fail();
    PollButtons(touch,12.5); PollButtons(0,13); assert(!Pending() && !TakeMenuRequest()); // no duplicate or menu
    PollButtons(touch,14); PollButtons(touch|SCE_PAD_BUTTON_UP,14.2);
    PollButtons(0,14.8); assert(!Pending() && !TakeMenuRequest());
    Configure(false); PollButtons(touch,17); PollButtons(0,18.1); assert(!Pending());
    PollButtons(touch,19); PollButtons(0,20); assert(TakeMenuRequest());
    int files=0;
    for (auto& f:std::filesystem::directory_iterator(root+"/screenshots"))
    {
        ++files;
        assert(f.path().extension()==".png");
        const auto name=f.path().filename().string();
        assert(name.rfind("Biblioteca_",0)==0 || name.rfind("Test_Game_Saturn_",0)==0);
    }
    assert(files==2);
    assert(messages[0]=="Screenshot taken.");
    assert(messages[1].find("Screenshot saved:")==0);
    Configure(true); PollButtons(touch,23); PollButtons(0,24.1); assert(Pending());
    Fail(); assert(!Pending()); Shutdown(); assert(notices==6);
    puts("touchpad boundaries, mutually exclusive menu/capture, modifiers, controls, PNG and async writer: PASS");
}

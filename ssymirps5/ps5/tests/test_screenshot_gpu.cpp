// Offscreen Vulkan composition + actual screenshot writer. SPDX-License-Identifier: MIT
#include "ProsperoGpu.h"
#include "ProsperoSlang.h"
#include "fe_screenshot.h"
#include "ProsperoSce.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
static std::string root;
const std::string& OrbisRoot() { return root; }
std::string OrbisDir(const char* s) { return root+"/"+s; }
bool OrbisMkdirs(const std::string& p) { std::error_code e;std::filesystem::create_directories(p,e);return !e; }
void OrbisLog(const char* f,...) { va_list a;va_start(a,f);vfprintf(stderr,f,a);fputc('\n',stderr);va_end(a); }
void ProsperoNotify(const char* f,...) { va_list a;va_start(a,f);vfprintf(stderr,f,a);fputc('\n',stderr);va_end(a); }
namespace crashlog { void* ArmThread() { return nullptr; } void DisarmThread(void*) {} }
namespace fe {const char* T(const char* s) {return s;} }
namespace emu {bool GameLoaded() {return false;} std::string GameName() {return "";} }
static std::vector<uint8_t> final_pixels;
extern "C" void HostGpuFlip(const uint8_t* p,int w,int h,bool) {final_pixels.assign(p,p+size_t(w)*h*4);}
namespace ps5slang {
void ScanExternal() {}
void CheckAllExternal() {}
int PresetCount() {return kBuiltinPresetCount;}
const Preset& PresetAt(int i) {return kBuiltinPresets[i];}
bool IsExternal(int) {return false;}
bool EnsureCompiled(int,std::string*) {return true;}
bool CompileGlsl(const std::string& src,bool vertex,std::vector<uint32_t>& spv,std::string& err) {
    const std::string stage=vertex?"vert":"frag";
    const std::string in=root+"/shader."+stage, out=root+"/shader.spv";
    {std::ofstream f(in);f<<src;}
    const std::string cmd="glslangValidator -V -S "+stage+" "+in+" -o "+out+" >/dev/null";
    if(std::system(cmd.c_str())!=0){err="test shader compilation failed";return false;}
    std::ifstream f(out,std::ios::binary|std::ios::ate);const auto size=f.tellg();f.seekg(0);
    spv.resize(size_t(size)/4);f.read(reinterpret_cast<char*>(spv.data()),size);return bool(f);
}
}
int main(int argc,char** argv) {
    assert(argc==2);root=argv[1];std::filesystem::create_directories(root);
    assert(ps5gpu::Init(320,180,true,640,360));
    assert(ps5gpu::SceneAvailable());
    std::vector<uint32_t> blue(16,0xffff0000u); // RGBA blue
    assert(ps5gpu::SceneTexture(42,blue.data(),4,4,false));
    ps5gpu::SceneQuad q; q.tex=42;
    q.x[0]=q.x[2]=0;q.x[1]=q.x[3]=320;
    q.y[0]=q.y[1]=0;q.y[2]=q.y[3]=180;
    ps5gpu::SetScene({q});
    std::vector<uint32_t> ui(320*180,0); // transparent UI over the blue scene
    for(int y=0;y<30;y++) for(int x=0;x<30;x++)ui[y*320+x]=0xff0000ffu; // red UI corner
    screenshot::Configure(true);
    const uint32_t chord=SCE_PAD_BUTTON_TOUCH_PAD;
    screenshot::PollButtons(chord,0);screenshot::PollButtons(chord,1.1);assert(screenshot::Pending());
    assert(ps5gpu::Present(ui.data(),{0,0,320,180},false));
    screenshot::Shutdown();assert(!screenshot::Pending());
    assert(final_pixels.size()==640*360*4);
    // BGRA target: opaque red UI, opaque blue GPU backdrop. The UI surface alone cannot pass this.
    const auto red=&final_pixels[(size_t(10)*640+10)*4];assert(red[2]>240 && red[0]<10);
    const auto blue_pixel=&final_pixels[(size_t(180)*640+320)*4];assert(blue_pixel[0]>240 && blue_pixel[2]<10);
    std::ofstream f(root+"/expected.bgra",std::ios::binary);f.write(reinterpret_cast<char*>(final_pixels.data()),final_pixels.size());f.close();
    ps5gpu::Shutdown();puts("Vulkan final composition screenshot: PASS");
}

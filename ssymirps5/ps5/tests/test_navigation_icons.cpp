// Real navigation hint renderer regression test. SPDX-License-Identifier: MIT
#include "fe_style.h"
#include "ProsperoVideo.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
static int scale=1;
static bool gpu=false;
static std::vector<uint32_t> pixels;
namespace ps5video {
uint32_t* Surface(){return pixels.data();}
int PixelScale(){return scale;}
int SurfaceWidth(){return 1920*scale;}
int SurfaceHeight(){return 1080*scale;}
bool GpuActive(){return gpu;}
}
static int light(int x,int y){auto p=pixels[size_t(y*scale)*1920*scale+x*scale];return int(p&255)+int((p>>8)&255)+int((p>>16)&255);}
static void dump(const std::string& path){
 FILE* f=fopen(path.c_str(),"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",1920*scale,1080*scale);
 for(auto p:pixels){unsigned char rgb[3]={static_cast<unsigned char>(p),static_cast<unsigned char>(p>>8),static_cast<unsigned char>(p>>16)};assert(fwrite(rgb,1,3,f)==3);}fclose(f);
}
int main(int argc,char** argv){
 assert(argc==2);
 for(scale=1;scale<=2;++scale){
  pixels.assign(size_t(1920*scale)*1080*scale,ps5video::Rgb(23,29,40));
  using namespace fe;using namespace fe::style;
  assert(ControllerHintWidth(icon::R3,30)==36);
  assert(ControllerHintWidth(icon::DpadLeftRight,30)==36);
  assert(ControllerHintWidth(icon::DpadUpDown,30)==36);
  assert(ControllerHintWidth(icon::Cross,30)==36);
  assert(ControllerHintWidth(icon::L3,30)==36);
  assert(ControllerHintWidth(icon::L1,30)==ControllerHintWidth(icon::R1,30));
  assert(ControllerHintWidth(icon::L2,30)==ControllerHintWidth(icon::R2,30));
  assert(ControllerHintWidth(icon::Touchpad,30)>ControllerHintWidth(icon::Cross,30));
  assert(TextWidthPt(icon::Square,30)==ControllerHintWidth(icon::Square,30));
  assert(ControllerHintWidth(icon::GitHub,30)==TextWidthPt(icon::GitHub,30)); // unrelated symbols unchanged
  // Both directional icons share the same box; only which arms are bright differs.
  DrawControllerHint(icon::DpadLeftRight,100,100,50);
  DrawControllerHint(icon::DpadUpDown,300,100,50);
  assert(light(104,100)>light(130,73)+150);
  assert(light(330,73)>light(304,100)+150);
  assert(pixels[size_t(100*scale)*1920*scale+130*scale]==ps5video::Rgb(23,29,40)); // open center
  // Safe clipping at every surface edge.
  DrawControllerHint(icon::R3,-10,5,30);DrawControllerHint(icon::DpadLeftRight,1910,1075,30);
  const char* all[]={icon::Cross,icon::Circle,icon::Triangle,icon::Square,icon::L1,icon::R1,icon::L2,icon::R2,icon::L3,icon::R3,icon::Options,icon::Touchpad};
  const char* labels[]={"Cruz","Círculo","Triángulo","Cuadrado","L1","R1","L2","R2","L3","R3","OPTIONS","Touchpad"};
  for(int i=0;i<12;++i)
  {
      const int col=i%6,row=i/6;const int left=160+col*270,mid=220+row*230;
      DrawControllerHint(all[i],float(left),float(mid),66);
      TextMid(left+42,mid+85,labels[i],20,Hi(),Weight::Medium,Align::Center);
      DrawControllerHint(all[i],-15,1070,30); // all new designs clip safely
  }
  // Real inline text uses the same drawing and advances as the bottom hints.
  const std::string inline_text=std::string(icon::Square)+" Buscar   "+icon::Triangle+" Ajustes   "+icon::Touchpad+" Menú";
  TextMid(960,650,inline_text.c_str(),24,Hi(),Weight::Medium,Align::Center);
  // Brand-colour pixels must be present; the former monochrome font cannot satisfy this.
  const uint32_t colors[]={ps5video::Rgb(145,198,245),ps5video::Rgb(241,126,115),ps5video::Rgb(105,213,198),ps5video::Rgb(231,151,201)};
  for(uint32_t c:colors) { size_t count=0; for(uint32_t p:pixels) count+=p==c; assert(count>20); }
  // Four face symbols have the same horizontal centre within one surface pixel.
  for(int i=0;i<4;++i)
  {
      int lo=99999,hi=-1;
      for(int yy=170*scale;yy<270*scale;++yy)
          for(int xx=(160+i*270)*scale;xx<(240+i*270)*scale;++xx)
              if(pixels[size_t(yy)*1920*scale+xx]==colors[i]) { lo=std::min(lo,xx);hi=std::max(hi,xx); }
      const double center=(160+i*270+ControllerHintWidth(all[i],66)/2.0)*scale;
      assert(hi>=lo && std::abs((lo+hi+1)/2.0-center)<=1.5);
  }


  Hints({{{icon::R3},"Caja 3D"},{{icon::DpadLeftRight},"Explorar"},{{icon::DpadUpDown},"Letra"}},750);
  Hints({{{icon::Cross},"Jugar"},{{icon::R3},"Caja 3D"},{{icon::DpadLeftRight},"Explorar"},{{icon::DpadUpDown},"Letra"},{{icon::L1,icon::R1},"Pestañas"},{{icon::L2,icon::R2},"Saltar letra"},{{icon::Triangle},"Ajustes"},{{icon::Options},"Salir"}},850);
  dump(std::string(argv[1])+"/navigation-"+std::to_string(scale)+"x.ppm");
  // A bounded-area hint must rasterize identically to the main-screen hint: same size, font and spacing.
  const std::vector<Hint> same={{{icon::DpadLeftRight},"Girar"},{{icon::L3},"Rotar"},{{icon::L1,icon::R1},"Juego"}};
  std::fill(pixels.begin(),pixels.end(),ps5video::Rgb(23,29,40));
  Hints(same,500); const auto full=pixels;
  std::fill(pixels.begin(),pixels.end(),ps5video::Rgb(23,29,40));
  HintsInArea(same,485,950,500); assert(pixels==full); // both areas have the same center
  // The 3D case is centered at x=660: the whole group must move 300 points left from x=960.
  std::fill(pixels.begin(),pixels.end(),ps5video::Rgb(23,29,40));
  HintsInArea(same,660-475,950,500);
  for(int yy=450*scale;yy<550*scale;++yy)
      for(int xx=0;xx<1920*scale;++xx)
      {
          const int from=xx+300*scale;
          const auto expected=from<1920*scale?full[size_t(yy)*1920*scale+from]:ps5video::Rgb(23,29,40);
          assert(pixels[size_t(yy)*1920*scale+xx]==expected);
      }

  // Offscreen cover text must not redirect control drawings into the UI surface.
  const auto before=pixels;
  std::vector<uint32_t> canvas(300*100,ps5video::Rgb(0,0,0));
  TextOn({canvas.data(),300,100,300},0,0,inline_text.c_str(),20,Hi());
  assert(pixels==before);
  assert(std::any_of(canvas.begin(),canvas.end(),[](uint32_t p){return p!=ps5video::Rgb(0,0,0);}));
  // The GPU overlays use premultiplied alpha; antialiasing may not introduce bright fringes.
  gpu=true; ClearSurface();
  for(int i=0;i<12;++i) DrawControllerHint(all[i],100.0f+i*130,500,30);
  bool antialiased=false;
  for(uint32_t p:pixels)
  {
      const uint32_t a=p>>24;
      assert((p&255)<=a && ((p>>8)&255)<=a && ((p>>16)&255)<=a);
      antialiased=antialiased || (a>0 && a<255);
  }
  assert(antialiased); gpu=false;

  printf("Complete controller icons: %dx%d PASS\n",1920*scale,1080*scale);
 }
}

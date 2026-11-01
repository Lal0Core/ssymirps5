// Case-panel layout, clipping, scrolling and localization regression. SPDX-License-Identifier: MIT
#include "fe_casepanel.h"
#include "ProsperoVideo.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstring>
static int scale=1,language=0;
static std::vector<uint32_t> pixels;
namespace ps5video {
uint32_t* Surface(){return pixels.data();}int PixelScale(){return scale;}
int SurfaceWidth(){return 1920*scale;}int SurfaceHeight(){return 1080*scale;}bool GpuActive(){return false;}
}
namespace fe {
const char* T(const char* s){
 const char* en[]={"Play","Back side","Front side","Back","Product code","Game","Release date","Discs","Region","Time played","Last played","File"};
 const char* es[]={"Jugar","Reverso","Portada","Volver","Código","Juego","Fecha","Discos","Región","Tiempo jugado","Última vez","Archivo"};
 const char* pt[]={"Jogar","Verso","Frente","Voltar","Código","Jogo","Data","Discos","Região","Tempo jogado","Última vez","Arquivo"};
 const char* fr[]={"Jouer","Verso","Recto","Retour","Code","Jeu","Date","Disques","Région","Temps de jeu","Dernière partie","Fichier"};
 const char* de[]={"Spielen","Rückseite","Vorderseite","Zurück","Produktcode","Spiel","Erscheinungsdatum","Discs","Region","Gesamte Spielzeit","Zuletzt gespielt","Dateiname"};
 const char* const* table=language==0?en:language==1?es:language==2?pt:language==3?fr:de;
 for(int i=0;i<12;++i)if(strcmp(s,en[i])==0)return table[i];return s;
}}
static bool overlap(const fe::CaseActionBox& a,const fe::CaseActionBox& b){return a.x<b.x+b.w && b.x<a.x+a.w && a.y<b.y+b.h && b.y<a.y+a.h;}
static void save(const std::string& p){FILE* f=fopen(p.c_str(),"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",1920*scale,1080*scale);for(auto c:pixels){unsigned char rgb[]={uint8_t(c),uint8_t(c>>8),uint8_t(c>>16)};fwrite(rgb,1,3,f);}fclose(f);}
int main(int argc,char** argv){assert(argc==2);using namespace fe;
 assert(MarqueeOffset(100,200,10)==0);assert(MarqueeOffset(1000,200,2)==0);
 assert(std::abs(MarqueeOffset(1000,200,3)-34)<.001);
 for(double t=0;t<200;t+=.13){double x=MarqueeOffset(1000,200,t);assert(x>=-.001 && x<=800.001);}
 for(int width:{260,400,670}){auto b=CaseActionLayout({500,550,600},width);assert(b.size()==3);for(size_t i=0;i<b.size();++i){assert(b[i].x>=0&&b[i].x+b[i].w<=width&&b[i].y+b[i].h<=134);for(size_t j=0;j<i;++j)assert(!overlap(b[i],b[j]));}}
 const std::string long_title="Daytona USA - Championship Circuit Edition - Complete International Collection";
 const std::vector<std::pair<std::string,std::string>> rows={{"Product code","MK-81213"},{"Game",long_title},{"Release date","01/11/1996"},{"Discs","1"},{"Region","Norteamérica / Europa / Japón"},{"Time played","menos de 1 min"},{"Last played","hace 2 días"},{"File",long_title+" (USA) (Track 01) [Edition complète].cue"}};
 for(scale=1;scale<=2;++scale){
  const auto bg=ps5video::Rgb(23,29,40);pixels.assign(size_t(1920*scale)*1080*scale,bg);
  // A moving line may not write even one pixel outside its rectangular viewport.
  TextMarquee(100,100,170,40,long_title,30,style::Hi(),6);
  bool ink=false;for(int y=0;y<1080*scale;++y)for(int x=0;x<1920*scale;++x)if(pixels[size_t(y)*1920*scale+x]!=bg){ink=true;assert(x>=100*scale&&x<270*scale&&y>=100*scale&&y<140*scale);}assert(ink);
  for(language=0;language<5;++language)for(double time:{0.0,3.0,10.0}){
   std::fill(pixels.begin(),pixels.end(),bg);
   auto buttons=DrawCasePanel(1150,670,long_title,"1996",rows,T("Back side"),"",time);
   style::HintsInArea({{{icon::DpadLeftRight},T("Turn")},{{icon::L3},T("Rotate")},{{icon::L1,icon::R1},T("Game")}},660-475,950,1020);
   assert(buttons.size()==3);for(size_t i=0;i<buttons.size();++i){assert(buttons[i].x>=1150&&buttons[i].x+buttons[i].w<=1820&&buttons[i].y>=850&&buttons[i].y+buttons[i].h<=984);for(size_t j=0;j<i;++j)assert(!overlap(buttons[i],buttons[j]));}
   if(language==1&&time==3)save(std::string(argv[1])+"/case-panel-"+std::to_string(scale)+"x.ppm");
  }
  printf("Case panel %dx%d: five languages, long text, no overlap and strict clipping PASS\n",1920*scale,1080*scale);
 }
 assert(style::Accent()==ps5video::Rgb(203,181,139));puts("Champagne palette PASS");
}

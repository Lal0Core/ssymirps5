// Fixed case information panel and non-overlapping actions. SPDX-License-Identifier: MIT
#pragma once
#include "fe_style.h"
#include "fe_i18n.h"
#include <algorithm>
#include <utility>
namespace fe {
struct CaseActionBox { int x,y,w,h; };
inline std::vector<CaseActionBox> CaseActionLayout(const std::vector<int>& widths,int width)
{
    std::vector<CaseActionBox> out; int x=0,y=0;
    const int cap=std::max(1,(width-14)/2);
    for(int requested:widths) {
        const int w=std::min(requested,cap);
        if(x && x+w>width) {x=0;y+=74;}
        out.push_back({x,y,w,60}); x+=w+14;
    }
    return out;
}
inline std::vector<CaseActionBox> DrawCasePanel(int x,int width,const std::string& title,const std::string& subtitle,
    const std::vector<std::pair<std::string,std::string>>& rows,const char* reverse_label,
    const std::string& note,double elapsed)
{
    Text(x,150,"SEGA SATURN",16,style::Accent(),Weight::SemiBold);
    TextMarquee(x,186,width,64,title,48,style::Hi(),elapsed,Weight::SemiBold);
    TextMarquee(x,264,width,30,subtitle,20,style::Lo(),elapsed);
    int label_width=180;
    for(const auto& row:rows) label_width=std::max(label_width,TextWidthPt(T(row.first.c_str()),19,Weight::Medium)+24);
    label_width=std::min(label_width,width*48/100);
    const int row_height=std::min(60,500/std::max(1,int(rows.size())));
    int y=320;
    for(const auto& row:rows) {
        style::Line(float(x),float(x+width),float(y),style::White(),30);
        const std::string label=FitTextPt(T(row.first.c_str()),19,label_width-16,Weight::Medium);
        TextMid(x,y+row_height/2,label.c_str(),19,style::Lo(),Weight::Medium);
        TextMarquee(x+label_width,y+row_height/2-11,width-label_width,30,
            row.second.empty()?"-":row.second,22,style::Hi(),elapsed,Weight::SemiBold);
        y+=row_height;
    }
    style::Line(float(x),float(x+width),float(y),style::White(),30);
    const char* icons[]={icon::Cross,icon::R3,icon::Circle};
    const char* labels[]={T("Play"),reverse_label,T("Back")};
    std::vector<int> widths;
    for(int i=0;i<3;++i) widths.push_back(32+style::ControllerHintWidth(icons[i],26)+12+TextWidthPt(labels[i],24,Weight::SemiBold));
    auto layout=CaseActionLayout(widths,width);
    for(int i=0;i<3;++i) {
        auto& b=layout[size_t(i)]; b.x+=x;b.y+=850;
        style::RoundRect(float(b.x),float(b.y),float(b.w),60,30,i==0?style::Accent():style::Surface2());
        const int iw=style::ControllerHintWidth(icons[i],26);
        const std::string label=FitTextPt(labels[i],24,std::max(1,b.w-32-iw-12),Weight::SemiBold);
        style::DrawControllerHint(icons[i],float(b.x+16),float(b.y+30),26);
        TextMid(b.x+16+iw+12,b.y+30,label.c_str(),24,i==0?style::AccentText():style::Hi(),Weight::SemiBold);
    }
    TextMarquee(x,1008,width,28,note,16,style::Lo(),elapsed);
    return layout;
}
}

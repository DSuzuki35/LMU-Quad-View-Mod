#pragma once
#include <charconv>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include "openxr/openxr.h"

namespace lmuqv {
struct FocusBox { double percent=100;bool valid=true; };
inline FocusBox parse_focus_box(const char* text) {
    FocusBox box;
    if(!text||!*text)return box;
    const char* end=text+std::strlen(text);
    auto parsed=std::from_chars(text,end,box.percent,std::chars_format::fixed);
    if(parsed.ec!=std::errc{}||parsed.ptr!=end||!std::isfinite(box.percent)||box.percent<25||box.percent>200){
        box.percent=100;box.valid=false;
    }
    return box;
}
struct FocusBoxSize { FocusBox width,height; };
inline FocusBoxSize parse_focus_box_size(const char* width,const char* height,const char* legacy) {
    return {parse_focus_box(width&&*width?width:legacy),parse_focus_box(height&&*height?height:legacy)};
}
inline bool scale_focus_fov(XrFovf& fov,double widthPercent,double heightPercent) {
    constexpr double halfPi=1.57079632679489661923;
    auto valid=[&](const XrFovf& value){
        for(float angle:{value.angleLeft,value.angleRight,value.angleUp,value.angleDown})
            if(!std::isfinite(angle)||angle<=-halfPi||angle>=halfPi)return false;
        return value.angleLeft<value.angleRight&&value.angleDown<value.angleUp;
    };
    auto validPercent=[](double percent){return std::isfinite(percent)&&percent>=25&&percent<=200;};
    if(!validPercent(widthPercent)||!validPercent(heightPercent)||!valid(fov))return false;
    if(widthPercent==100&&heightPercent==100)return true; // No tangent round trip: preserve every input bit.
    const double left=std::tan(double(fov.angleLeft)),right=std::tan(double(fov.angleRight));
    const double up=std::tan(double(fov.angleUp)),down=std::tan(double(fov.angleDown));
    const double centerX=(left+right)/2,centerY=(down+up)/2;
    const double halfWidth=(right-left)*widthPercent/200,halfHeight=(up-down)*heightPercent/200;
    const XrFovf scaled{widthPercent==100?fov.angleLeft:float(std::atan(centerX-halfWidth)),
                        widthPercent==100?fov.angleRight:float(std::atan(centerX+halfWidth)),
                        heightPercent==100?fov.angleUp:float(std::atan(centerY+halfHeight)),
                        heightPercent==100?fov.angleDown:float(std::atan(centerY-halfHeight))};
    if(!valid(scaled))return false; // Includes float rounding to a singular or degenerate FOV.
    fov=scaled;
    return true;
}
inline bool scale_focus_fov(XrFovf& fov,double percent) { return scale_focus_fov(fov,percent,percent); }
}

#pragma once
#include "focus_box.hpp"
#include <algorithm>
#include <array>
#include <cstdint>

namespace lmuqv {
struct CropPercent { double percent=100;bool valid=true; };
inline CropPercent parse_crop_percent(const char* text){
    auto parsed=parse_focus_box(text);
    return parsed.valid&&parsed.percent<=100?CropPercent{parsed.percent,true}:CropPercent{100,false};
}
inline uint32_t crop_dimension(uint32_t full,double percent){
    if(!full||!std::isfinite(percent)||percent<25||percent>100)return 0;
    return static_cast<uint32_t>(std::ceil(double(full)*percent/100));
}
// Fit the entire focus image inside the reduced field. Near the crop edge its
// center stops following gaze; shrink its angular span only when it cannot fit.
// Rendering and UI share this geometry. Peripheral output is subsequently
// embedded in its original FOV for compositors which cache the full eye FOV.
inline bool apply_view_crop(XrView* views,double width,double height){
    if(!views||!std::isfinite(width)||!std::isfinite(height)||width<25||width>100||height<25||height>100)return false;
    if(width==100&&height==100)return true;
    std::array<XrView,4> result;
    std::copy_n(views,4,result.begin());
    auto fit=[](float& low,float& high,float outerLow,float outerHigh){
        const double lo=std::tan(double(low)),hi=std::tan(double(high));
        const double boundLo=std::tan(double(outerLow)),boundHi=std::tan(double(outerHigh));
        const double span=hi-lo;
        if(span>=boundHi-boundLo){low=outerLow;high=outerHigh;return;}
        if(lo<boundLo){low=outerLow;high=float(std::atan(boundLo+span));}
        else if(hi>boundHi){high=outerHigh;low=float(std::atan(boundHi-span));}
        low=(std::max)(low,outerLow);high=(std::min)(high,outerHigh);
    };
    for(unsigned eye=0;eye<2;++eye){
        auto& outer=result[eye].fov;auto& focus=result[eye+2].fov;
        if(!scale_focus_fov(outer,width,height)||!scale_focus_fov(focus,100,100))return false;
        fit(focus.angleLeft,focus.angleRight,outer.angleLeft,outer.angleRight);
        fit(focus.angleDown,focus.angleUp,outer.angleDown,outer.angleUp);
        if(!scale_focus_fov(focus,100,100))return false;
    }
    std::copy(result.begin(),result.end(),views);return true;
}
// Normalized image bounds, top-left origin. Work in tangent space so asymmetric
// eyes and off-center projections retain exactly the same angular scale.
inline bool crop_bounds(XrFovf full,XrFovf crop,std::array<float,4>& bounds){
    if(!scale_focus_fov(full,100,100)||!scale_focus_fov(crop,100,100))return false;
    const double l=std::tan(double(full.angleLeft)),r=std::tan(double(full.angleRight));
    const double u=std::tan(double(full.angleUp)),d=std::tan(double(full.angleDown));
    const std::array<double,4> values{{(std::tan(double(crop.angleLeft))-l)/(r-l),
        (u-std::tan(double(crop.angleUp)))/(u-d),(std::tan(double(crop.angleRight))-l)/(r-l),
        (u-std::tan(double(crop.angleDown)))/(u-d)}};
    for(unsigned i=0;i<4;++i){
        if(!std::isfinite(values[i])||values[i]<-1e-6||values[i]>1+1e-6)return false;
        bounds[i]=float((std::clamp)(values[i],0.0,1.0));
    }
    return bounds[2]>bounds[0]&&bounds[3]>bounds[1];
}
}

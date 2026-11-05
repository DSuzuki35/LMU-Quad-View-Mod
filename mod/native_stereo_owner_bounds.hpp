#pragma once
#include "native_stereo_classify_capture.hpp"
#include <algorithm>

namespace lmuqv {
struct NativeStereoOwnerBounds {
    bool available=false;
    std::array<NativeStereoClassifyFloat3,8> corners{};
    // Must be the center used by the native bounds helper, not an object origin.
    NativeStereoClassifyFloat3 center{};
    float radius=0;
};
// The native 0x3b7270 helper reduces eight packed corners to an AABB; 0x41c6f0
// averages its extrema. Preserve MINSS/MAXSS operand order, including zero ties.
inline NativeStereoOwnerBounds prepare_native_stereo_owner_bounds(
    const std::array<NativeStereoClassifyFloat3,8>& corners,float radius) noexcept {
    NativeStereoFpGuard preserve;NativeStereoOwnerBounds out;
    if((_mm_getcsr()&0xffc0u)!=0x1f80u||std::fegetround()!=FE_TONEAREST||!std::isfinite(radius)||radius<0)return out;
    constexpr bool currentFirst[8][3]={{false,false,false},{true,false,true},{false,false,false},{false,false,true},
        {false,false,false},{true,true,true},{false,false,false},{false,true,true}};
    std::array<float,3> low{1e12f,1e12f,1e12f},high{-1e12f,-1e12f,-1e12f};
    for(unsigned i=0;i<8;++i){const float point[]{corners[i].x,corners[i].y,corners[i].z};
        for(unsigned axis=0;axis<3;++axis){
            if(!std::isfinite(point[axis]))return out;
            const auto p=_mm_set_ss(point[axis]),l=_mm_set_ss(low[axis]),h=_mm_set_ss(high[axis]);
            low[axis]=_mm_cvtss_f32(currentFirst[i][axis]?_mm_min_ss(l,p):_mm_min_ss(p,l));
            high[axis]=_mm_cvtss_f32(currentFirst[i][axis]?_mm_max_ss(h,p):_mm_max_ss(p,h));
        }
    }
    std::array<float,3> center;
    for(unsigned axis=0;axis<3;++axis){
        const auto a=_mm_set_ss(axis?high[axis]:low[axis]),b=_mm_set_ss(axis?low[axis]:high[axis]);
        center[axis]=_mm_cvtss_f32(_mm_mul_ss(_mm_add_ss(a,b),_mm_set_ss(.5f)));
        if(!std::isfinite(center[axis]))return out;
    }
    out.available=true;out.corners=corners;out.center={center[0],center[1],center[2]};out.radius=radius;return out;
}
struct NativeStereoOwnerBoundsView {
    bool available=false;
    NativeStereoClassifyFloat3 position{};
    float range=0;
    std::array<NativeStereoClassifyPlane,6> planes{};
};
enum class NativeStereoOwnerBoundsReason {
    None, MissingInputs, UnsupportedFlags, UnsupportedFp, NonFiniteInput,
    InvalidDomain, NonFiniteArithmetic
};
struct NativeStereoOwnerBoundsResult {
    bool available=false,admitted=false;
    NativeStereoOwnerBoundsReason reason=NativeStereoOwnerBoundsReason::MissingInputs;
    uint32_t flags=0;
    // Ordered writes from distance, sphere and corner gates, including writes
    // later undone by another gate. No native memory is changed.
    std::array<uint32_t,3> flagWrites{};
    unsigned writeCount=0;
};

// Regular null-source path of 0x41c6f0, 0x41c500 and 0x418e40. The center
// supplied by the caller must already represent the helper's bounds center.
// World refresh and transformed-plane (0x200000) paths remain unsupported.
// No callbacks/native objects, heap allocation, pointer reads or visible work.
inline NativeStereoOwnerBoundsResult classify_native_stereo_owner_bounds(
    const NativeStereoOwnerBounds& bounds,const NativeStereoOwnerBoundsView& view,uint32_t flags) noexcept {
    using Reason=NativeStereoOwnerBoundsReason;
    const auto refuse=[](Reason reason){NativeStereoOwnerBoundsResult out;out.reason=reason;return out;};
    if(!bounds.available||!view.available)return refuse(Reason::MissingInputs);
    if(!(flags&0x80000u)||(flags&0x400000u)||(flags&0x200000u))return refuse(Reason::UnsupportedFlags);
    if((_mm_getcsr()&0xffc0u)!=0x1f80u||std::fegetround()!=FE_TONEAREST)return refuse(Reason::UnsupportedFp);
    NativeStereoFpGuard preserve;
    const auto finite=[](const NativeStereoClassifyFloat3& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    if(!finite(bounds.center)||!finite(view.position)||!std::isfinite(bounds.radius)||!std::isfinite(view.range))return refuse(Reason::NonFiniteInput);
    for(const auto& p:bounds.corners)if(!finite(p))return refuse(Reason::NonFiniteInput);
    for(const auto& p:view.planes)if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||!std::isfinite(p.w))return refuse(Reason::NonFiniteInput);
    if(bounds.radius<0||view.range<0)return refuse(Reason::InvalidDomain);
    NativeStereoOwnerBoundsResult out;out.reason=Reason::None;out.flags=flags;
    const auto write=[&](uint32_t value){out.flags=value;out.flagWrites[out.writeCount++]=value;};
    const auto finish=[&](bool admitted){out.available=true;out.admitted=admitted;return out;};
    const auto clear=[&](){write(out.flags&~0x10000u);return finish(false);};
    const float radius=view.range+bounds.radius;
    const float radius2=radius*radius;
    const float dx=bounds.center.x-view.position.x,dy=bounds.center.y-view.position.y,dz=bounds.center.z-view.position.z;
    const float yy=dy*dy,xx=dx*dx,zz=dz*dz;
    const float yx=yy+xx,distance2=yx+zz;
    if(!std::isfinite(radius)||!std::isfinite(radius2)||!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(dz)||
       !std::isfinite(yy)||!std::isfinite(xx)||!std::isfinite(zz)||!std::isfinite(yx)||!std::isfinite(distance2))return refuse(Reason::NonFiniteArithmetic);
    if(distance2>radius2)return clear();
    const auto dot=[](const NativeStereoClassifyFloat3& p,const NativeStereoClassifyPlane& plane,float& value){
        const float y=p.y*plane.y,x=p.x*plane.x,z=p.z*plane.z;
        const float yx=y+x,zw=z+plane.w;value=yx+zw;
        return std::isfinite(y)&&std::isfinite(x)&&std::isfinite(z)&&std::isfinite(yx)&&std::isfinite(zw)&&std::isfinite(value);
    };
    write(out.flags|0x10000u);
    for(const auto& plane:view.planes){
        float value=0;if(!dot(bounds.center,plane,value))return refuse(Reason::NonFiniteArithmetic);
        if(value>bounds.radius)return clear();
        if(value>-bounds.radius){write(out.flags|0x80000000u);break;}
    }
    // Native corner rejection still runs after an early sphere intersection.
    // A point exactly on a plane is inside; all eight must be strictly outside.
    for(const auto& plane:view.planes){
        bool outside=true;
        for(const auto& point:bounds.corners){
            float value=0;if(!dot(point,plane,value))return refuse(Reason::NonFiniteArithmetic);
            if(!(value>0)){outside=false;break;}
        }
        if(outside)return clear();
    }
    // Already set by the sphere helper. The final native OR is an equal write;
    // omit it to keep at most three state-changing write sites in this result.
    return finish(true);
}
}

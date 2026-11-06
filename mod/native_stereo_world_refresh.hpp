#pragma once
#include "native_stereo_view_math.hpp"
#include "native_stereo_owner_bounds.hpp"

namespace lmuqv {
// Owned inputs for the ordinary 0x41c0a0 path. Attachments refer to an owned
// index, or to the explicit external snapshot; none of these fields is a native
// object on which an engine method may be called.
struct NativeStereoOwnerWorld {
    bool available=false;
    uint32_t localMetadata=0,sceneFlags=0;
    Matrix local{},world{};
    NativeStereoClassifyFloat3 scale{};
    std::array<NativeStereoClassifyFloat3,8> localCorners{},worldCorners{};
    NativeStereoClassifyFloat3 center{};
    float radius=0;
    int32_t attachmentIndex=-1;
    bool externalAttachmentAvailable=false;
    Matrix attachmentWorld{};
    std::array<NativeStereoClassifyFloat3,8> attachmentCorners{};
};
struct NativeStereoWorldRefreshResult {
    bool complete=false,skipped=false;
    const char* reason="missing world refresh inputs";
    uint32_t flags=0;
    NativeStereoOwnerWorld world;
};

inline bool native_stereo_world_point(const NativeStereoClassifyFloat3& p,
    const Matrix& m,NativeStereoClassifyFloat3& output) noexcept {
    const float input[]{p.x,p.y,p.z};
    for(float f:input)if(!std::isfinite(f))return false;
    float result[3];
    for(unsigned c=0;c<3;++c){
        const float x=p.x*m[c],y=p.y*m[4+c],z=p.z*m[8+c];
        const float xy=y+x,zt=z+m[12+c],value=xy+zt;
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||
           !std::isfinite(xy)||!std::isfinite(zt)||!std::isfinite(value))return false;
        result[c]=value;
    }
    output={result[0],result[1],result[2]};return true;
}

// 0x41a2c0: native AABB center followed by the furthest of all eight corner
// distances. An object origin or a diagonal-length estimate is not equivalent.
inline NativeStereoOwnerBounds prepare_native_stereo_world_bounds(
    const std::array<NativeStereoClassifyFloat3,8>& corners) noexcept {
    NativeStereoFpGuard preserve;
    auto result=prepare_native_stereo_owner_bounds(corners,0);
    if(!result.available)return result;
    float maximum=0;
    for(const auto& corner:corners){
        const float x=result.center.x-corner.x,y=result.center.y-corner.y,z=result.center.z-corner.z;
        const float xx=x*x,yy=y*y,zz=z*z,yx=yy+xx,distance=yx+zz;
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(xx)||
           !std::isfinite(yy)||!std::isfinite(zz)||!std::isfinite(yx)||!std::isfinite(distance))return {};
        maximum=_mm_cvtss_f32(_mm_max_ss(_mm_set_ss(distance),_mm_set_ss(maximum)));
    }
    result.radius=_mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(maximum)));
    if(!std::isfinite(result.radius))return {};
    return result;
}

// One numeric refresh, with traversal supplied by the owner planner. Original
// 0x41c0a0 skips disabled nodes and their descendants before any mutation.
// Camera-facing and active extra-matrix effects retain native fallback.
inline NativeStereoWorldRefreshResult refresh_native_stereo_world(
    const NativeStereoOwnerWorld& input,uint32_t flags,bool attachmentPresent,
    const NativeStereoOwnerWorld* attachment=nullptr,bool force=false) noexcept {
    NativeStereoFpGuard preserve;
    const auto refuse=[](const char* reason){NativeStereoWorldRefreshResult out;out.reason=reason;return out;};
    if((_mm_getcsr()&0xffc0u)!=0x1f80u||std::fegetround()!=FE_TONEAREST)return refuse("unsupported world FP controls");
    if(!force&&(flags&0x20000u)){
        NativeStereoWorldRefreshResult out;out.complete=out.skipped=true;out.flags=flags;
        out.world=input;out.reason="native disabled world subtree";return out;
    }
    if(!input.available)return refuse("missing world refresh inputs");
    if(flags&0x21000000u)return refuse("native camera-facing world refresh required");
    if((flags&0x200000u)&&(input.sceneFlags&0xcu))return refuse("native extra world matrix required");
    auto value=input;
    for(float f:value.local)if(!std::isfinite(f))return refuse("non-finite local world matrix");
    if((value.localMetadata&6u)==6u){
        // 0x589d50 transposes the matrix, multiplies by the scale diagonal with
        // all four terms (including zeros), then transposes it back. Retain that
        // operation order instead of simply scaling its three rows.
        Matrix transpose{},scaled{};
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)transpose[r*4+c]=value.local[c*4+r];
        const Matrix diagonal{value.scale.x,0,0,0,0,value.scale.y,0,0,0,0,value.scale.z,0,0,0,0,1};
        if(!native_stereo_view_product(transpose,diagonal,scaled))return refuse("invalid native local scale refresh");
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)value.local[r*4+c]=scaled[c*4+r];
        value.localMetadata&=~5u;
    }
    const Matrix* parentWorld=nullptr;
    const std::array<NativeStereoClassifyFloat3,8>* parentCorners=nullptr;
    if(attachmentPresent){
        if(attachment&&attachment->available){parentWorld=&attachment->world;parentCorners=&attachment->worldCorners;}
        else if(value.externalAttachmentAvailable){parentWorld=&value.attachmentWorld;parentCorners=&value.attachmentCorners;}
        else return refuse("missing native attachment world snapshot");
        if(!native_stereo_view_product(value.local,*parentWorld,value.world))return refuse("invalid attached world product");
    }else value.world=value.local;
    for(unsigned i=0;i<8;++i)
        if(!native_stereo_world_point(value.localCorners[i],value.world,value.worldCorners[i]))return refuse("invalid transformed world corner");
    const auto bounds=prepare_native_stereo_world_bounds(parentCorners?*parentCorners:value.worldCorners);
    if(!bounds.available)return refuse("invalid refreshed world bounds");
    value.center=bounds.center;value.radius=bounds.radius;
    NativeStereoWorldRefreshResult out;out.complete=true;out.flags=(flags&~0x400000u)|0x40800000u;
    out.world=value;out.reason="owned native-order ordinary world refresh";return out;
}
}

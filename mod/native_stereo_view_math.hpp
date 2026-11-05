#pragma once
#include "quad_math.hpp"
#include "native_stereo_classify_capture.hpp"

namespace lmuqv {
// Operation order from the captured 0x557870 derived-view path. This is value
// math, not a call into an engine TransformRecord or its opaque tail fields.
inline bool native_stereo_view_product(const Matrix& a,const Matrix& b,Matrix& result) noexcept {
    result={};NativeStereoFpGuard preserve;
    if((_mm_getcsr()&0xffc0u)!=0x1f80u||std::fegetround()!=FE_TONEAREST)return false;
    for(float f:a)if(!std::isfinite(f))return false;
    for(float f:b)if(!std::isfinite(f))return false;
    Matrix output;
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col){
        const float p0=a[row*4]*b[col],p1=a[row*4+1]*b[4+col];
        const float p2=a[row*4+2]*b[8+col],p3=a[row*4+3]*b[12+col];
        const float high=p3+p2,low=p1+p0,value=high+low;
        if(!std::isfinite(p0)||!std::isfinite(p1)||!std::isfinite(p2)||!std::isfinite(p3)||
           !std::isfinite(high)||!std::isfinite(low)||!std::isfinite(value))return false;
        output[row*4+col]=value;
    }
    result=output;return true;
}
struct NativeStereoViewGeometry {
    bool complete=false;
    const char* reason="invalid view geometry";
    Matrix derived{};
    std::array<NativeStereoClassifyPlane,6> planes{};
};
inline NativeStereoViewGeometry prepare_native_stereo_view_geometry(const Matrix& current,const Matrix& projection) noexcept {
    NativeStereoFpGuard preserve;NativeStereoViewGeometry out;
    if(!native_stereo_view_product(current,projection,out.derived))return out;
    // Native order: left, right, top, bottom, z, w-z. Negative normalization
    // makes positive signed distance mean outside, including reversed depth.
    for(unsigned plane=0;plane<6;++plane){
        std::array<float,4> raw{};
        for(unsigned row=0;row<4;++row){
            const auto i=row*4;
            switch(plane){
            case 0:raw[row]=out.derived[i+3]+out.derived[i];break;
            case 1:raw[row]=out.derived[i+3]-out.derived[i];break;
            case 2:raw[row]=out.derived[i+3]-out.derived[i+1];break;
            case 3:raw[row]=out.derived[i+3]+out.derived[i+1];break;
            case 4:raw[row]=out.derived[i+2];break;
            default:raw[row]=out.derived[i+3]-out.derived[i+2];break;
            }
        }
        const float xx=raw[0]*raw[0],yy=raw[1]*raw[1],zz=raw[2]*raw[2];
        const float xy=yy+xx,length2=xy+zz,length=std::sqrt(length2),scale=-1.f/length;
        if(!std::isfinite(xx)||!std::isfinite(yy)||!std::isfinite(zz)||!std::isfinite(xy)||
           !(length2>0)||!std::isfinite(length2)||!std::isfinite(length)||!std::isfinite(scale)){
            NativeStereoViewGeometry failed;failed.reason="degenerate or non-finite frustum plane";return failed;}
        NativeStereoClassifyPlane value{raw[0]*scale,raw[1]*scale,raw[2]*scale,raw[3]*scale};
        if(!std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.z)||!std::isfinite(value.w)){
            NativeStereoViewGeometry failed;failed.reason="non-finite normalized frustum plane";return failed;}
        out.planes[plane]=value;
    }
    out.complete=true;out.reason="owned native-order derived matrix and six planes";return out;
}
}

#pragma once
#include "native_stereo_view_math.hpp"
#include <xmmintrin.h>

namespace lmuqv {
// Native 0xffb6d0 cofactor order, including RCPSS and its single Newton step.
// A rigid transpose or ordinary division gives different binary32 results.
// No native object/method, allocation or captured executable bytes are used.
inline bool native_stereo_view_inverse(const Matrix& input,Matrix& output) noexcept {
    NativeStereoFpGuard preserve;
    if((_mm_getcsr()&0xffc0u)!=0x1f80u||std::fegetround()!=FE_TONEAREST)return false;
    for(float value:input)if(!std::isfinite(value))return false;
    const auto half01=_mm_setr_ps(input[0],input[1],input[4],input[5]);
    const auto half23=_mm_setr_ps(input[8],input[9],input[12],input[13]);
    const auto halfZW01=_mm_setr_ps(input[2],input[3],input[6],input[7]);
    const auto halfZW23=_mm_setr_ps(input[10],input[11],input[14],input[15]);
    const auto col0=_mm_shuffle_ps(half01,half23,0x88);
    const auto col1Rot=_mm_shuffle_ps(half23,half01,0xdd);
    const auto col2=_mm_shuffle_ps(halfZW01,halfZW23,0x88);
    const auto col3Rot=_mm_shuffle_ps(halfZW23,halfZW01,0xdd);
    const auto col2Rot=_mm_shuffle_ps(col2,col2,0x4e);

    // First paired minors and the first inverse row's cofactors.
    auto minor=_mm_mul_ps(col3Rot,col2);
    minor=_mm_shuffle_ps(minor,minor,0xb1);
    auto rotated=_mm_shuffle_ps(minor,minor,0x4e);
    const auto saved40=_mm_sub_ps(_mm_mul_ps(rotated,col0),_mm_mul_ps(minor,col0));
    auto cofactor0=_mm_sub_ps(_mm_mul_ps(rotated,col1Rot),_mm_mul_ps(minor,col1Rot));

    minor=_mm_mul_ps(col2,col1Rot);minor=_mm_shuffle_ps(minor,minor,0xb1);
    rotated=_mm_shuffle_ps(minor,minor,0x4e);
    const auto saved50=_mm_sub_ps(_mm_mul_ps(rotated,col0),_mm_mul_ps(minor,col0));
    cofactor0=_mm_add_ps(cofactor0,_mm_mul_ps(minor,col3Rot));
    cofactor0=_mm_sub_ps(cofactor0,_mm_mul_ps(rotated,col3Rot));

    const auto col1Half=_mm_shuffle_ps(col1Rot,col1Rot,0x4e);
    minor=_mm_mul_ps(col1Half,col3Rot);minor=_mm_shuffle_ps(minor,minor,0xb1);
    rotated=_mm_shuffle_ps(minor,minor,0x4e);
    cofactor0=_mm_add_ps(cofactor0,_mm_mul_ps(col2Rot,minor));
    cofactor0=_mm_sub_ps(cofactor0,_mm_mul_ps(col2Rot,rotated));
    auto cofactor2=_mm_sub_ps(_mm_mul_ps(rotated,col0),_mm_mul_ps(minor,col0));
    cofactor2=_mm_shuffle_ps(cofactor2,cofactor2,0x4e);

    minor=_mm_mul_ps(col1Rot,col0);minor=_mm_shuffle_ps(minor,minor,0xb1);
    rotated=_mm_shuffle_ps(minor,minor,0x4e);
    auto cofactor3=_mm_mul_ps(col2Rot,minor);
    const auto saved12=_mm_mul_ps(col2Rot,rotated);
    cofactor2=_mm_add_ps(cofactor2,_mm_mul_ps(minor,col3Rot));
    cofactor2=_mm_sub_ps(_mm_mul_ps(rotated,col3Rot),cofactor2);

    auto minor20=_mm_mul_ps(col2Rot,col0);minor20=_mm_shuffle_ps(minor20,minor20,0xb1);
    const auto minor20Rot=_mm_shuffle_ps(minor20,minor20,0x4e);
    const auto saved7=_mm_mul_ps(minor20,col1Rot);
    const auto saved3=_mm_mul_ps(minor20,col3Rot);
    auto minor30=_mm_mul_ps(col3Rot,col0);minor30=_mm_shuffle_ps(minor30,minor30,0xb1);
    const auto minor30Rot=_mm_shuffle_ps(minor30,minor30,0x4e);
    cofactor2=_mm_add_ps(cofactor2,_mm_mul_ps(minor30,col1Rot));
    cofactor2=_mm_sub_ps(cofactor2,_mm_mul_ps(minor30Rot,col1Rot));

    auto cofactor1=_mm_shuffle_ps(saved40,saved40,0x4e);
    cofactor1=_mm_sub_ps(cofactor1,_mm_mul_ps(col2Rot,minor30));
    cofactor1=_mm_add_ps(cofactor1,_mm_mul_ps(col2Rot,minor30Rot));
    cofactor1=_mm_add_ps(cofactor1,saved3);
    cofactor1=_mm_sub_ps(cofactor1,_mm_mul_ps(minor20Rot,col3Rot));
    rotated=_mm_shuffle_ps(saved50,saved50,0x4e);
    cofactor3=_mm_sub_ps(cofactor3,rotated);
    cofactor3=_mm_sub_ps(cofactor3,saved12);
    cofactor3=_mm_sub_ps(cofactor3,saved7);
    cofactor3=_mm_add_ps(cofactor3,_mm_mul_ps(minor20Rot,col1Rot));

    auto determinant=_mm_mul_ps(cofactor0,col0);
    auto sum=_mm_shuffle_ps(determinant,determinant,0x4e);sum=_mm_add_ps(sum,determinant);
    determinant=_mm_shuffle_ps(sum,sum,0xb1);determinant=_mm_add_ss(determinant,sum);
    const float det=_mm_cvtss_f32(determinant);
    if(!std::isfinite(det)||det==0)return false;
    auto reciprocal=_mm_rcp_ss(determinant);
    const auto squared=_mm_mul_ss(reciprocal,reciprocal);
    reciprocal=_mm_add_ss(reciprocal,reciprocal);
    reciprocal=_mm_sub_ss(reciprocal,_mm_mul_ss(determinant,squared));
    reciprocal=_mm_shuffle_ps(reciprocal,reciprocal,0);
    Matrix result;
    _mm_storeu_ps(result.data(),_mm_mul_ps(cofactor0,reciprocal));
    _mm_storeu_ps(result.data()+4,_mm_mul_ps(cofactor1,reciprocal));
    _mm_storeu_ps(result.data()+8,_mm_mul_ps(cofactor2,reciprocal));
    _mm_storeu_ps(result.data()+12,_mm_mul_ps(cofactor3,reciprocal));
    for(float value:result)if(!std::isfinite(value))return false;
    output=result;return true;
}

inline bool native_stereo_bounds_position(const Matrix& current,
    const NativeStereoClassifyFloat3& anchor,NativeStereoClassifyFloat3& position) noexcept {
    NativeStereoFpGuard preserve;Matrix inverse;
    if(!std::isfinite(anchor.x)||!std::isfinite(anchor.y)||!std::isfinite(anchor.z)||
       !native_stereo_view_inverse(current,inverse))return false;
    std::array<float,3> result;
    for(unsigned col=0;col<3;++col){
        const float x=anchor.x*inverse[col],y=anchor.y*inverse[4+col],z=anchor.z*inverse[8+col];
        const float low=col==1?x+y:y+x,high=z+inverse[12+col];
        result[col]=low+high;
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(low)||
           !std::isfinite(high)||!std::isfinite(result[col]))return false;
    }
    position={result[0],result[1],result[2]};return true;
}
}

#include "../native_stereo_view_inverse.hpp"
#include <cassert>
#include <iostream>
#include <random>

using namespace lmuqv;
namespace {
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
std::array<double,16> reference(const Matrix& m){
    // Independent pivoted Gauss-Jordan reference, not the SIMD cofactor model.
    double a[4][8]{};
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){a[r][c]=m[r*4+c];a[r][c+4]=r==c?1.:0.;}
    for(unsigned col=0;col<4;++col){unsigned pivot=col;
        for(unsigned r=col+1;r<4;++r)if(std::abs(a[r][col])>std::abs(a[pivot][col]))pivot=r;
        assert(a[pivot][col]!=0);for(unsigned c=0;c<8;++c)std::swap(a[pivot][c],a[col][c]);
        const double divisor=a[col][col];for(double& x:a[col])x/=divisor;
        for(unsigned r=0;r<4;++r)if(r!=col){const double factor=a[r][col];for(unsigned c=0;c<8;++c)a[r][c]-=factor*a[col][c];}
    }
    std::array<double,16> out{};for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)out[r*4+c]=a[r][c+4];return out;
}
void check(const Matrix& m){
    Matrix inverse;assert(native_stereo_view_inverse(m,inverse));const auto expected=reference(m);
    for(unsigned i=0;i<16;++i)assert(std::abs(double(inverse[i])-expected[i])<2e-5*(1+std::abs(expected[i])));
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){double product=0;
        for(unsigned k=0;k<4;++k)product+=double(m[r*4+k])*inverse[k*4+c];
        assert(std::abs(product-(r==c?1.:0.))<1e-4);
    }
}
}
int main(){
    NativeStereoFpGuard defaults(true);Matrix m=identity(),out;
    check(m);assert(native_stereo_view_inverse(m,out));
    const auto estimate=_mm_rcp_ss(_mm_set_ss(1));
    const float refined=_mm_cvtss_f32(_mm_sub_ss(_mm_add_ss(estimate,estimate),_mm_mul_ss(estimate,estimate)));
    for(unsigned i:{0u,5u,10u,15u})assert(!std::memcmp(&out[i],&refined,4));
    m={0,-1,0,0,1,0,0,0,0,0,1,0,21,-100,3,1};check(m);
    NativeStereoClassifyFloat3 p;assert(native_stereo_bounds_position(m,{0,0,50},p));
    assert(std::abs(p.x+100)<1e-4&&std::abs(p.y+21)<1e-4&&std::abs(p.z-47)<1e-4);
    std::mt19937 random(107);std::uniform_real_distribution<float> value(-.3f,.3f);
    for(unsigned i=0;i<1000;++i){for(float& f:m)f=value(random);for(unsigned j=0;j<4;++j)m[j*4+j]+=2;check(m);}
    m=identity();m[12]=17;m[13]=-23;m[14]=51;auto aliased=m;assert(native_stereo_view_inverse(aliased,aliased));
    assert(native_stereo_view_inverse(m,out)&&aliased==out);
    const Matrix before=out;
    for(unsigned failure=0;failure<5;++failure){m=identity();
        if(failure==0)m={};else if(failure==1)m[0]=std::numeric_limits<float>::infinity();
        else if(failure==2)m[15]=std::numeric_limits<float>::quiet_NaN();
        else if(failure==3)m[0]=m[5]=m[10]=m[15]=(std::numeric_limits<float>::max)();
        else _mm_setcsr(0x9f80);
        assert(!native_stereo_view_inverse(m,out));assert(!std::memcmp(&out,&before,sizeof(out)));_mm_setcsr(0x1f80);
    }
    _mm_setcsr(0x1fbf);assert(native_stereo_view_inverse(identity(),out)&&_mm_getcsr()==0x1fbf);
    p={9,8,7};assert(!native_stereo_bounds_position({}, {1,2,3},p)&&p.x==9&&p.y==8&&p.z==7);
    assert(!native_stereo_bounds_position(identity(),{NAN,0,0},p)&&p.x==9);
    std::cout<<"Native view inverse: cofactor/reciprocal order, independent dense/projective reference, bounds transform, aliasing and atomic refusal passed\n";
}

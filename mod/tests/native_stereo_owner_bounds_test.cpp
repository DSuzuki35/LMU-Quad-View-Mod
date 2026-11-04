#include "../native_stereo_owner_bounds.hpp"
#include <cassert>
#include <iostream>
#include <limits>

using namespace lmuqv;
using Reason=NativeStereoOwnerBoundsReason;
NativeStereoOwnerBounds box(float x=0,float y=0,float z=0,float extent=.5f,float radius=1){
    NativeStereoOwnerBounds out;out.available=true;out.center={x,y,z};out.radius=radius;
    for(unsigned i=0;i<8;++i)out.corners[i]={x+(i&1?extent:-extent),y+(i&2?extent:-extent),z+(i&4?extent:-extent)};
    return out;
}
NativeStereoOwnerBoundsView frustum(){
    NativeStereoOwnerBoundsView out;out.available=true;out.range=100;
    out.planes={{{1,0,0,-10},{-1,0,0,-10},{0,1,0,-10},{0,-1,0,-10},{0,0,1,-10},{0,0,-1,-10}}};return out;
}
void refused(const NativeStereoOwnerBoundsResult& out,Reason reason){
    assert(!out.available&&!out.admitted&&out.reason==reason&&out.flags==0&&out.writeCount==0);
    for(auto flags:out.flagWrites)assert(flags==0);
}
int main(){
    NativeStereoFpGuard defaults(true);
    auto b=box();auto v=frustum();const auto before=b;const auto viewBefore=v;
    auto out=classify_native_stereo_owner_bounds(b,v,0x80000);
    assert(out.available&&out.admitted&&out.flags==0x90000&&out.writeCount==1);
    assert(b.available==before.available&&b.radius==before.radius&&!std::memcmp(&b.corners,&before.corners,sizeof(b.corners))&&
           !std::memcmp(&b.center,&before.center,sizeof(b.center))&&v.available==viewBefore.available&&v.range==viewBefore.range&&
           !std::memcmp(&v.planes,&viewBefore.planes,sizeof(v.planes))&&!std::memcmp(&v.position,&viewBefore.position,sizeof(v.position)));
    b=box(3);v.range=2;out=classify_native_stereo_owner_bounds(b,v,0x80000);
    assert(out.available&&out.admitted); // distance exactly range + radius
    b=box(std::nextafter(3.f,4.f));out=classify_native_stereo_owner_bounds(b,v,0x90000);
    assert(out.available&&!out.admitted&&out.flags==0x80000&&out.writeCount==1);
    b=box(12);v=frustum();out=classify_native_stereo_owner_bounds(b,v,0x80000);
    assert(out.available&&!out.admitted&&out.writeCount==2&&out.flagWrites[0]==0x90000&&out.flagWrites[1]==0x80000);
    b=box(9.5f);out=classify_native_stereo_owner_bounds(b,v,0x80000);
    assert(out.available&&out.admitted&&out.flags==0x80090000&&out.writeCount==2);
    // Native sphere intersection exits early; the later corner gate must still
    // reject an object entirely beyond another plane, retaining the sign write.
    b=box(9.5f,0,0,.1f);v.planes[2]={0,1,0,1};
    out=classify_native_stereo_owner_bounds(b,v,0x80000);
    assert(out.available&&!out.admitted&&out.flags==0x80080000&&out.writeCount==3);
    assert(out.flagWrites[0]==0x90000&&out.flagWrites[1]==0x80090000&&out.flagWrites[2]==0x80080000);
    b=box(10,0,0,0);v=frustum();out=classify_native_stereo_owner_bounds(b,v,0x80000);
    assert(out.available&&out.admitted); // plane equality includes every corner
    b=box(std::nextafter(10.f,11.f),0,0,0);
    out=classify_native_stereo_owner_bounds(b,v,0x80000);assert(out.available&&!out.admitted);
    b=box();out=classify_native_stereo_owner_bounds(b,v,0x80080004);
    assert(out.available&&out.admitted&&out.flags==0x80090004); // sign and unrelated bits persist
    for(unsigned axis=0;axis<3;++axis)for(float sign:{-1.f,1.f}){
        b=box(axis==0?sign*12:0,axis==1?sign*12:0,axis==2?sign*12:0);
        out=classify_native_stereo_owner_bounds(b,v,0x80000);assert(out.available&&!out.admitted);
    }
    b=box(0,0,0,0,0);v.range=0;out=classify_native_stereo_owner_bounds(b,v,0x80000);
    assert(out.available&&out.admitted);
    b=box();v=frustum();b.available=false;refused(classify_native_stereo_owner_bounds(b,v,0x80000),Reason::MissingInputs);
    b.available=true;v.available=false;refused(classify_native_stereo_owner_bounds(b,v,0x80000),Reason::MissingInputs);
    v=frustum();for(uint32_t flags:{0u,0x480000u,0x280000u})refused(classify_native_stereo_owner_bounds(b,v,flags),Reason::UnsupportedFlags);
    b.radius=-1;refused(classify_native_stereo_owner_bounds(b,v,0x80000),Reason::InvalidDomain);
    b=box();v.range=-1;refused(classify_native_stereo_owner_bounds(b,v,0x80000),Reason::InvalidDomain);
    v=frustum();b.center.x=std::numeric_limits<float>::infinity();refused(classify_native_stereo_owner_bounds(b,v,0x80000),Reason::NonFiniteInput);
    b=box();b.corners[7].z=std::numeric_limits<float>::quiet_NaN();refused(classify_native_stereo_owner_bounds(b,v,0x80000),Reason::NonFiniteInput);
    b=box(1000);v.planes[5].w=std::numeric_limits<float>::quiet_NaN();refused(classify_native_stereo_owner_bounds(b,v,0x80000),Reason::NonFiniteInput);
    b=box();v=frustum();b.radius=(std::numeric_limits<float>::max)();refused(classify_native_stereo_owner_bounds(b,v,0x80000),Reason::NonFiniteArithmetic);
    b=box();v=frustum();b.center.x=(std::numeric_limits<float>::max)();refused(classify_native_stereo_owner_bounds(b,v,0x80000),Reason::NonFiniteArithmetic);
    // Overflow after a provisional sphere write returns a wholly empty result.
    b=box(0,0,0,2);v=frustum();v.planes[5]={(std::numeric_limits<float>::max)(),0,0,-10};
    refused(classify_native_stereo_owner_bounds(b,v,0x80000),Reason::NonFiniteArithmetic);
    b=box();v=frustum();_mm_setcsr(0x1fbf);out=classify_native_stereo_owner_bounds(b,v,0x80000);
    assert(out.available&&_mm_getcsr()==0x1fbf);
    for(unsigned csr:{0x9f80u,0x1fc0u,0x3f80u,0x1f00u}){
        _mm_setcsr(csr);refused(classify_native_stereo_owner_bounds(b,v,0x80000),Reason::UnsupportedFp);assert(_mm_getcsr()==csr);
    }
    _mm_setcsr(0x1f80);
    std::cout<<"Owned owner bounds: distance and plane boundaries, sphere/corner order, six planes, flag writes, unsupported refresh/special paths, finite arithmetic and FP preservation passed\n";
}

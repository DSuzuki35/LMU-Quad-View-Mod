#include "../native_stereo_view_inputs.hpp"
#include <cassert>
#include <iostream>
using namespace lmuqv;
Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
int main(){
    NativeStereoFpGuard defaults(true);
    auto g=prepare_native_stereo_view_geometry(identity(),identity());assert(g.complete);
    const std::array<NativeStereoClassifyPlane,6> expected{{{-1,0,0,-1},{1,0,0,-1},{0,1,0,-1},{0,-1,0,-1},{0,0,-1,0},{0,0,1,-1}}};
    for(unsigned i=0;i<6;++i){const auto& a=g.planes[i];const auto& b=expected[i];assert(a.x==b.x&&a.y==b.y&&a.z==b.z&&a.w==b.w);}
    auto projection=identity();assert(make_engine_projection({-.8f,.9f,.7f,-.6f},.1f,100,1,1,projection));
    g=prepare_native_stereo_view_geometry(identity(),projection);assert(g.complete);
    for(const auto& p:g.planes){const float norm=std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);assert(std::abs(norm-1)<1e-6f);}
    projection[10]=1.f-projection[10];projection[14]=-projection[14];
    assert(prepare_native_stereo_view_geometry(identity(),projection).complete); // reversed depth
    NativeStereoFocusInputs focus;focus.complete=true;focus.identity={1,2,3,400,5};
    for(auto& eye:focus.eyes){eye.current=identity();eye.projection=identity();}
    focus.eyes[1].current[12]=-2;
    std::array<NativeStereoViewMetadata,2> metadata;
    for(unsigned eye=0;eye<2;++eye){auto& m=metadata[eye];m.complete=true;m.identity=focus.identity;m.eye=eye;m.scene=10;m.view=20;
        m.mask=eye?3:1;m.constantPosition={-float(eye)*2,-float(eye)*3,-float(eye)*4};
        m.classifyPosition={-m.constantPosition.x,-m.constantPosition.y,-m.constantPosition.z};
        m.boundsPosition={25,30,35};m.scale=2.5f;m.range=100;m.mode=1;}
    const auto good=metadata;auto out=prepare_native_stereo_view_inputs(focus,metadata);
    assert(out.complete&&!out.renderingEligible&&out.scene==10&&out.identity==focus.identity);
    assert(out.eyes[0].owner.mask==1&&out.eyes[1].owner.mask==3);
    assert(out.eyes[1].classification.viewPosition.x==2&&out.eyes[1].owner.bounds.position.x==25);
    assert(out.eyes[1].transform.position[0]==-2&&out.eyes[1].classification.viewScale==2.5f);
    assert(out.eyes[0].owner.bounds.planes[0].w!=out.eyes[1].owner.bounds.planes[0].w);
    for(unsigned failure=0;failure<9;++failure){metadata=good;
        switch(failure){
        case 0:metadata[1].eye=0;break;
        case 1:++metadata[1].identity.frame;break;
        case 2:metadata[1].scene=11;break;
        case 3:metadata[1].view=0;break;
        case 4:metadata[1].parentNull=false;break;
        case 5:metadata[1].classifyPosition.x=3;break;
        case 6:metadata[1].range=-1;break;
        case 7:metadata[1].scale=0;break;
        default:metadata[1].boundsPosition.z=std::numeric_limits<float>::quiet_NaN();break;
        }
        out=prepare_native_stereo_view_inputs(focus,metadata);assert(!out.complete&&out.scene==0&&!out.renderingEligible);
    }
    metadata=good;_mm_setcsr(0x1fbf);assert(prepare_native_stereo_view_inputs(focus,metadata).complete&&_mm_getcsr()==0x1fbf);
    _mm_setcsr(0x9f80);assert(!prepare_native_stereo_view_inputs(focus,metadata).complete&&_mm_getcsr()==0x9f80);_mm_setcsr(0x1f80);
    assert(!prepare_native_stereo_view_geometry({},identity()).complete);
    Matrix huge=identity();huge[0]=(std::numeric_limits<float>::max)();assert(!prepare_native_stereo_view_geometry(huge,huge).complete);
    huge=identity();huge[0]=std::numeric_limits<float>::infinity();assert(!prepare_native_stereo_view_geometry(huge,identity()).complete);
    std::cout<<"Owned stereo view inputs: native-order matrix/planes, independent masks and three position domains, frame/eye checks, reversed depth, finite arithmetic and FP preservation passed\n";
}

#include "../stereo_constants.hpp"
#include <cassert>
#include <iostream>
#include "allocation_failure_fixture.hpp"
using namespace lmuqv;
NativeStereoMatrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
float value(const std::vector<uint8_t>& bytes,size_t offset){float out=0;std::memcpy(&out,bytes.data()+offset,4);return out;}
int main(){
    NativeStereoFpGuard defaults(true);
    std::array<NativeStereoTransformInput,2> inputs;
    for(unsigned eye=0;eye<2;++eye){auto& in=inputs[eye];in.world=in.derived=in.view=identity();in.world[12]=10;
        in.derived[12]=30+float(eye);in.view[12]=20+float(eye);in.position={1+float(eye),2,3};in.localTransform=true;}
    std::array<std::vector<uint8_t>,2> base;for(unsigned eye=0;eye<2;++eye)base[eye].assign(512,uint8_t(eye?0x5a:0xa5));
    auto out=prepare_stereo_transform_payloads(inputs,base);assert(out.complete);
    assert(value(out.shadow[0],12*4)==40&&value(out.shadow[1],12*4)==41);
    assert(value(out.shadow[0],4*16+12*4)==10&&value(out.shadow[0],16*16+12*4)==30);
    assert(value(out.shadow[0],8*16)==1&&value(out.shadow[1],8*16)==2&&value(out.shadow[0],8*16+12)==0);
    for(unsigned eye=0;eye<2;++eye){for(size_t b=9*16;b<16*16;++b)assert(out.shadow[eye][b]==base[eye][b]);
        for(size_t b=20*16;b<base[eye].size();++b)assert(out.shadow[eye][b]==base[eye][b]);}
    for(auto& in:inputs)in.localTransform=false;out=prepare_stereo_transform_payloads(inputs,base);
    assert(out.complete&&value(out.shadow[0],12*4)==30&&value(out.shadow[0],16*16+12*4)==20);
    auto bad=base;bad[1].resize(319);assert(!prepare_stereo_transform_payloads(inputs,bad).complete);
    inputs[1].world[0]=std::numeric_limits<float>::infinity();out=prepare_stereo_transform_payloads(inputs,base);
    assert(!out.complete&&out.shadow[0].empty()&&out.shadow[1].empty());inputs[1].world=identity();
    StereoShaderVariant variant;variant.eyeStride=4;variant.constants={{0,2,0},{4,1,3}};
    std::array<StereoConstantBuffers,2> source;
    for(unsigned eye=0;eye<2;++eye){source[eye][0].assign(32,uint8_t(eye?10:1));source[eye][4].assign(16,uint8_t(eye?40:4));}
    auto packed=pack_stereo_constants(variant,source);assert(packed.complete&&packed.slot==13&&packed.bytes.size()==128);
    for(unsigned eye=0;eye<2;++eye){for(unsigned i=0;i<32;++i)assert(packed.bytes[eye*64+i]==(eye?10:1));
        for(unsigned i=32;i<48;++i)assert(packed.bytes[eye*64+i]==0);
        for(unsigned i=48;i<64;++i)assert(packed.bytes[eye*64+i]==(eye?40:4));}
    const auto good=variant;
    for(unsigned failure=0;failure<7;++failure){variant=good;
        switch(failure){case 0:variant.packedSlot=14;break;case 1:variant.eyeStride=2049;break;case 2:variant.constants[1].offset=1;break;
        case 3:variant.constants[1].sourceSlot=0;break;case 4:variant.constants[1].sourceSlot=14;break;
        case 5:variant.constants[1].registers=(std::numeric_limits<unsigned>::max)();break;default:variant.eyeStride=0;break;}
        packed=pack_stereo_constants(variant,source);assert(!packed.complete&&packed.bytes.empty());}
    variant=good;source[1][4].clear();assert(!pack_stereo_constants(variant,source).complete);
    variant={};packed=pack_stereo_constants(variant,source);assert(packed.complete&&packed.bytes.empty());
    _mm_setcsr(0x9f80);assert(!prepare_stereo_transform_payloads(inputs,base).complete&&_mm_getcsr()==0x9f80);_mm_setcsr(0x1f80);
    allocation_refusals([&]{return prepare_stereo_transform_payloads(inputs,base);},[](const auto& p,bool complete){assert(p.complete==complete);if(!complete)assert(p.shadow[0].empty()&&p.shadow[1].empty());});
    variant=good;source[1][4].assign(16,42);
    allocation_refusals([&]{return pack_stereo_constants(variant,source);},[](const auto& p,bool complete){assert(p.complete==complete);if(!complete)assert(p.bytes.empty());});
    std::cout<<"Owned stereo constants: native shadow offsets, untouched material bytes, per-eye transforms, packed CB banks/holes, range bounds and whole-pair fallback passed\n";
}

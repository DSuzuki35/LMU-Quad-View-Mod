#include "../native_stereo_constants.hpp"
#include "allocation_failure_fixture.hpp"
#include <cassert>
#include <iostream>
using namespace lmuqv;
namespace {
std::vector<uint8_t> renderer(0x82cb0),image(0x1b1a800);
uintptr_t blocked=0;size_t calls=0;
bool readable(const void* address,size_t size){++calls;const auto p=reinterpret_cast<uintptr_t>(address);
    if(blocked&&p<=blocked&&size>blocked-p)return false;
    for(const auto* buffer:{&renderer,&image}){const auto first=reinterpret_cast<uintptr_t>(buffer->data());if(p>=first&&p-first<=buffer->size()&&size<=buffer->size()-(p-first))return true;}return false;}
template<class T> void put(size_t offset,const T& value){std::memcpy(renderer.data()+offset,&value,sizeof(value));}
NativeStereoMatrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
void reset(){std::fill(renderer.begin(),renderer.end(),uint8_t(0));std::fill(image.begin(),image.end(),uint8_t(0));blocked=0;calls=0;
    for(unsigned stage=0;stage<2;++stage){const auto fill=[&](const auto& ranges){for(unsigned i=0;i<ranges.size();++i){const auto& range=ranges[i];
        const unsigned slot=stage&&i==5?6:i;put(range.slot,slot);put(range.buffer,uint64_t(100+stage*100+i));
        std::memset(renderer.data()+range.data,int(i+stage*20+1),range.bytes);}};
        if(stage)fill(nativeStereoPixelConstants);else fill(nativeStereoVertexConstants);}}
NativeStereoConstantSnapshot capture(size_t budget=65536){return capture_native_stereo_constants(reinterpret_cast<uintptr_t>(renderer.data()),reinterpret_cast<uintptr_t>(image.data()),{1,2,3,400,5},{17,33},readable,budget);}
void empty(const NativeStereoConstantSnapshot& s){assert(!s.complete&&!s.renderingEligible);for(const auto& stage:s.stages)for(const auto& bytes:stage)assert(bytes.empty());}
float value(const std::vector<uint8_t>& bytes,size_t offset){float f=0;std::memcpy(&f,bytes.data()+offset,4);return f;}
}
int main(){
    NativeStereoFpGuard defaults(true);reset();auto snapshot=capture();assert(snapshot.complete&&!snapshot.renderingEligible&&snapshot.bytesRead<2000);
    assert(snapshot.vertexCommonSlot==0&&snapshot.pixelCommonSlot==0&&snapshot.stages[0][0].size()==272&&snapshot.stages[0][4].size()==640);
    assert(snapshot.stages[1][0].size()==288&&snapshot.stages[1][5]==snapshot.stages[0][0]&&snapshot.bufferTokens[1][5]==snapshot.bufferTokens[0][0]);
    assert(snapshot.stages[0][1].empty()&&snapshot.stages[1][3].empty());const auto retained=snapshot.stages;std::fill(renderer.begin(),renderer.end(),uint8_t(0));assert(snapshot.stages==retained);
    std::array<NativeStereoTransformInput,2> input;
    for(unsigned eye=0;eye<2;++eye){input[eye].world=input[eye].derived=input[eye].view=identity();input[eye].world[12]=10;
        input[eye].derived[12]=30+float(eye);input[eye].view[12]=20+float(eye);input[eye].position={float(eye)+1,2,3};input[eye].localTransform=true;}
    std::array<NativeStereoConstantSnapshot,2> bases{snapshot,snapshot};auto constants=prepare_native_stereo_draw_constants(input,bases);assert(constants.complete&&!constants.renderingEligible);
    for(unsigned eye=0;eye<2;++eye){const auto& v=constants.eyes[eye][0][0];const auto& p=constants.eyes[eye][1][0];
        assert(value(v,12*4)==10&&value(v,64+12*4)==40+float(eye)&&value(v,128)==float(eye)+1&&value(v,160+12*4)==30+float(eye));
        assert(value(p,192)==float(eye)+1&&value(p,204)==0&&constants.eyes[eye][1][5]==v);
        for(size_t i=144;i<160;++i)assert(v[i]==snapshot.stages[0][0][i]);for(size_t i=224;i<272;++i)assert(v[i]==snapshot.stages[0][0][i]);
        for(size_t i=0;i<288;++i)if(i<192||i>=208)assert(p[i]==snapshot.stages[1][0][i]);
        assert(constants.eyes[eye][0][4]==snapshot.stages[0][4]);}
    for(unsigned failure=0;failure<9;++failure){reset();switch(failure){
        case 0:put(nativeStereoVertexConstants[1].slot,0u);break;
        case 1:put(nativeStereoPixelConstants[2].slot,5u);break;
        case 2:put(nativeStereoVertexConstants[0].buffer,uint64_t(0));break;
        case 3:put(nativeStereoPixelConstants[0].buffer,uint64_t(100));break;
        case 4:renderer[0xa56]=1;break;
        case 5:image[0x1b1a58e]=1;break;
        case 6:image[0x1b1a7fd]=1;break;
        case 7:blocked=reinterpret_cast<uintptr_t>(renderer.data())+nativeStereoPixelConstants[0].data+4;break;
        default:put(nativeStereoVertexConstants[0].slot,13u);break;}
        empty(capture());}
    reset();empty(capture(256));empty(capture_native_stereo_constants((std::numeric_limits<uintptr_t>::max)()-8,reinterpret_cast<uintptr_t>(image.data()),{1,2,3,400,5},{17,33},readable));
    allocation_refusals([]{return capture();},[](const auto& s,bool ok){if(ok)assert(s.complete);else empty(s);});
    allocation_refusals([&]{return prepare_native_stereo_draw_constants(input,bases);},[](const auto& s,bool ok){assert(s.complete==ok);if(!ok)for(const auto& eye:s.eyes)for(const auto& stage:eye)for(const auto& b:stage)assert(b.empty());});
    bases[1].identity.frame++;assert(!prepare_native_stereo_draw_constants(input,bases).complete);bases[1]=snapshot;bases[1].stages[1][5][0]^=1;assert(!prepare_native_stereo_draw_constants(input,bases).complete);
    std::cout<<"Native constant images: real VS/PS remapping and PS b5 alias, preserved material bytes, selected CPU-shadow reads, atomic allocation/read/domain/identity refusal passed\n";
}

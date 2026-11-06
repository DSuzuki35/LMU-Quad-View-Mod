#include "../sps_game_parameters.hpp"
#include <cassert>
#include <iostream>

using namespace lmuqv;
namespace {
const uint8_t* memory=nullptr;size_t memorySize=0;
bool readable(const void* p,size_t bytes){const auto at=reinterpret_cast<uintptr_t>(p),start=reinterpret_cast<uintptr_t>(memory);
    return at>=start&&at-start<=memorySize&&bytes<=memorySize-(at-start);}
float scalar(const std::vector<uint8_t>& image,size_t offset){float value=0;assert(offset+4<=image.size());std::memcpy(&value,image.data()+offset,4);return value;}
void run(){
    // Slot placement belongs to the game-selected shader ABI. Exercise a
    // permutation, rather than assuming the common image is always b0.
    SpsGameParameterLayout layout;layout.vertex={8,7,6,5,4,3,2,1,0};layout.pixel={6,4,3,2,1,0};assert(layout.valid());
    std::vector<uint8_t> renderer(0x82cb0);memory=renderer.data();memorySize=renderer.size();
    for(unsigned i=0;i<9;++i)std::memcpy(renderer.data()+nativeStereoVertexConstants[i].slot,&layout.vertex[i],4);
    for(unsigned i=0;i<6;++i)std::memcpy(renderer.data()+nativeStereoPixelConstants[i].slot,&layout.pixel[i],4);
    SpsGameParameterLayout copied;assert(read_sps_game_parameter_layout(reinterpret_cast<uintptr_t>(memory),readable,copied));assert(copied.vertex==layout.vertex&&copied.pixel==layout.pixel);
    renderer[0xa56]=1;assert(!read_sps_game_parameter_layout(reinterpret_cast<uintptr_t>(memory),readable,copied));renderer[0xa56]=0;
    auto invalid=layout;invalid.pixel[0]=5;assert(!invalid.valid());invalid=layout;invalid.vertex[1]=invalid.vertex[0];assert(!invalid.valid());
    SpsGameParameters parameters(layout);std::array<float,16> world{},projection{},view{};
    for(unsigned i=0;i<16;++i){world[i]=float(i+1);projection[i]=float(100+i);view[i]=float(200+i);}
    const float eye[4]{10,20,30,0},tint[4]{.25f,.5f,.75f,1};
    assert(parameters.vertex(4,world.data(),4)&&parameters.vertex(0,projection.data(),4)&&parameters.vertex(8,eye,1));
    assert(parameters.vertex(14,tint,1)&&parameters.vertex(16,view.data(),4));
    std::array<Matrix,40> bones;for(unsigned i=0;i<bones.size();++i)bones[i].fill(float(i+1));
    assert(parameters.vertex(84,bones.data(),160));
    assert(!parameters.vertex(84,bones.data(),165));assert(!parameters.vertex(17,world.data(),1));
    assert(parameters.pixel(0,tint,1)&&parameters.pixel(15,eye,1)&&parameters.pixel(17,world.data(),4));
    const float extra[4]{301,302,303,304};assert(parameters.pixel(91,extra,1)&&parameters.pixel(901,extra,1));
    assert(parameters.pixel(902,extra,1)&&parameters.pixel(914,extra,1)&&parameters.pixel(919,extra,1));
    const uint8_t flags[]{0,1,2,0,1,1,0,255};assert(parameters.vertex_booleans(0,flags,8));assert(!parameters.vertex_booleans(7,flags,2));
    const auto images=parameters.take();const auto& common=images[0][layout.vertex[0]];
    assert(common.size()==272&&scalar(common,0)==1&&scalar(common,64)==100&&scalar(common,128)==10&&scalar(common,144)==.25f&&scalar(common,160)==200);
    assert(scalar(common,224)==301&&images[1][5]==common);
    const auto& palette=images[0][layout.vertex[2]];assert(palette.size()==2624&&scalar(palette,0)==1&&scalar(palette,39*64)==40&&scalar(palette,2560)==0);
    const auto& pixel=images[1][layout.pixel[0]];assert(pixel.size()==288&&scalar(pixel,0)==.25f&&scalar(pixel,192)==10&&scalar(pixel,208)==301&&scalar(pixel,224)==1);
    assert(scalar(images[1][layout.pixel[2]],1648)==301&&scalar(images[1][layout.pixel[3]],0)==301&&scalar(images[1][layout.pixel[1]],48)==301);
    const auto& booleans=images[0][layout.vertex[8]];for(unsigned i=0;i<8;++i){uint32_t flag=0;std::memcpy(&flag,booleans.data()+i*4,4);assert(flag==flags[i]);}
    const auto patches=sps_game_transform_patches(layout,images);assert(patches.size()==9);
    assert(patches[0].stage==0&&patches[0].slot==8&&patches[0].value==SpsConstantValue::World);
    assert(patches[4].stage==1&&patches[4].slot==5&&patches[8].slot==6&&patches[8].offset==192);
    std::cout<<"SPS game parameters: owned shader ABI, permuted CB slots, common transforms/PS alias, skinning palette, material fields and bounded writes; zero native setters/preparation passed\n";
}
}
int main(){run();return 0;}

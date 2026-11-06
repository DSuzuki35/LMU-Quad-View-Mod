#pragma once
#include "native_stereo_constants.hpp"
#include "sps_game_mesh_inputs.hpp"
#include "sps_scene.hpp"

namespace lmuqv {
struct SpsGameParameterLayout {
    std::array<unsigned,9> vertex{};
    std::array<unsigned,6> pixel{};
    bool valid()const{
        uint32_t vs=0,ps=1u<<5;
        for(auto slot:vertex){if(slot>=13||(vs&(1u<<slot)))return false;vs|=1u<<slot;}
        for(auto slot:pixel){if(slot>=13||(ps&(1u<<slot)))return false;ps|=1u<<slot;}return true;
    }
};
// Read the shader ABI selected at renderer initialization, never its mutable
// prepared-eye constant images or GPU Map(WRITE) memory.
inline bool read_sps_game_parameter_layout(uint64_t renderer,bool(*readable)(const void*,size_t),SpsGameParameterLayout& out){
    out={};size_t bytes=0;sps_game_input_detail::Reader reader(readable,bytes);SpsGameParameterLayout layout;
    uint8_t alternate=1;if(!reader.read(renderer,0xa56,alternate)||alternate)return false;
    for(size_t i=0;i<layout.vertex.size();++i)if(!reader.read(renderer,nativeStereoVertexConstants[i].slot,layout.vertex[i]))return false;
    for(size_t i=0;i<layout.pixel.size();++i)if(!reader.read(renderer,nativeStereoPixelConstants[i].slot,layout.pixel[i]))return false;
    if(!layout.valid())return false;out=layout;return true;
}

// Owned material/frame/object writers use the pinned logical-register ABI.
// Each selected CB is allocated once and shared by all views. Writes outside a
// known buffer fail the whole preparation; no original setter/preparer is used.
class SpsGameParameters {
    SpsGameParameterLayout layout_;
    std::array<StereoConstantBuffers,2> images_;
    bool valid_=false;
    bool write(unsigned stage,unsigned buffer,size_t offset,const void* data,size_t bytes){
        if(!valid_||!data||!bytes||bytes%16)return false;
        const size_t limit=stage?nativeStereoPixelConstants[buffer].bytes:nativeStereoVertexConstants[buffer].bytes;
        if(offset>limit||bytes>limit-offset)return false;
        const unsigned slot=stage?layout_.pixel[buffer]:layout_.vertex[buffer];auto& image=images_[stage][slot];
        if(image.empty())image.resize(limit,0);std::memcpy(image.data()+offset,data,bytes);return true;
    }
public:
    explicit SpsGameParameters(SpsGameParameterLayout layout):layout_(layout),valid_(layout.valid()){}
    bool vertex(unsigned logical,const void* data,unsigned registers){
        if(!registers||registers>4096)return false;unsigned buffer=0;size_t offset=0;
        if(logical<4)offset=64;
        else if(logical==4)offset=0;
        else if(logical==8)offset=128;
        else if(logical>=9&&logical<=13){buffer=6;offset=(logical-9)*16;}
        else if(logical==14)offset=144;
        else if(logical==15){buffer=6;offset=80;}
        else if(logical==16)offset=160;
        else if(logical==44)buffer=4;
        else if(logical==64){buffer=4;offset=320;}
        else if(logical==84)buffer=2;
        else if(logical==252){buffer=6;offset=96;}
        else if(logical==800)buffer=3;
        else if(logical==809)buffer=1;
        else if(logical==900){buffer=6;offset=112;}
        else if(logical==901){buffer=6;offset=176;}
        else if(logical>=20){buffer=5;offset=size_t(logical-20)*16;}
        else return false;
        return write(0,buffer,offset,data,size_t(registers)*16);
    }
    bool pixel(unsigned logical,const void* data,unsigned registers){
        if(!registers||registers>4096)return false;unsigned buffer=0;size_t offset=0;
        if(logical<=3)offset=logical*16;
        else if(logical==4)buffer=1;
        else if(logical==5)offset=64;
        else if(logical==6){buffer=1;offset=16;}
        else if(logical>=7&&logical<=9)offset=(logical-2)*16;
        else if(logical==10){buffer=1;offset=32;}
        else if(logical==17)offset=224;
        else if(logical==91){ // PS logical c91 is in the aliased VS common image.
            return write(0,0,224,data,size_t(registers)*16);
        }
        else if(logical==901)offset=208;
        else if(logical>=902&&logical<=913){buffer=2;offset=1648+(logical-902)*16;}
        else if(logical==914)buffer=3;
        else if(logical>=915&&logical<=918){buffer=2;offset=1840+(logical-915)*16;}
        else if(logical==919){buffer=1;offset=48;}
        else if(logical<=15)offset=(logical-3)*16;
        else {buffer=2;offset=logical<192?size_t(logical-16)*16:1520+size_t(logical-192)*16;}
        return write(1,buffer,offset,data,size_t(registers)*16);
    }
    bool vertex_booleans(unsigned first,const uint8_t* values,unsigned count){
        if(!values||first>=8||!count||count>8-first)return false;
        for(unsigned i=0;i<count;++i){std::array<uint32_t,4> value{}; // Update scalar lanes, not sixteen bytes per boolean.
            if(!valid_)return false;auto& image=images_[0][layout_.vertex[8]];if(image.empty())image.resize(32,0);
            value[0]=values[i];std::memcpy(image.data()+(first+i)*4,value.data(),4);}return true;
    }
    std::array<StereoConstantBuffers,2> take(){
        if(!valid_)return {};const auto& common=images_[0][layout_.vertex[0]];
        if(!common.empty())images_[1][5]=common;return std::move(images_);
    }
};

inline std::vector<SpsConstantPatch> sps_game_transform_patches(const SpsGameParameterLayout& layout,const std::array<StereoConstantBuffers,2>& images){
    std::vector<SpsConstantPatch> patches;if(!layout.valid())return patches;
    const auto common=[&](unsigned stage,unsigned slot){if(images[stage][slot].empty())return;
        patches.push_back({stage,slot,0,SpsConstantValue::World});patches.push_back({stage,slot,64,SpsConstantValue::WorldViewProjection});
        patches.push_back({stage,slot,128,SpsConstantValue::EyePosition});patches.push_back({stage,slot,160,SpsConstantValue::WorldView});};
    common(0,layout.vertex[0]);common(1,5);
    if(!images[1][layout.pixel[0]].empty())patches.push_back({1,layout.pixel[0],192,SpsConstantValue::EyePosition});return patches;
}
}

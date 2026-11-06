#pragma once
#include "sps_game_parameters.hpp"

namespace lmuqv {
struct SpsGameLightingScene {
    std::array<float,3> ambient{1,1,1},diffuse{1,1,1},direction{};
    float ambientIntensity=1,diffuseIntensity=1,environment=0,specularScale=1;
};
struct SpsGameMaterialColors {
    uint32_t flags=0;
    std::array<float,4> diffuse{},ambient{},specular{},emission{};
    float intensityOverride=0,specularPower=0;
};
struct SpsGameLightingResult {
    bool complete=false;
    const char* reason="SPS material lighting incomplete";
    std::array<std::array<float,4>,4> pixel{};
};

// Shared scene values, sampled once before the owned union traversal. No
// material color cache, selected native eye, renderer setter or callback.
inline bool read_sps_game_lighting_scene(uint64_t scene,uint64_t image,
    bool(*readable)(const void*,size_t),SpsGameLightingScene& output) noexcept {
    output={};if(!scene||!image||!readable)return false;
    try{
        size_t bytes=0;sps_game_input_detail::Reader reader(readable,bytes);SpsGameLightingScene input;
        uint64_t begin=0,end=0,environment=0;
        if(!reader.read(scene,0x10c,input.direction)||!reader.read(scene,0x63c,input.ambient)||
           !reader.read(scene,0x928,begin)||!reader.read(scene,0x930,end)||!reader.read(scene,0xac0,environment)||
           !reader.read(image,0x19a16f0,input.specularScale))return false;
        if((begin|end)&7||end<begin||end-begin>4096*8||(!begin&&end))return false;
        if(begin!=end){uint64_t light=0;std::array<float,5> values{};
            if(!reader.read(begin,0,light)||!reader.read(light,0x34,values))return false;
            input.ambientIntensity=values[0];input.diffuse={values[1],values[2],values[3]};input.diffuseIntensity=values[4];
        }
        if(environment&&!reader.read(environment,0xc4,input.environment))return false;
        output=input;return true;
    }catch(...){return false;}
}
inline bool read_sps_game_material_colors(uint64_t material,bool(*readable)(const void*,size_t),SpsGameMaterialColors& output) noexcept {
    output={};if(!material||!readable)return false;
    try{size_t bytes=0;sps_game_input_detail::Reader reader(readable,bytes);SpsGameMaterialColors input;
        std::array<std::array<float,4>,4> colors;
        if(!reader.read(material,0,input.flags)||!reader.read(material,0x74,colors)||
           !reader.read(material,0x518,input.intensityOverride)||!reader.read(material,0x534,input.specularPower))return false;
        input.diffuse=colors[0];input.ambient=colors[1];input.specular=colors[2];input.emission=colors[3];output=input;return true;
    }catch(...){return false;}
}

// Port the complete shared ambient/diffuse/specular preparation together. This
// replaces the per-eye 0x53f080 + 0x539a70 material cache mutations. The owning
// frame establishes default FP controls once, outside its material/object loops.
inline SpsGameLightingResult prepare_sps_game_lighting_values(const SpsGameLightingScene& scene,
    const SpsGameMaterialColors& material,const Matrix* directionBasis=nullptr) noexcept {
    const auto fail=[](const char* reason){SpsGameLightingResult out;out.reason=reason;return out;};
    if(!native_stereo_transform_default_controls(_mm_getcsr()))return fail("SPS lighting requires owned frame FP controls");
    const auto finite=[](const auto& values){for(float f:values)if(!std::isfinite(f))return false;return true;};
    if(!finite(scene.ambient)||!finite(scene.diffuse)||!finite(scene.direction)||!finite(material.ambient)||!finite(material.diffuse)||
       !finite(material.specular)||!finite(material.emission)||!std::isfinite(scene.ambientIntensity)||!std::isfinite(scene.diffuseIntensity)||
       !std::isfinite(scene.environment)||!std::isfinite(scene.specularScale)||!std::isfinite(material.intensityOverride)||!std::isfinite(material.specularPower)||
       (directionBasis&&!finite(*directionBasis)))return fail("non-finite SPS material lighting input");
    const float environment=std::abs(scene.environment);
    float ambientWeight=0,diffuseWeight=0;
    if(material.flags&0x80000u){diffuseWeight=material.intensityOverride;ambientWeight=2.f-diffuseWeight;}
    else {ambientWeight=scene.ambientIntensity*(1.f+environment);diffuseWeight=scene.diffuseIntensity*(1.f-environment);}
    SpsGameLightingResult out;
    for(unsigned channel=0;channel<3;++channel){
        float ambient=material.ambient[channel],diffuse=material.diffuse[channel],specular=material.specular[channel],emission=material.emission[channel];
        if(material.flags&0x200000u)ambient*=diffuseWeight;
        if(material.flags&0x1000000u)diffuse*=diffuseWeight;
        if(material.flags&0x8000000u)specular*=diffuseWeight;
        if(material.flags&0x40000000u)emission*=diffuseWeight;
        const float litAmbient=(ambient*scene.ambient[channel])*ambientWeight;
        const float lightDiffuse=scene.diffuse[channel]*scene.diffuseIntensity;
        const float litDiffuse=(diffuse*lightDiffuse)*diffuseWeight;
        const float litSpecular=specular*lightDiffuse;
        out.pixel[0][channel]=litAmbient+emission;out.pixel[1][channel]=litDiffuse;out.pixel[3][channel]=litSpecular*scene.specularScale;
    }
    out.pixel[0][3]=material.diffuse[3];
    out.pixel[1][3]=material.specularPower*.039788734167814255f+.31830987334251404f;
    std::array<float,3> direction{-scene.direction[0],-scene.direction[1],-scene.direction[2]};
    if(directionBasis){std::array<float,3> local{};for(unsigned axis=0;axis<3;++axis)
        local[axis]=(direction[0]*(*directionBasis)[axis]+direction[1]*(*directionBasis)[4+axis])+direction[2]*(*directionBasis)[8+axis];direction=local;}
    const float squared=(direction[0]*direction[0]+direction[1]*direction[1])+direction[2]*direction[2];
    if(!std::isfinite(squared)||squared<0)return fail("SPS material direction overflow");
    if(squared!=0){const float inverse=1.f/_mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(squared)));for(auto& channel:direction)channel*=inverse;}
    for(unsigned axis=0;axis<3;++axis)out.pixel[2][axis]=direction[axis];
    out.pixel[2][3]=scene.environment;out.pixel[3][3]=material.specularPower;
    for(const auto& color:out.pixel)if(!finite(color))return fail("SPS material lighting output overflow");
    out.complete=true;out.reason="owned shared SPS ambient, diffuse, directional and specular constants";return out;
}
inline bool write_sps_game_lighting(SpsGameParameters& parameters,const SpsGameLightingResult& lighting){
    return lighting.complete&&parameters.pixel(0,lighting.pixel.data(),4)&&parameters.vertex(14,lighting.pixel[2].data(),1);
}
}

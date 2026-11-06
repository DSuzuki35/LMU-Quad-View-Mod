#include "../sps_game_lighting.hpp"
#include <cassert>
#include <iostream>

using namespace lmuqv;
namespace {
std::vector<std::pair<uintptr_t,size_t>> spans;
bool readable(const void* p,size_t bytes){const auto address=reinterpret_cast<uintptr_t>(p);
    for(const auto& span:spans)if(address>=span.first&&address-span.first<=span.second&&bytes<=span.second-(address-span.first))return true;return false;}
template<size_t N>struct Region {
    std::array<uint8_t,N> bytes{};
    Region(){spans.push_back({token(),N});}
    uintptr_t token()const{return reinterpret_cast<uintptr_t>(bytes.data());}
    template<class T>void set(size_t offset,const T& value){assert(offset+sizeof(T)<=N);std::memcpy(bytes.data()+offset,&value,sizeof(value));}
};
void closeEnough(float value,double expected){assert(std::abs(double(value)-expected)<=2e-6*(1+std::abs(expected)));}
void run(){
    NativeStereoFpGuard frame(true);SpsGameLightingScene scene;scene.ambient={.2f,.3f,.4f};scene.diffuse={.5f,.6f,.7f};
    scene.ambientIntensity=.8f;scene.diffuseIntensity=.9f;scene.direction={0,3,4};scene.specularScale=1.5f;
    SpsGameMaterialColors material;material.ambient={.4f,.3f,.2f,.1f};material.diffuse={.9f,.8f,.7f,.6f};
    material.specular={.2f,.3f,.4f,.5f};material.emission={.01f,.02f,.03f,.04f};material.intensityOverride=1.25f;material.specularPower=24;
    const uint32_t flags[]{0x80000u,0x200000u,0x1000000u,0x8000000u,0x40000000u};
    for(float environment:{-.75f,0.f,.75f})for(unsigned mask=0;mask<32;++mask){scene.environment=environment;material.flags=0;
        for(unsigned bit=0;bit<5;++bit)if(mask&(1u<<bit))material.flags|=flags[bit];
        const auto result=prepare_sps_game_lighting_values(scene,material);assert(result.complete);
        const double diffuse=(mask&1)?1.25:double(scene.diffuseIntensity)*(1-std::abs(double(environment)));
        const double ambient=(mask&1)?.75:double(scene.ambientIntensity)*(1+std::abs(double(environment)));
        for(unsigned c=0;c<3;++c){
            closeEnough(result.pixel[0][c],double(material.ambient[c])*scene.ambient[c]*ambient*((mask&2)?diffuse:1)+double(material.emission[c])*((mask&16)?diffuse:1));
            closeEnough(result.pixel[1][c],double(material.diffuse[c])*scene.diffuse[c]*scene.diffuseIntensity*diffuse*((mask&4)?diffuse:1));
            closeEnough(result.pixel[3][c],double(material.specular[c])*scene.diffuse[c]*scene.diffuseIntensity*scene.specularScale*((mask&8)?diffuse:1));}
        assert(result.pixel[0][3]==material.diffuse[3]&&result.pixel[3][3]==24&&result.pixel[2][3]==environment);
        closeEnough(result.pixel[1][3],(24.+8.)/(8.*3.141592653589793));
        closeEnough(result.pixel[2][0],0);closeEnough(result.pixel[2][1],-.6);closeEnough(result.pixel[2][2],-.8);
    }
    const Matrix rotation{0,1,0,0,-1,0,0,0,0,0,1,0,0,0,0,1};
    const auto local=prepare_sps_game_lighting_values(scene,material,&rotation);assert(local.complete);
    closeEnough(local.pixel[2][0],.6);closeEnough(local.pixel[2][1],0);closeEnough(local.pixel[2][2],-.8);
    auto zero=scene;zero.direction={0,0,0};const auto zeros=prepare_sps_game_lighting_values(zero,material);assert(zeros.complete&&std::signbit(zeros.pixel[2][0]));
    auto invalid=scene;invalid.diffuse[0]=INFINITY;assert(!prepare_sps_game_lighting_values(invalid,material).complete);
    invalid=scene;invalid.direction={FLT_MAX,FLT_MAX,FLT_MAX};assert(!prepare_sps_game_lighting_values(invalid,material).complete);
    SpsGameParameterLayout layout;layout.vertex={0,1,2,3,4,5,6,7,8};layout.pixel={0,1,2,3,4,6};SpsGameParameters parameters(layout);
    assert(write_sps_game_lighting(parameters,local));auto images=parameters.take();
    assert(!std::memcmp(images[1][layout.pixel[0]].data(),local.pixel.data(),64));
    assert(!std::memcmp(images[0][layout.vertex[0]].data()+144,local.pixel[2].data(),16));
    Region<0xad0> sceneMemory;Region<0x538> materialMemory;Region<0x50> light;Region<8> lights;Region<0xc8> environment;
    float specularScale=1.5f;spans.push_back({reinterpret_cast<uintptr_t>(&specularScale),4});const auto image=reinterpret_cast<uintptr_t>(&specularScale)-0x19a16f0;
    sceneMemory.set(0x10c,scene.direction);sceneMemory.set(0x63c,scene.ambient);sceneMemory.set(0x928,lights.token());sceneMemory.set(0x930,lights.token()+8);
    sceneMemory.set(0xac0,environment.token());environment.set(0xc4,scene.environment);lights.set(0,light.token());
    const std::array<float,5> lightColors{scene.ambientIntensity,scene.diffuse[0],scene.diffuse[1],scene.diffuse[2],scene.diffuseIntensity};light.set(0x34,lightColors);
    materialMemory.set(0,material.flags);materialMemory.set(0x74,material.diffuse);materialMemory.set(0x84,material.ambient);
    materialMemory.set(0x94,material.specular);materialMemory.set(0xa4,material.emission);
    materialMemory.set(0x518,material.intensityOverride);materialMemory.set(0x534,material.specularPower);
    SpsGameLightingScene copiedScene;SpsGameMaterialColors copiedMaterial;
    assert(read_sps_game_lighting_scene(sceneMemory.token(),image,readable,copiedScene)&&read_sps_game_material_colors(materialMemory.token(),readable,copiedMaterial));
    const auto copied=prepare_sps_game_lighting_values(copiedScene,copiedMaterial,&rotation);assert(copied.complete&&copied.pixel==local.pixel);
    materialMemory.set(0x534,100.f);assert(copiedMaterial.specularPower==24); // Snapshot owns its source numbers.
    std::cout<<"SPS owned material lighting: complete ambient/diffuse/specular flag combinations, scene/environment values, local direction, shader ABI writes and invalid inputs; zero native cache/preparation calls passed\n";
}
}
int main(){run();return 0;}

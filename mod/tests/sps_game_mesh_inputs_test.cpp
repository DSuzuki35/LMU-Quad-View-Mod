#include "../sps_game_mesh_inputs.hpp"
#include <cassert>
#include <iostream>

using namespace lmuqv;
namespace {
std::vector<std::pair<uintptr_t,size_t>> memory;
bool readable(const void* p,size_t bytes){const auto address=reinterpret_cast<uintptr_t>(p);
    for(const auto& span:memory)if(address>=span.first&&address-span.first<=span.second&&bytes<=span.second-(address-span.first))return true;return false;}
template<size_t N>struct Region {
    std::array<uint8_t,N> bytes{};
    Region(){memory.push_back({token(),N});}
    uint64_t token()const{return reinterpret_cast<uintptr_t>(bytes.data());}
    template<class T>void set(size_t offset,const T& value){assert(offset<=N&&sizeof(T)<=N-offset);std::memcpy(bytes.data()+offset,&value,sizeof(T));}
    void pointer(size_t offset,uint64_t value){set(offset,value);}
    void integer(size_t offset,uint32_t value){set(offset,value);}
};
void run(){
    Region<0x1b4> source;Region<0xe0*2> meshes;Region<0x558> material;Region<0x160> effect;
    Region<0x40> vertices;Region<0x30> vertexDescriptor,indexDescriptor;Region<0x10> vertexBinding,indexBinding;
    Region<16> vsArray,psArray;Region<0x100> vsHandle,psHandle;Region<0x40> vsGroup,psGroup,vs0,vs1,ps0,ps1;
    const auto mesh=meshes.token()+0xe0;
    source.pointer(0x170,meshes.token());source.integer(0x168,2);source.integer(0x1a0,4);source.integer(0x1b0,9);
    source.pointer(0x130,0x1234);
    const auto set=[&](size_t offset,uint32_t value){meshes.integer(0xe0+offset,value);};
    const auto ptr=[&](size_t offset,uint64_t value){meshes.pointer(0xe0+offset,value);};
    set(0,0x8000);set(0x10,15);set(0x28,7);set(0x38,11);set(0x4c,4);set(0x48,31);
    ptr(0x80,source.token());ptr(0x20,vertices.token());ptr(0x30,indexDescriptor.token());ptr(0x40,material.token());
    vertices.integer(0x20,0x1000);vertices.pointer(0x38,vertexDescriptor.token());
    vertexDescriptor.integer(0x14,32);vertexDescriptor.pointer(0x20,0x1001);vertexDescriptor.pointer(0x28,vertexBinding.token());
    vertexBinding.pointer(0,0x1002);indexDescriptor.integer(0x1c,0x47);indexDescriptor.pointer(0x28,indexBinding.token());indexBinding.pointer(0,0x1003);
    material.pointer(0x550,effect.token());
    effect.pointer(0x130,vsArray.token());effect.pointer(0x138,vsArray.token()+8);effect.pointer(0x148,psArray.token());effect.pointer(0x150,psArray.token()+8);
    vsArray.pointer(0,vsHandle.token());psArray.pointer(0,psHandle.token());vsHandle.pointer(8,vsGroup.token());psHandle.pointer(8,psGroup.token());
    vsHandle.integer(0xf8,0x12f);vsGroup.pointer(0x30,vs0.token());vsGroup.pointer(0x38,vs1.token());
    psGroup.pointer(0x30,ps0.token());psGroup.pointer(0x38,ps1.token());
    vs0.pointer(0x28,0x2001);vs1.pointer(0x28,0x2002);ps0.pointer(0x28,0x3001);ps1.pointer(0x28,0x3002);
    auto input=read_sps_game_mesh(mesh,source.token(),0,readable);assert(input.complete&&input.mesh==mesh&&input.geometry==source.token());
    assert(input.localTransform&&input.owner==0x1234&&input.pass==31&&input.shaderFlags==0x12f);
    assert(input.vertexBuffer==0x1002&&input.indexBuffer==0x1003&&input.layout==0x1001&&input.vertexStride==32);
    assert(input.vertexShader==0x2001&&input.pixelShader==0x3001&&input.vertexParameters==vsHandle.token());
    assert(input.indexCount==15&&input.startIndex==20&&input.baseVertex==11&&input.topology==D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    assert(!input.skinned&&input.palette.empty()&&input.bytesRead<2048);
    set(0x4c,5);indexDescriptor.integer(0x1c,0x46);
    input=read_sps_game_mesh(mesh,source.token(),1,readable);assert(input.complete&&input.vertexShader==0x2002&&input.pixelShader==0x3002);
    assert(input.topology==D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP&&input.indexFormat==DXGI_FORMAT_R16_UINT);
    Region<0x30> pose;Region<256*8> bones;Region<3> mapping;Region<0x90> bone0,bone1;
    source.integer(0,0x4000);source.pointer(0x138,pose.token());pose.integer(0,0x1000);pose.pointer(0x28,bones.token());
    set(0x88,3);ptr(0x90,mapping.token());mapping.bytes={1,0,1};bones.pointer(0,bone0.token());bones.pointer(8,bone1.token());
    Matrix a{1,0,0,0,0,1,0,0,0,0,1,0,2,3,4,1},b=a;b[12]=5;bone0.set(0x4c,a);bone1.set(0x4c,b);
    input=read_sps_game_mesh(mesh,source.token(),0,readable);assert(input.complete&&input.skinned&&input.palette.size()==3);
    assert(input.palette[0]==b&&input.palette[1]==a&&input.palette[2]==b);
    // Owned values survive the synchronous input scope; no native finalizer,
    // upload, mesh callback, camera setter or renderer is linked or invoked.
    b[12]=8;bone1.set(0x4c,b);assert(input.palette[0][12]==5);
    pose.integer(0,0);assert(!read_sps_game_mesh(mesh,source.token(),0,readable).complete);pose.integer(0,0x1000);
    ptr(0xcc,0xdeadbeef);assert(!read_sps_game_mesh(mesh,source.token(),0,readable).complete);ptr(0xcc,0);
    set(0,0x10000000);assert(!read_sps_game_mesh(mesh,source.token(),0,readable).complete);set(0,0x8000);
    vertexBinding.bytes[0xc]=1;assert(!read_sps_game_mesh(mesh,source.token(),0,readable).complete);vertexBinding.bytes[0xc]=0;
    assert(!read_sps_game_mesh(mesh+1,source.token(),0,readable).complete);
    assert(!read_sps_game_mesh(mesh,source.token(),2,readable).complete);
    set(0x38,UINT_MAX);assert(!read_sps_game_mesh(mesh,source.token(),0,readable).complete);
    std::cout<<"SPS game mesh inputs: unfiltered source containment, list/strip ranges, geometry and shader handles, profile selection, owned skinning; zero native rendering/preparation passed\n";
}
}
int main(){run();return 0;}

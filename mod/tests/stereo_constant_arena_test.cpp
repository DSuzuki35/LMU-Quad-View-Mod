#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../stereo_constant_arena.hpp"
#include <d3dcompiler.h>
#include <cassert>
#include <iostream>
#include <thread>
using namespace lmuqv;
void run(bool ordinary){
    StereoComRef<ID3D11Device> d;StereoComRef<ID3D11DeviceContext> c;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,d.put(),&level,c.put())));
    StereoFrameIdentity key{1,2,3,400,GetCurrentThreadId()};StereoConstantArena arena;
    assert(arena.begin(c.get(),key,1024*1024,ordinary));if(ordinary)assert(!arena.uses_offsets());
    std::vector<std::vector<uint8_t>> values(300,std::vector<uint8_t>(16));std::vector<StereoConstantAllocation> allocations;
    for(uint32_t i=0;i<values.size();++i){std::memcpy(values[i].data(),&i,4);auto allocation=arena.add(values[i]);assert(allocation);allocations.push_back(allocation);}
    const auto duplicate=arena.add(values[37]);assert(duplicate.page==allocations[37].page&&duplicate.first==allocations[37].first);
    assert(!arena.resolve(duplicate).buffer&&!arena.begin(c.get(),key));
    assert(arena.seal()&&arena.map_count()==(arena.uses_offsets()?2:300));const auto generation=arena.generation();
    assert(arena.ready(key,generation)&&!arena.add(values[0]));
    const char* source="cbuffer C:register(b0){uint4 value;};RWStructuredBuffer<uint> O:register(u0);[numthreads(1,1,1)]void main(){O[value.x]=value.x+7;}";
    StereoComRef<ID3DBlob> code,error;assert(SUCCEEDED(D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main","cs_5_0",0,0,code.put(),error.put())));
    StereoComRef<ID3D11ComputeShader> cs;assert(SUCCEEDED(d->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,cs.put())));
    D3D11_BUFFER_DESC bd{};bd.ByteWidth=1200;bd.BindFlags=D3D11_BIND_UNORDERED_ACCESS;bd.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;bd.StructureByteStride=4;
    StereoComRef<ID3D11Buffer> output;assert(SUCCEEDED(d->CreateBuffer(&bd,nullptr,output.put())));D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};ud.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;ud.Buffer.NumElements=300;
    StereoComRef<ID3D11UnorderedAccessView> uav;assert(SUCCEEDED(d->CreateUnorderedAccessView(output.get(),&ud,uav.put())));
    auto* u=uav.get();c->CSSetShader(cs.get(),nullptr,0);c->CSSetUnorderedAccessViews(0,1,&u,nullptr);
    StereoComRef<ID3D11DeviceContext1> c1;if(arena.uses_offsets())assert(SUCCEEDED(c->QueryInterface(__uuidof(ID3D11DeviceContext1),reinterpret_cast<void**>(c1.put()))));
    for(const auto allocation:allocations){auto binding=arena.resolve(allocation);assert(binding.buffer);
        if(arena.uses_offsets())c1->CSSetConstantBuffers1(0,1,&binding.buffer,&binding.first,&binding.count);else c->CSSetConstantBuffers(0,1,&binding.buffer);
        c->Dispatch(1,1,1);}
    assert(arena.mark_submitted(key)&&!arena.ready(key,generation)&&!arena.mark_submitted(key));
    u=nullptr;c->CSSetUnorderedAccessViews(0,1,&u,nullptr);
    bd.Usage=D3D11_USAGE_STAGING;bd.BindFlags=bd.MiscFlags=bd.StructureByteStride=0;bd.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    StereoComRef<ID3D11Buffer> read;assert(SUCCEEDED(d->CreateBuffer(&bd,nullptr,read.put())));c->CopyResource(read.get(),output.get());D3D11_MAPPED_SUBRESOURCE map{};
    assert(SUCCEEDED(c->Map(read.get(),0,D3D11_MAP_READ,0,&map)));for(unsigned i=0;i<300;++i)assert(static_cast<const uint32_t*>(map.pData)[i]==i+7);c->Unmap(read.get(),0);
    auto invalid=duplicate;invalid.first=0xffffffffu;invalid.count=32;assert(!arena.resolve(invalid).buffer);
    bool foreign=true;std::thread worker([&]{foreign=arena.begin(c.get(),key);});worker.join();assert(!foreign);
    ++key.frame;assert(arena.begin(c.get(),key,1024,ordinary)&&!arena.resolve(duplicate).buffer&&!arena.ready(key,generation));
    assert(arena.add(values[0])&&arena.seal()&&arena.mark_submitted(key));++key.frame;
    assert(arena.begin(c.get(),key,16,ordinary));std::vector<uint8_t> large(32);assert(!arena.add(large)&&!arena.seal());arena.abort();
    assert(arena.begin(c.get(),key,1024,ordinary));std::vector<uint8_t> misaligned(17);assert(!arena.add(misaligned)&&!arena.seal());arena.abort();c->ClearState();
    std::cout<<"Constant arena WARP mode="<<(ordinary?"ordinary":"offsets")<<": 300 GPU-read payloads, dedup, bulk maps, stale/thread/lifecycle/budget guards passed\n";
}
int main(){run(false);run(true);}

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../sps_cpu_profile_hooks.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>

int main(){
    using namespace lmuqv;
    assert(MH_Initialize()==MH_OK);
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));
    assert(sps_cpu_hooks::configure(context,nullptr));
    D3D11_BUFFER_DESC desc{};desc.ByteWidth=64;desc.Usage=D3D11_USAGE_DYNAMIC;desc.BindFlags=D3D11_BIND_CONSTANT_BUFFER;desc.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
    ID3D11Buffer* buffer=nullptr;assert(SUCCEEDED(device->CreateBuffer(&desc,nullptr,&buffer)));
    spsCpuProfile.frame(true);for(unsigned i=0;i<16;++i)spsCpuProfile.frame(false);
    const auto csr=_mm_getcsr();
    {
        int view=0;SpsCpuPassScope pass(2,&view);SpsCpuRangeScope range(&view,31,0x51df1d);
        SpsCpuScope upload(SpsCpuWork::upload);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        assert(SUCCEEDED(context->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped))&&mapped.pData);
        static_cast<unsigned char*>(mapped.pData)[0]=42;context->Unmap(buffer,0);
        // This deliberately unbound draw produces no image; the adapter must
        // forward it once and observe the API invocation without reading state.
        context->Draw(3,0);
    }
    const auto sample=spsCpuProfile.sample(true,2,0);
    assert(sample.work[unsigned(SpsCpuWork::upload)].calls==1);
    assert(sample.work[unsigned(SpsCpuWork::map)].calls==1&&sample.work[unsigned(SpsCpuWork::unmap)].calls==1);
    assert(sample.work[unsigned(SpsCpuWork::draw)].calls==1&&_mm_getcsr()==csr);
    // RAII completion on exceptions must preserve the native failure.
    try {int view=0;SpsCpuPassScope pass(3,&view);SpsCpuRangeScope range(&view,31,0x51df1d);SpsCpuScope scope(SpsCpuWork::preparation);throw std::runtime_error("native");}
    catch(const std::runtime_error& e){assert(std::string(e.what())=="native");}
    assert(spsCpuProfile.samples(true,3)==1);
    sps_cpu_hooks::stop();assert(!spsCpuProfile.enabled()&&sps_cpu_hooks::installed==0);
    D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(buffer,0,D3D11_MAP_WRITE_DISCARD,0,&mapped)));context->Unmap(buffer,0);
    buffer->Release();context->Release();device->Release();assert(MH_Uninitialize()==MH_OK);
    std::cout<<"SPS CPU D3D11 adapter: WARP Map/Unmap/Draw forwarding, nested attribution, FP controls, exception completion and hook teardown passed\n";
}

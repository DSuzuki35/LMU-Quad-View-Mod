#include "../stereo_resource_trace.hpp"
#include <sstream>
// Integration coverage: losing outside-scope hooks, high SRV slots, source IDs,
// UAV bindings, or exact copy forwarding must fail these checks.
void resource_trace_test(
                         const std::filesystem::path& parent) {
    using namespace lmuqv;
    const auto directory=parent/L"resource-uses";
    assert(std::filesystem::create_directory(directory));
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context)));

    assert(stereo_audit_configure(context,true,directory.c_str(),log));
    ID3D11Texture2D *source=nullptr,*destination=nullptr,*staging=nullptr;
    D3D11_TEXTURE2D_DESC desc{};desc.Width=desc.Height=8;desc.ArraySize=desc.MipLevels=desc.SampleDesc.Count=1;
    desc.Format=DXGI_FORMAT_R32_UINT;desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_UNORDERED_ACCESS;
    assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&source)));
    assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&destination)));
    ID3D11RenderTargetView* target=nullptr;ID3D11ShaderResourceView* srv=nullptr;ID3D11UnorderedAccessView* uav=nullptr;
    assert(SUCCEEDED(device->CreateRenderTargetView(source,nullptr,&target)));
    assert(SUCCEEDED(device->CreateShaderResourceView(source,nullptr,&srv)));
    assert(SUCCEEDED(device->CreateUnorderedAccessView(destination,nullptr,&uav)));
    auto* code=compile("Texture2D<uint> input:register(t127); RWTexture2D<uint> output:register(u0); [numthreads(1,1,1)] void main(uint3 id:SV_DispatchThreadID){output[id.xy]=input[id.xy]+7;}","cs_5_0");
    ID3D11ComputeShader* cs=nullptr;assert(SUCCEEDED(device->CreateComputeShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&cs)));drop(code);
    const UINT clear[4]{35,35,35,35};ID3D11UnorderedAccessView* sourceUav=nullptr;assert(SUCCEEDED(device->CreateUnorderedAccessView(source,nullptr,&sourceUav)));
    (void)clear;std::array<UINT,64> pixels{};for(unsigned i=0;i<64;++i)pixels[i]=100+i;context->UpdateSubresource(source,0,nullptr,pixels.data(),8*sizeof(UINT),0);
    context->ClearState();
    // Noncapturing calls must not contribute records.
    context->CopyResource(destination,source);
    stereo_audit_frame(true);
    {StereoAuditScope scope(StereoAuditPass::focus0);StereoAuditGroup group(31);
        context->OMSetRenderTargets(1,&target,nullptr);const auto token=stereo_audit_native_pass_begin(nativeInfo());assert(token.entered);stereo_audit_native_pass_end(token);}
    const auto readsAfterNative=nativeReads;
    std::thread foreignThread([&]{context->CopyResource(destination,source);});foreignThread.join();
    ID3D11DeviceContext* deferred=nullptr;assert(SUCCEEDED(device->CreateDeferredContext(0,&deferred)));deferred->Draw(0,0);drop(deferred);
    context->OMSetRenderTargets(0,nullptr,nullptr);
    context->VSSetShaderResources(127,1,&srv);context->HSSetShaderResources(127,1,&srv);context->DSSetShaderResources(127,1,&srv);
    context->GSSetShaderResources(127,1,&srv);context->PSSetShaderResources(127,1,&srv);
    context->OMSetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,1,&uav,nullptr);
    context->Draw(0,0); // An observed binding is potential use, even with no work.
    context->OMSetRenderTargetsAndUnorderedAccessViews(0,nullptr,nullptr,0,1,&sourceUav,nullptr);
    context->ClearState();context->CopyResource(destination,source);
    D3D11_BOX box{1,2,0,3,4,1};context->CopySubresourceRegion(destination,0,4,5,0,source,0,&box);
    context->CSSetShader(cs,nullptr,0);context->CSSetShaderResources(127,1,&srv);context->CSSetUnorderedAccessViews(0,1,&uav,nullptr);context->Dispatch(1,1,1);
    context->ClearState();stereo_audit_frame_end();stereo_audit_frame(false);stereo_audit_frame(true);
    const auto captures=files(directory);assert(captures.size()==1);const auto json=read(captures[0]);
    const auto resourceStart=json.find("\"resource_uses\":{");assert(resourceStart!=std::string::npos);
    const auto trace=json.substr(resourceStart);
    assert(trace.find("\"coverage\":\"partial\"")!=std::string::npos&&trace.find("\"truncated\":false")!=std::string::npos);
    // Native scene/view/RTV identities precede the resource, giving source ID 4.
    assert(json.find("\"view\":3,\"resource\":4")!=std::string::npos);
    assert(trace.find("\"sequence\":3,\"frame\":0,\"slot\":-1,\"operation\":\"draw\"")!=std::string::npos);
    for(const char* stage:{"vs","hs","ds","gs","ps","cs"}) {
        const auto use="\"resource\":4,\"stage\":\""+std::string(stage)+"\",\"binding\":\"srv\",\"slot\":127,\"access\":\"potential_read\"";
        assert(trace.find(use)!=std::string::npos);
    }
    assert(trace.find("\"stage\":\"om\",\"binding\":\"uav\",\"slot\":0,\"access\":\"potential_read_write\"")!=std::string::npos);
    assert(trace.find("\"operation\":\"copy_resource\"")!=std::string::npos&&trace.find("\"operation\":\"copy_region\"")!=std::string::npos);
    assert(trace.find("\"destination_xyz\":[4,5,0],\"source_box\":[1,2,0,3,4,1]")!=std::string::npos);
    assert(trace.find("\"sequence\":6,\"frame\":0,\"slot\":-1,\"operation\":\"dispatch\"")!=std::string::npos);
    assert(count(trace,"\"operation\":")==4&&nativeReads==readsAfterNative);
    desc.BindFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    assert(SUCCEEDED(device->CreateTexture2D(&desc,nullptr,&staging)));context->CopyResource(staging,destination);
    D3D11_MAPPED_SUBRESOURCE mapped{};assert(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&mapped)));
    UINT pixel=0;std::memcpy(&pixel,mapped.pData,sizeof(pixel));assert(pixel==107);
    std::memcpy(&pixel,static_cast<char*>(mapped.pData)+5*mapped.RowPitch+4*sizeof(UINT),sizeof(pixel));assert(pixel==117);
    std::memcpy(&pixel,static_cast<char*>(mapped.pData)+7*mapped.RowPitch+7*sizeof(UINT),sizeof(pixel));assert(pixel==163);context->Unmap(staging,0);
    // Simulate identity-map allocation failure after GetShaderResources and
    // GetResource have both acquired references. The real WARP objects must
    // retain exactly their prior reference counts and bindings.
    context->VSSetShaderResources(127,1,&srv);
    auto refs=[](IUnknown* value){value->AddRef();return value->Release();};
    const auto resourceRefs=refs(source),viewRefs=refs(srv);
    StereoResourceTrace failedTrace;StereoResourceTrace::Event failedEvent;
    unsigned identityCalls=0;
    failedTrace.observe(failedEvent,context,D3D_FEATURE_LEVEL_11_0,[&](ID3D11Resource*)->uint32_t{++identityCalls;throw std::bad_alloc();});
    assert(identityCalls==1&&failedTrace.aborted()&&!failedTrace.available());
    assert(refs(source)==resourceRefs&&refs(srv)==viewRefs);
    ID3D11ShaderResourceView* stillBound=nullptr;context->VSGetShaderResources(127,1,&stillBound);assert(stillBound==srv);drop(stillBound);
    std::ostringstream failedJson;failedTrace.write(failedJson);assert(failedJson.str().find("\"aborted\":true")!=std::string::npos&&failedJson.str().find("\"events\":[]")!=std::string::npos);
    failedTrace.reset();assert(!failedTrace.aborted()&&failedTrace.available());
    context->ClearState();
    // Depth-only readonly views may read; a readonly depth aspect with a
    // writable stencil aspect still potentially writes the resource.
    for(unsigned variant=0;variant<3;++variant){
        D3D11_TEXTURE2D_DESC depthDesc{};depthDesc.Width=depthDesc.Height=8;depthDesc.ArraySize=depthDesc.MipLevels=depthDesc.SampleDesc.Count=1;
        depthDesc.Format=variant?DXGI_FORMAT_D24_UNORM_S8_UINT:DXGI_FORMAT_D32_FLOAT;depthDesc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        ID3D11Texture2D* depth=nullptr;ID3D11DepthStencilView* depthView=nullptr;
        assert(SUCCEEDED(device->CreateTexture2D(&depthDesc,nullptr,&depth)));
        D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};viewDesc.Format=depthDesc.Format;viewDesc.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        viewDesc.Flags=D3D11_DSV_READ_ONLY_DEPTH|(variant==2?D3D11_DSV_READ_ONLY_STENCIL:0);
        assert(SUCCEEDED(device->CreateDepthStencilView(depth,&viewDesc,&depthView)));context->OMSetRenderTargets(0,nullptr,depthView);
        StereoResourceTrace depthTrace;depthTrace.observe({},context,D3D_FEATURE_LEVEL_11_0,[](ID3D11Resource*){return 1u;});
        std::ostringstream depthJson;depthTrace.write(depthJson);
        const auto access=variant==1?"\"access\":\"potential_read_write\"":"\"access\":\"potential_read\"";assert(depthJson.str().find(access)!=std::string::npos);
        context->ClearState();drop(depthView);drop(depth);
    }
    // 820 submissions with 640 bindings exceed the global use budget before
    // the event budget. Truncation must be explicit, with no retained COM refs.
    std::array<ID3D11ShaderResourceView*,128> denseBindings{};denseBindings.fill(srv);
    context->VSSetShaderResources(0,128,denseBindings.data());context->HSSetShaderResources(0,128,denseBindings.data());
    context->DSSetShaderResources(0,128,denseBindings.data());context->GSSetShaderResources(0,128,denseBindings.data());context->PSSetShaderResources(0,128,denseBindings.data());
    StereoResourceTrace denseTrace;
    for(unsigned i=0;i<820;++i){assert(denseTrace.available());denseTrace.observe({},context,D3D_FEATURE_LEVEL_11_0,[](ID3D11Resource*){return 1u;});}
    assert(!denseTrace.available());
    struct PrefixBuffer:std::streambuf {
        std::string prefix;
        std::streamsize xsputn(const char* text,std::streamsize count) override {const auto keep=(std::min)(size_t(count),size_t(2048)-prefix.size());prefix.append(text,keep);return count;}
        int_type overflow(int_type value) override {if(!traits_type::eq_int_type(value,traits_type::eof())&&prefix.size()<2048)prefix.push_back(traits_type::to_char_type(value));return traits_type::not_eof(value);}
    } prefixBuffer;
    std::ostream prefixOutput(&prefixBuffer);denseTrace.write(prefixOutput);
    assert(prefixBuffer.prefix.find("\"uses_dropped\":512")!=std::string::npos&&prefixBuffer.prefix.find("\"events_dropped\":1")!=std::string::npos&&prefixBuffer.prefix.find("\"truncated\":true")!=std::string::npos);
    denseTrace.reset();assert(denseTrace.available());context->ClearState();
    // New capture resets IDs, events and sequence. Empty submissions reach the
    // event bound without taking resource references or overflowing storage.
    stereo_audit_frame(false);stereo_audit_frame(true);
    for(unsigned i=0;i<32770;++i)context->Draw(0,0);
    stereo_audit_frame(false);stereo_audit_frame(true);
    bool foundBound=false;for(const auto& file:files(directory)){const auto data=read(file);if(data.find("\"events_dropped\":2")!=std::string::npos){assert(count(data,"\"operation\":")==32768);assert(data.find("\"truncated\":true")!=std::string::npos);assert(data.find("\"sequence\":1,\"frame\":0,\"slot\":-1")!=std::string::npos);foundBound=true;}}
    assert(foundBound);stereo_audit_stop();
    drop(staging);drop(cs);drop(uav);drop(sourceUav);drop(srv);drop(target);drop(source);drop(destination);drop(context);drop(device);
}

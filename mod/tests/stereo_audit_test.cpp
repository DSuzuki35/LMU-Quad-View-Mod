#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../stereo_audit.hpp"
#include <d3dcompiler.h>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <locale>
#include <string>
#include <thread>
#include <vector>
namespace {
template<class T> void drop(T*& p){if(p){p->Release();p=nullptr;}}
std::vector<std::string> messages;
struct GroupedNumbers:std::numpunct<char> {char do_thousands_sep()const override{return ',';}std::string do_grouping()const override{return "\3";}};
void log(const char* s){messages.emplace_back(s);}
bool contains(const char* s){for(const auto& m:messages)if(m.find(s)!=std::string::npos)return true;return false;}
ID3DBlob* compile(const char* source,const char* target){ID3DBlob *code=nullptr,*errors=nullptr;const auto hr=D3DCompile(source,std::strlen(source),nullptr,nullptr,nullptr,"main",target,0,0,&code,&errors);if(FAILED(hr)&&errors)std::cerr<<static_cast<char*>(errors->GetBufferPointer());drop(errors);assert(SUCCEEDED(hr));return code;}
std::vector<std::filesystem::path> files(const std::filesystem::path& path){std::vector<std::filesystem::path> result;for(const auto& file:std::filesystem::directory_iterator(path))if(file.path().extension()==".json")result.push_back(file.path());return result;}
std::string read(const std::filesystem::path& p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};}
unsigned count(const std::string& s,const char* needle){unsigned n=0;size_t p=0;while((p=s.find(needle,p))!=std::string::npos){++n;p+=std::strlen(needle);}return n;}
struct NativeFixture {
    alignas(8) std::array<unsigned char,0xdc8> scene{};
    alignas(8) std::array<unsigned char,0x508> bucket{};
    alignas(8) std::array<unsigned char,0x4a8> item{};
    alignas(8) std::array<unsigned char,0xd4> mesh{};
    alignas(8) std::array<unsigned char,0x110> shader{};
    uint64_t bucketPointer=0,itemPointer=0,meshPointer=0;
    template<class T,size_t N> static void put(std::array<unsigned char,N>& bytes,size_t offset,T value){std::memcpy(bytes.data()+offset,&value,sizeof(value));}
    template<size_t N> static void vector(std::array<unsigned char,N>& bytes,size_t offset,uint64_t* pointer){
        const auto begin=reinterpret_cast<uint64_t>(pointer);put(bytes,offset,begin);put(bytes,offset+8,begin+8);put(bytes,offset+16,begin+8);
    }
    NativeFixture(){
        bucketPointer=reinterpret_cast<uint64_t>(bucket.data());itemPointer=reinterpret_cast<uint64_t>(item.data());meshPointer=reinterpret_cast<uint64_t>(mesh.data());
        vector(scene,0xdb0,&bucketPointer);vector(bucket,0x4f0,&itemPointer);vector(item,0x490,&meshPointer);
        put(mesh,0,uint32_t(0x400000));put(mesh,0xcc,uint64_t(1));put(shader,0xf8,uint32_t(0x8000));put(shader,0x108,uint64_t(1));
    }
    bool sameMemory(const NativeFixture& other)const{
        return scene==other.scene&&bucket==other.bucket&&item==other.item&&mesh==other.mesh&&shader==other.shader&&
            bucketPointer==other.bucketPointer&&itemPointer==other.itemPointer&&meshPointer==other.meshPointer;
    }
};
NativeFixture nativeFixture;
std::array<uint64_t,4096> repeatedMeshes{};
unsigned nativeReads=0;
bool testSuspension=false,suspensionProbed=false;
bool throwNativeReader=false;
bool nativeReadable(const void* p,size_t bytes){
    ++nativeReads;
    if(throwNativeReader)throw std::bad_alloc();
    if(testSuspension&&!suspensionProbed){
        suspensionProbed=true;
        const lmuqv::StereoNativePassInfo info{nativeFixture.scene.data(),nativeFixture.shader.data(),0x51df1d,0,false,nativeReadable};
        assert(!lmuqv::stereo_audit_native_pass_begin(info).entered);
        lmuqv::StereoNativePrepareInfo metadata{nativeFixture.shader.data(),0,false,nativeReadable};
        assert(!lmuqv::stereo_audit_native_prepare_begin(nativeFixture.mesh.data(),&metadata).entered);
    }
    const auto address=reinterpret_cast<uintptr_t>(p);
    const auto begin=reinterpret_cast<uintptr_t>(&nativeFixture);
    if(address>=begin&&address-begin<=sizeof(nativeFixture)&&bytes<=sizeof(nativeFixture)-(address-begin))return true;
    const auto extraBegin=reinterpret_cast<uintptr_t>(repeatedMeshes.data());
    return address>=extraBegin&&address-extraBegin<=sizeof(repeatedMeshes)&&bytes<=sizeof(repeatedMeshes)-(address-extraBegin);
}
lmuqv::StereoNativePassInfo nativeInfo(){return {nativeFixture.scene.data(),nativeFixture.shader.data(),0x51df1d,17,true,nativeReadable};}
}
int main(int argc,char** argv){
    using namespace lmuqv;
    std::locale::global(std::locale(std::locale::classic(),new GroupedNumbers));
    const bool quiet=argc>1&&!std::strcmp(argv[1],"--diagnostics-off");if(quiet)SetEnvironmentVariableA("LMU_QV_DIAGNOSTICS","0");
    const bool hardware=argc>1&&!std::strcmp(argv[1],"--hardware");
    bool exportShaders=false;for(int i=1;i<argc;++i)exportShaders|=!std::strcmp(argv[i],"--shader-export");
    bool proofRequested=false;for(int i=1;i<argc;++i)proofRequested|=!std::strcmp(argv[i],"--pair-proof");
    SetEnvironmentVariableA("LMU_QV_STEREO_SHADER_CAPTURE",exportShaders?"1":"0");
    SetEnvironmentVariableA("LMU_QV_STEREO_DRAW_PROOF",proofRequested?"1":"0");
    wchar_t executable[32768]{};assert(GetModuleFileNameW(nullptr,executable,32768));
    const auto directory=std::filesystem::path(executable).parent_path()/("stereo-audit-test-"+std::to_string(GetTickCount64()));assert(std::filesystem::create_directory(directory));
    assert(!stereo_audit_prepare(false,directory.c_str(),log));
    assert(!stereo_audit_configure(nullptr,false,directory.c_str(),log)&&!stereo_audit_enabled());
    auto info=nativeInfo();assert(!stereo_audit_native_pass_begin(info).entered&&nativeReads==0);
    if(!quiet){assert(!stereo_audit_configure(nullptr,true,directory.c_str(),log));assert(stereo_audit_prepare(true,directory.c_str(),log));}
    else assert(!stereo_audit_prepare(true,directory.c_str(),log));
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,hardware?D3D_DRIVER_TYPE_HARDWARE:D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    if(quiet){assert(!stereo_audit_configure(context,true,directory.c_str(),log));stereo_audit_frame(true);{StereoAuditScope pass(StereoAuditPass::focus0);StereoAuditGroup group(31);assert(!stereo_audit_native_pass_begin(info).entered);StereoNativePrepareInfo metadata{nativeFixture.shader.data(),0,true,nativeReadable};assert(!stereo_audit_native_prepare_begin(nativeFixture.mesh.data(),&metadata).entered);}assert(nativeReads==0);stereo_audit_frame_end();stereo_audit_stop();assert(files(directory).empty()&&messages.empty());drop(context);drop(device);std::cout<<"Stereo audit diagnostics-off: no hooks/capture/files/native reads\n";return 0;}
    assert(stereo_audit_enabled()&&contains("early device interception active"));
    auto* code=compile("cbuffer C:register(b2){float4 offset;} float4 main(float2 p:POSITION):SV_Position{return float4(p+offset.xy,0.5,1);}","vs_5_0");
    ID3D11VertexShader* vs=nullptr;assert(SUCCEEDED(device->CreateVertexShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&vs)));
    ID3D11InputLayout* layout=nullptr;D3D11_INPUT_ELEMENT_DESC element{"POSITION",0,DXGI_FORMAT_R32G32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0};assert(SUCCEEDED(device->CreateInputLayout(&element,1,code->GetBufferPointer(),code->GetBufferSize(),&layout)));drop(code);
    code=compile("float4 main():SV_Target{return float4(0,1,0,1);}","ps_5_0");ID3D11PixelShader* ps=nullptr;assert(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&ps)));drop(code);
    // xrCreateSession happens after the game's mesh layouts and some shader
    // objects exist. Confirming the same device must preserve that metadata.
    assert(stereo_audit_configure(context,true,directory.c_str(),log)&&contains("preserving early shader/layout definitions"));
    ID3D11Texture2D* texture=nullptr;D3D11_TEXTURE2D_DESC td{};td.Width=td.Height=8;td.ArraySize=td.MipLevels=td.SampleDesc.Count=1;td.Format=DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;
    assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&texture)));ID3D11RenderTargetView* rtv=nullptr;assert(SUCCEEDED(device->CreateRenderTargetView(texture,nullptr,&rtv)));
    ID3D11Buffer *vb=nullptr,*ib=nullptr,*cb=nullptr;const float vertices[]={-1,-1,-1,3,3,-1};const UINT indices[]={0,1,2};
    auto buffer=[&](UINT bytes,UINT bind,const void* src,ID3D11Buffer** out){D3D11_BUFFER_DESC d{};d.ByteWidth=bytes;d.BindFlags=bind;D3D11_SUBRESOURCE_DATA data{src,0,0};assert(SUCCEEDED(device->CreateBuffer(&d,src?&data:nullptr,out)));};
    buffer(sizeof(vertices),D3D11_BIND_VERTEX_BUFFER,vertices,&vb);buffer(sizeof(indices),D3D11_BIND_INDEX_BUFFER,indices,&ib);buffer(16,D3D11_BIND_CONSTANT_BUFFER,nullptr,&cb);
    context->OMSetRenderTargets(1,&rtv,nullptr);D3D11_VIEWPORT viewport{0,0,8,8,0,1};context->RSSetViewports(1,&viewport);
    const UINT stride=8,offset=0;context->IASetVertexBuffers(0,1,&vb,&stride,&offset);context->IASetIndexBuffer(ib,DXGI_FORMAT_R32_UINT,0);context->IASetInputLayout(layout);context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(vs,nullptr,0);context->PSSetShader(ps,nullptr,0);context->VSSetConstantBuffers(2,1,&cb);const float zero[4]{};context->UpdateSubresource(cb,0,nullptr,zero,0,0);
    int nativeMeshIdentity=0;
    auto draw=[&]{const auto native=stereo_audit_native_prepare_begin(&nativeMeshIdentity);context->UpdateSubresource(cb,0,nullptr,zero,0,0);stereo_audit_native_prepare_end(native);context->DrawIndexed(3,0,0);};
    draw();assert(files(directory).empty()); // All ordinary draws are forwarded.
    assert(!stereo_audit_native_pass_begin(info).entered&&nativeReads==0);
    ID3D11Query *query=nullptr,*timer=nullptr;D3D11_QUERY_DESC queryDesc{D3D11_QUERY_OCCLUSION,0};assert(SUCCEEDED(device->CreateQuery(&queryDesc,&query)));
    queryDesc.Query=D3D11_QUERY_TIMESTAMP_DISJOINT;assert(SUCCEEDED(device->CreateQuery(&queryDesc,&timer)));context->Begin(timer);context->Begin(query);
    for(unsigned frame=0;frame<2;++frame){
        stereo_audit_frame(true); // Holding the key must not stop/restart.
        for(unsigned pass=0;pass<4;++pass){StereoAuditScope scope{StereoAuditPass(pass)};StereoAuditGroup group(7);draw();}
        stereo_audit_frame_end();
        if(frame==0)context->End(query);
    }
    assert(contains("stereo_audit complete")&&files(directory).size()==1);
    auto first=read(files(directory)[0]);assert(count(first,"\"kind\":")==8&&count(first,"\"sha256\":\"")==2);
    assert(first.find("\"scene_frames\":2")!=std::string::npos&&first.find("\"rendering\":\"legacy\"")!=std::string::npos);
    assert(first.find("\"name\":\"offset\"")!=std::string::npos&&first.find("\"group\":7")!=std::string::npos&&first.find("\"layout_known\":1")!=std::string::npos);
    assert(first.find("\"early_device\":1")!=std::string::npos&&first.find("\"shader_definitions_dropped\":0")!=std::string::npos);
    // Timing-only and timing+occlusion scopes must be distinguished, including
    // scopes begun before a capture. Only the former permits private replay.
    assert(count(first,"\"native_mesh\":1")==8&&count(first,"\"active_queries\":2")==4&&count(first,"\"active_queries\":1")==4);
    assert(count(first,"\"active_query_mask\":10")==4&&count(first,"\"active_query_mask\":8")==4);context->End(timer);drop(timer);
    assert(first.find(proofRequested?"\"stereo_pair_proof\":{\"requested\":1":"\"stereo_pair_proof\":{\"requested\":0")!=std::string::npos);
    if(proofRequested)assert(first.find("\"status\":\"inconclusive\"")!=std::string::npos&&first.find("no eligible draw with safe query state")!=std::string::npos);drop(query);
    assert(count(first,"\"bytecode_file\":")==unsigned(exportShaders?2:0));
    unsigned bytecodeFiles=0;if(std::filesystem::exists(directory/L"stereo-shaders"))for(const auto& file:std::filesystem::directory_iterator(directory/L"stereo-shaders")){
        const auto data=read(file.path());assert(data.size()>4&&data.substr(0,4)=="DXBC");++bytecodeFiles;}
    assert(bytecodeFiles==unsigned(exportShaders?2:0));
    // Reproduce cache exhaustion using >1,024 real, distinct programs. Identical
    // CreateShader calls may return the same object, so merely repeating them
    // would not exercise the old capacity bug on every D3D11 implementation.
    std::vector<ID3D11PixelShader*> extra;
    for(unsigned i=0;i<1100;++i){
        const auto source="float4 main():SV_Target{return float4("+std::to_string(i+1)+".0/2048.0,1,0,1);}";
        code=compile(source.c_str(),"ps_5_0");ID3D11PixelShader* shader=nullptr;
        assert(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&shader)));extra.push_back(shader);drop(code);
    }
    // Create another object/program request containing the same DXBC. It must
    // not consume an extra unique-program slot (objects may be driver-deduped).
    code=compile("float4 main():SV_Target{return float4(1100.0/2048.0,1,0,1);}","ps_5_0");ID3D11PixelShader* duplicate=nullptr;
    assert(SUCCEEDED(device->CreatePixelShader(code->GetBufferPointer(),code->GetBufferSize(),nullptr,&duplicate)));drop(code);
    stereo_audit_frame(false);stereo_audit_frame(true);
    context->PSSetShader(duplicate,nullptr,0);
    {StereoAuditScope pass(StereoAuditPass::focus0);for(unsigned i=0;i<4100;++i)context->DrawIndexed(3,0,0);}
    context->PSSetShader(ps,nullptr,0);
    stereo_audit_frame_end();stereo_audit_frame(false);stereo_audit_frame(true); // Save the bounded capture early.
    assert(files(directory).size()==2);bool bounded=false;
    for(const auto& file:files(directory)){const auto data=read(file);if(data.find("\"reason\":\"hotkey\"")!=std::string::npos){assert(count(data,"\"kind\":")==4096&&data.find("\"dropped\":4")!=std::string::npos);
        assert(data.find("\"shader_definitions_dropped\":0")!=std::string::npos&&data.find("\"unique_programs\":1102")!=std::string::npos&&count(data,"\"sha256\":\"")==2);bounded=true;}}
    assert(bounded);
    // Native scope observation correlates packets/preparation/draw identities,
    // but only while the original group-31 call is on the capture owner thread.
    const auto fixtureBefore=nativeFixture;
    auto expectedFixture=fixtureBefore;
    ID3D11Texture2D *depthTexture=nullptr,*secondTexture=nullptr;
    auto depthDesc=td;depthDesc.Format=DXGI_FORMAT_D32_FLOAT;depthDesc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    ID3D11DepthStencilView* dsv=nullptr;ID3D11RenderTargetView* secondRtv=nullptr;
    assert(SUCCEEDED(device->CreateTexture2D(&depthDesc,nullptr,&depthTexture))&&SUCCEEDED(device->CreateDepthStencilView(depthTexture,nullptr,&dsv)));
    assert(SUCCEEDED(device->CreateTexture2D(&td,nullptr,&secondTexture))&&SUCCEEDED(device->CreateRenderTargetView(secondTexture,nullptr,&secondRtv)));
    stereo_audit_frame(false);stereo_audit_frame(true);
    StereoNativePassToken stale;
    {
        StereoAuditScope scope(StereoAuditPass::focus0);
        {StereoAuditGroup wrongGroup(7);assert(!stereo_audit_native_pass_begin(info).entered&&nativeReads==0);}
        StereoAuditGroup group(31);auto wrong=info;wrong.callerRva=0;
        assert(!stereo_audit_native_pass_begin(wrong).entered&&nativeReads==0);
        std::thread other([&]{assert(!stereo_audit_native_pass_begin(info).entered);StereoNativePrepareInfo metadata{nativeFixture.shader.data(),0,false,nativeReadable};assert(!stereo_audit_native_prepare_begin(nativeFixture.mesh.data(),&metadata).entered);stereo_audit_native_pass_end({true,0,0});});other.join();assert(nativeReads==0);
        ID3D11RenderTargetView* boundTargets[2]{rtv,secondRtv};context->OMSetRenderTargets(2,boundTargets,dsv);
        testSuspension=true;stale=stereo_audit_native_pass_begin(info);testSuspension=false;assert(stale.entered&&nativeReads>0&&suspensionProbed);
        context->OMSetRenderTargets(1,&rtv,nullptr);
        StereoNativePrepareInfo metadata{nativeFixture.shader.data(),0x80000000,true,nativeReadable};
        const auto preparation=stereo_audit_native_prepare_begin(nativeFixture.mesh.data(),&metadata);assert(preparation.entered);
        stereo_audit_native_prepare_end(preparation);context->DrawIndexed(3,0,0);
        stereo_audit_native_pass_end(stale);
        stereo_audit_native_pass_end(stale);stereo_audit_native_prepare_end(preparation);
        // Supplied low-31 flags bypass the shader OR and do not inspect its
        // callback field when the resulting flags omit bit 0x8000.
        metadata.suppliedFlags=1;metadata.reset=false;const auto beforeExplicitFlags=nativeReads;
        const auto explicitFlags=stereo_audit_native_prepare_begin(nativeFixture.mesh.data(),&metadata);
        stereo_audit_native_prepare_end(explicitFlags);assert(nativeReads==beforeExplicitFlags+3);
        assert(nativeFixture.sameMemory(fixtureBefore));
        // Preserve the first observed metadata, then fill the global packet
        // budget with duplicate identities. Exhaustion performs no native reads.
        repeatedMeshes.fill(reinterpret_cast<uint64_t>(nativeFixture.mesh.data()));
        const auto begin=reinterpret_cast<uint64_t>(repeatedMeshes.data());
        NativeFixture::put(nativeFixture.item,0x490,begin);NativeFixture::put(nativeFixture.item,0x498,begin+sizeof(repeatedMeshes));NativeFixture::put(nativeFixture.item,0x4a0,begin+sizeof(repeatedMeshes));
        // Account for only the three intentional test edits. Deriving the
        // expectation from the original prevents capture mutations being hidden.
        NativeFixture::put(expectedFixture.item,0x490,begin);NativeFixture::put(expectedFixture.item,0x498,begin+sizeof(repeatedMeshes));NativeFixture::put(expectedFixture.item,0x4a0,begin+sizeof(repeatedMeshes));
        for(unsigned i=0;i<8;++i){const auto token=stereo_audit_native_pass_begin(info);assert(token.entered);stereo_audit_native_pass_end(token);}
        const auto exhaustedReads=nativeReads;
        for(unsigned i=9;i<128;++i){const auto token=stereo_audit_native_pass_begin(info);assert(token.entered);stereo_audit_native_pass_end(token);}
        assert(nativeReads==exhaustedReads&&!stereo_audit_native_pass_begin(info).entered&&nativeReads==exhaustedReads);
    }
    assert(nativeFixture.sameMemory(expectedFixture));
    nativeFixture=fixtureBefore;
    stereo_audit_frame_end();stereo_audit_frame(false);stereo_audit_frame(true);
    assert(files(directory).size()==3);
    std::string nativeJson;
    for(const auto& file:files(directory)){const auto data=read(file);if(data.find("\"native_passes\":{")!=std::string::npos)nativeJson=data;}
    assert(!nativeJson.empty()&&nativeJson.find("\"native_mesh\":1")!=std::string::npos);
    assert(nativeJson.find("\"packets\":[{\"mesh\":1")!=std::string::npos);
    assert(nativeJson.find("\"shader_flags\":32768,\"effective_flags\":2147516416")!=std::string::npos);
    assert(nativeJson.find("\"shader_flags\":32768,\"effective_flags\":1,")!=std::string::npos);
    assert(nativeJson.find("\"mesh_callback\":true,\"special_0x400000\":true")!=std::string::npos);
    assert(nativeJson.find("\"width\":8,\"height\":8,\"format\":28,\"samples\":1")!=std::string::npos);
    assert(nativeJson.find("\"width\":8,\"height\":8,\"format\":40,\"samples\":1")!=std::string::npos);
    assert(count(nativeJson,"\"mesh_index\":")==32768&&count(nativeJson,"\"targets_begin\":")==128&&nativeJson.find("\"scopes_dropped\":1")!=std::string::npos);
    assert(nativeJson.find("\"begin_sequence\":1,\"end_sequence\":5")!=std::string::npos&&nativeJson.find("\"sequence\":4")!=std::string::npos);
    assert(nativeJson.find("does not preserve contents or native lifetimes")!=std::string::npos);
    drop(dsv);drop(depthTexture);drop(secondRtv);drop(secondTexture);
    // A normal close on another thread saves stable buffered records, without
    // D3D context calls there; hook/resource cleanup waits for the open scope.
    stereo_audit_frame(false);stereo_audit_frame(true);
    {StereoAuditScope scope(StereoAuditPass::focus0);StereoAuditGroup group(31);
        // A stale token must not close this generation's index-zero scope.
        auto invalid=info;invalid.readable=nullptr;const auto invalidToken=stereo_audit_native_pass_begin(invalid);assert(invalidToken.entered);stereo_audit_native_pass_end(stale);
        stereo_audit_native_pass_end(invalidToken);
        StereoNativePrepareInfo invalidMetadata{nativeFixture.shader.data(),0,false,nullptr};
        auto preparation=stereo_audit_native_prepare_begin(nativeFixture.mesh.data(),&invalidMetadata);stereo_audit_native_prepare_end(preparation);
        throwNativeReader=true;auto aborted=stereo_audit_native_pass_begin(info);throwNativeReader=false;assert(aborted.entered);stereo_audit_native_pass_end(aborted);
        const auto afterAbort=nativeReads;assert(!stereo_audit_native_pass_begin(info).entered&&nativeReads==afterAbort);
        draw();std::thread close([]{stereo_audit_stop();});close.join();assert(!stereo_audit_enabled());}
    assert(files(directory).size()==4&&contains("cleanup deferred"));draw();
    std::string invalidJson;for(const auto& file:files(directory)){const auto data=read(file);if(data.find("\"reason\":\"session_end\"")!=std::string::npos)invalidJson=data;}
    assert(invalidJson.find("\"aborted\":true")!=std::string::npos&&invalidJson.find("missing readable callback")!=std::string::npos);
    assert(invalidJson.find("\"begin_sequence\":1,\"end_sequence\":2")!=std::string::npos&&invalidJson.find("\"shader_flags\":null")!=std::string::npos);
    // Confirm capture hooks never changed the resulting image.
    auto readDesc=td;readDesc.BindFlags=0;readDesc.Usage=D3D11_USAGE_STAGING;readDesc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;ID3D11Texture2D* staging=nullptr;assert(SUCCEEDED(device->CreateTexture2D(&readDesc,nullptr,&staging)));
    context->CopyResource(staging,texture);D3D11_MAPPED_SUBRESOURCE map{};assert(SUCCEEDED(context->Map(staging,0,D3D11_MAP_READ,0,&map)));uint32_t pixel=0;std::memcpy(&pixel,static_cast<const char*>(map.pData)+4*map.RowPitch+4*4,4);assert(pixel==0xff00ff00u);context->Unmap(staging,0);drop(staging);
    stereo_audit_stop();context->ClearState();for(auto*& shader:extra)drop(shader);drop(duplicate);drop(cb);drop(ib);drop(vb);drop(rtv);drop(texture);drop(layout);drop(ps);drop(vs);drop(context);drop(device);
    std::cout<<"Stereo draw audit "<<(hardware?"hardware":"WARP")<<" passed: early device capture, preserved layouts, >1024 programs, deduplicated/used-only definitions, opt-in shader export, transparent draws, reflection/SHA, CB updates, frame limits, held key, file saving, pixel readback and cross-thread deferred cleanup\n";
}

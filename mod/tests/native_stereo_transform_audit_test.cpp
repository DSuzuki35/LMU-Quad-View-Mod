#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../stereo_audit.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <vector>
#include <thread>
#include <string>
using namespace lmuqv;
#if defined(LMU_STEREO_TRANSFORM_AUDIT)
namespace {
std::vector<std::pair<uintptr_t,size_t>> spans;
unsigned reads=0;
bool readable(const void* p,size_t n){++reads;const auto a=reinterpret_cast<uintptr_t>(p);for(auto s:spans)if(a>=s.first && a-s.first<=s.second && n<=s.second-(a-s.first))return true;return false;}
template<class A,class T> void put(A& a,size_t at,const T& value){assert(at+sizeof(value)<=sizeof(a));std::memcpy(reinterpret_cast<char*>(a.data())+at,&value,sizeof(value));}
}
#endif
int main(){
#if defined(LMU_STEREO_TRANSFORM_AUDIT)
    std::array<unsigned char,0x110> shader{};std::array<unsigned char,0xe0> mesh{};
    std::array<unsigned char,0x178> geometry{};std::array<unsigned char,0x318> owner{};
    std::array<unsigned char,0x3e0> view{};std::array<float,80> shadow{};
    const std::array<float,16> id{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    put(shader,8,uint64_t{0x100000000ull});put(shader,0xf8,uint32_t{1});
    put(mesh,0x80,reinterpret_cast<uintptr_t>(geometry.data()));put(geometry,0x130,reinterpret_cast<uintptr_t>(owner.data()));
    put(owner,0x298,id);put(view,0x1dc,id);put(view,0x3a0,id);
    put(shadow,0,id);put(shadow,64,id);put(shadow,256,id);
    const auto allow=[&](const auto& a){spans.emplace_back(reinterpret_cast<uintptr_t>(a.data()),sizeof(a));};
    allow(shader);allow(mesh);allow(geometry);allow(owner);allow(view);allow(shadow);
    const StereoNativeTransformInfo info{{shader.data(),view.data(),mesh.data(),shadow.data(),1,false},readable};
    auto helperInfo=info;helperInfo.args.suppliedFlags=0;
    helperInfo.args.boundary=NativeStereoTransformBoundary::Helper;
    put(shader,0xf8,uint32_t{0x12f});
    assert(!stereo_audit_native_transform_begin(info,0x1f80).entered && reads==0);
    ID3D11Device* device=nullptr;ID3D11DeviceContext* context=nullptr;D3D_FEATURE_LEVEL level{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)));
    const auto dir=std::filesystem::absolute("build")/("transform-audit-"+std::to_string(GetTickCount64()));
    std::filesystem::create_directories(dir);
    assert(stereo_audit_configure(context,true,dir.c_str(),nullptr));
    stereo_audit_native_transform_hook_available(true);
    assert(!stereo_audit_native_transform_begin(info,0x1f80).entered && reads==0);
    stereo_audit_frame(true);
    StereoNativeTransformToken stale;
    {
        StereoAuditScope scope(StereoAuditPass::focus0,view.data());StereoAuditGroup group(31);
        auto wrong=info;wrong.args.view=owner.data();
        assert(!stereo_audit_native_transform_begin(wrong,0x1f80).entered && reads==0);
        std::thread worker([&]{assert(!stereo_audit_native_transform_begin(info,0x1f80).entered);});worker.join();assert(reads==0);
        stale=stereo_audit_native_transform_begin(helperInfo,0x1fbf);assert(stale.entered);
        stereo_audit_native_transform_end(stale,0x1f80);
        auto n=reads;stereo_audit_native_transform_end(stale,0x1f80);assert(reads==n);
        auto mismatch=stereo_audit_native_transform_begin(helperInfo,0x1f80);shadow[0]=2;
        stereo_audit_native_transform_end(mismatch,0x1f80);shadow[0]=1;
        auto drift=stereo_audit_native_transform_begin(helperInfo,0x1f80);put(view,0x220,float{9});
        stereo_audit_native_transform_end(drift,0x1f80);put(view,0x220,float{0});
        auto unsupported=info;unsupported.args.suppliedFlags=8;
        auto rejected=stereo_audit_native_transform_begin(unsupported,0x1f80);assert(rejected.entered);
        stereo_audit_native_transform_end(rejected,0x1f80);
        for(unsigned mode=1;mode<=2;++mode){
            auto unsupportedHelper=helperInfo;
            unsupportedHelper.args.alternateOwner=mode==1;unsupportedHelper.args.skipEyeConstants=mode==2;
            const auto before=reads;auto t=stereo_audit_native_transform_begin(unsupportedHelper,0x1f80);
            assert(t.entered && reads==before);stereo_audit_native_transform_end(t,0x1f80);assert(reads==before);
        }
        // Existing preparation-boundary fixtures retain their stricter domain.
        auto legacy=stereo_audit_native_transform_begin(info,0x1f80);assert(legacy.entered);
        stereo_audit_native_transform_end(legacy,0x1f80);
        for(unsigned i=7;i<128;++i){auto t=stereo_audit_native_transform_begin(helperInfo,0x1f80);assert(t.entered);stereo_audit_native_transform_end(t,0x1f80);}
        n=reads;assert(!stereo_audit_native_transform_begin(info,0x1f80).entered && reads==n);
    }
    stereo_audit_frame(false);stereo_audit_frame(true); // export explicitly, not a measured frame
    std::string json;for(auto& e:std::filesystem::directory_iterator(dir))if(e.path().extension()==".json"){std::ifstream f(e.path());json={std::istreambuf_iterator<char>(f),{}};}
    assert(json.find("\"native_transforms\":")!=std::string::npos);
    assert(json.find("\"attempts\":128,\"dropped\":1,\"excluded_views\":1")!=std::string::npos);
    assert(json.find("transform match")!=std::string::npos && json.find("transform mismatch")!=std::string::npos);
    assert(json.find("transform inputs changed")!=std::string::npos && json.find("unsupported preparation")!=std::string::npos);
    assert(json.find("\"scheduling_eligible\":false")!=std::string::npos);
    assert(json.find("\"hook_available\":true")!=std::string::npos);
    assert(json.find("\"boundary\":\"transform_helper\"")!=std::string::npos && json.find("\"boundary\":\"preparation\"")!=std::string::npos);
    assert(json.find("unsupported transform helper mode")!=std::string::npos);
    assert(json.find("\"available\":124,\"focus_available\":124,\"refused\":4,\"mismatched_records\":1")!=std::string::npos);
    assert(json.find("\"first_expected_bits\":1065353216,\"first_observed_bits\":1073741824")!=std::string::npos);
    stereo_audit_frame(false);stereo_audit_frame(true);
    StereoNativeTransformToken abandoned;
    {StereoAuditScope scope(StereoAuditPass::focus0,view.data());StereoAuditGroup group(31);
      abandoned=stereo_audit_native_transform_begin(info,0x1f80);assert(abandoned.entered);
    }
    {StereoAuditScope scope(StereoAuditPass::focus0,view.data());StereoAuditGroup group(31);
      const auto n=reads;stereo_audit_native_transform_end(abandoned,0x1f80);assert(n==reads);
    }
    {StereoAuditScope scope(StereoAuditPass::focus0,view.data());StereoAuditGroup group(31);
      const auto n=reads;stereo_audit_native_transform_end(stale,0x1f80);assert(n==reads);
      auto pending=stereo_audit_native_transform_begin(info,0x1f80);assert(pending.entered);
      std::thread stop([]{stereo_audit_stop();});stop.join();
      const auto stopped=reads;stereo_audit_native_transform_end(pending,0x1f80);assert(stopped==reads);
    }
    const auto n=reads;assert(!stereo_audit_native_transform_begin(info,0x1f80).entered && reads==n);
    context->Release();device->Release();
    std::cout<<"Transform audit: matches, mismatches, drift, caps, opt-out and lifecycle passed\n";
#else
    std::cerr<<"FAIL: native transform audit integration missing\n";return 1;
#endif
}

#pragma once
#include "stereo_constants.hpp"
#include "xr_frame_inputs.hpp"

namespace lmuqv {
struct NativeStereoConstantRange {size_t buffer,data,bytes,slot;};
// Renderer-relative CPU shadows, not mapped GPU memory. Pinned native binders
// 0x5c3f20/0x5c3d70 and uploaders 0x5cac50/0x5ca8f0 establish these records.
inline constexpr std::array<NativeStereoConstantRange,9> nativeStereoVertexConstants{{
    {0x77fc8,0x77fd0,272,0x780e0},{0x780e8,0x780f0,2624,0x78b30},
    {0x78b38,0x78b40,2624,0x79580},{0x79588,0x79590,2688,0x7a010},
    {0x7a018,0x7a020,640,0x7a2a0},{0x7a2a8,0x7a2b0,544,0x7a4d0},
    {0x7a4d8,0x7a4e0,192,0x7a5a0},{0x7a5a8,0x7a5b0,64,0x7a5f0},
    {0x7a5f8,0x7a600,32,0x7a620}
}};
inline constexpr std::array<NativeStereoConstantRange,6> nativeStereoPixelConstants{{
    {0x7a628,0x7a630,288,0x7a750},{0x7a758,0x7a760,64,0x7a7a0},
    {0x7a7a8,0x7a7b0,1904,0x7af20},{0x7af28,0x7af30,32000,0x82c30},
    {0x82c38,0x82c40,32,0x82c60},{0x82c68,0x82c70,48,0x82ca0}
}};
struct NativeStereoConstantSnapshot {
    bool complete=false,renderingEligible=false;
    const char* reason="missing native constant inputs";
    size_t bytesRead=0;
    StereoFrameIdentity identity;
    std::array<StereoConstantBuffers,2> stages;
    // Immediate binding checks only. No GPU object is retained by these tokens.
    std::array<std::array<uint64_t,14>,2> bufferTokens{};
    unsigned vertexCommonSlot=14,pixelCommonSlot=14;
};
namespace native_constant_detail {
class Reader {
    bool(*readable_)(const void*,size_t);
    size_t& bytes_;size_t budget_;
public:
    Reader(bool(*readable)(const void*,size_t),size_t& bytes,size_t budget):readable_(readable),bytes_(bytes),budget_(budget){}
    bool copy(uintptr_t base,size_t offset,void* out,size_t bytes){
        const auto maximum=(std::numeric_limits<uintptr_t>::max)();
        if(!base||offset>maximum-base||bytes>maximum-(base+offset)||bytes>budget_-bytes_)return false;
        bytes_+=bytes;const auto* address=reinterpret_cast<const void*>(base+offset);
        if(!readable_||!readable_(address,bytes))return false;std::memcpy(out,address,bytes);return true;
    }
    template<class T> bool read(uintptr_t base,size_t offset,T& out){return copy(base,offset,&out,sizeof(out));}
};
}
// Call only on the renderer's owning thread at a stable preparation boundary.
// Copy selected ordinary CPU shadows; never Map/read GPU WRITE memory or call
// an engine method. This does not establish early right-eye material effects.
inline NativeStereoConstantSnapshot capture_native_stereo_constants(
    uintptr_t renderer,uintptr_t image,const StereoFrameIdentity& key,
    const std::array<uint32_t,2>& required,bool(*readable)(const void*,size_t),size_t budget=65536) noexcept {
    NativeStereoConstantSnapshot out;
    const auto fail=[&](const char* reason){NativeStereoConstantSnapshot empty;empty.reason=reason;empty.bytesRead=out.bytesRead;return empty;};
    if(!key.valid()||!renderer||!image||!readable||budget>65536||((required[0]|required[1])&~0x1fffu))return fail("invalid native constant scope");
    try {
        native_constant_detail::Reader reader(readable,out.bytesRead,budget);
        uint8_t rendererMode=0,pixelBypass=0;std::array<uint8_t,2> alternate{};
        if(!reader.read(renderer,0xa56,rendererMode)||!reader.read(image,0x1b1a58e,alternate)||!reader.read(image,0x1b1a7fd,pixelBypass))return fail("unreadable native constant mode");
        if(rendererMode||pixelBypass||alternate[0]||alternate[1])return fail("unsupported native constant mode");
        std::array<std::array<uint64_t,14>,2> buffers{};
        std::array<uint32_t,2> supplied{};
        const auto stage=[&](unsigned index,const auto& ranges){
            uint32_t seen=index?(1u<<5):0;
            for(size_t r=0;r<ranges.size();++r){const auto& range=ranges[r];unsigned slot=14;uint64_t buffer=0;
                if(!reader.read(renderer,range.slot,slot)||!reader.read(renderer,range.buffer,buffer)||slot>=13||(seen&(1u<<slot)))return false;
                seen|=1u<<slot;if(!r){if(index)out.pixelCommonSlot=slot;else out.vertexCommonSlot=slot;}
                // The VS common image is also needed to reconstruct transforms
                // and the pinned PS b5 alias, even if VS does not read that CB.
                const bool needed=(!index&&!r)||(required[index]&(1u<<slot));
                if(!needed)continue;if(!buffer)return false;
                auto& bytes=out.stages[index][slot];bytes.resize(range.bytes);
                if(!reader.copy(renderer,range.data,bytes.data(),bytes.size()))return false;
                buffers[index][slot]=buffer;supplied[index]|=1u<<slot;
            }
            return true;
        };
        if(!stage(0,nativeStereoVertexConstants)||!stage(1,nativeStereoPixelConstants))return fail("unavailable/aliased native constant layout");
        if(required[1]&(1u<<5)){
            out.stages[1][5]=out.stages[0][out.vertexCommonSlot];buffers[1][5]=buffers[0][out.vertexCommonSlot];supplied[1]|=1u<<5;
        }
        for(unsigned s=0;s<2;++s)if((supplied[s]&required[s])!=required[s])return fail("shader constant slot is outside native shadow layout");
        for(unsigned s=0;s<2;++s)for(unsigned slot=0;slot<14;++slot)if(buffers[s][slot])
            for(unsigned t=0;t<=s;++t)for(unsigned other=0;other<14;++other){
                if(t==s&&other>=slot)break;
                if(buffers[s][slot]==buffers[t][other]&&!(s==1&&slot==5&&t==0&&other==out.vertexCommonSlot))return fail("aliased native constant resources");
            }
        out.bufferTokens=buffers;out.identity=key;out.complete=true;out.reason="owned native CPU constant images";return out;
    }catch(...){return fail("native constant capture allocation/reader failure");}
}
struct NativeStereoDrawConstants {
    bool complete=false,renderingEligible=false;
    const char* reason="incomplete native draw constants";
    std::array<std::array<StereoConstantBuffers,2>,2> eyes;
};
// Actual ordinary native layout, not the legacy logical-register shadow:
// VS world c4->CB[0..3], derived c0->CB[4..7], position c8->CB[8],
// view c16->CB[10..13]. PS logical c15->primary CB[12]. PS b5 aliases VS common.
inline NativeStereoDrawConstants prepare_native_stereo_draw_constants(
    const std::array<NativeStereoTransformInput,2>& input,
    const std::array<NativeStereoConstantSnapshot,2>& base) noexcept {
    NativeStereoFpGuard preserve;
    const auto fail=[](const char* reason){NativeStereoDrawConstants out;out.reason=reason;return out;};
    if(!base[0].complete||!base[1].complete||!base[0].identity.valid()||base[0].identity!=base[1].identity)return fail("stale/incomplete native constant images");
    try {
        std::array<NativeStereoTransformResult,2> prepared;
        for(unsigned eye=0;eye<2;++eye){const auto& b=base[eye];
            if(b.vertexCommonSlot>=13||b.pixelCommonSlot>=13||b.pixelCommonSlot==5||b.stages[0][b.vertexCommonSlot].size()!=272||
               (!b.stages[1][b.pixelCommonSlot].empty()&&b.stages[1][b.pixelCommonSlot].size()!=288)||
               (!b.stages[1][5].empty()&&b.stages[1][5]!=b.stages[0][b.vertexCommonSlot]))return fail("native constant image shape/alias differs");
            prepared[eye]=prepare_native_stereo_transform(input[eye]);if(!prepared[eye].available)return fail(prepared[eye].reason);
        }
        NativeStereoDrawConstants out;
        for(unsigned eye=0;eye<2;++eye){const auto& b=base[eye];const auto& values=prepared[eye].constants;out.eyes[eye]=b.stages;
            auto& common=out.eyes[eye][0][b.vertexCommonSlot];
            std::memcpy(common.data(),values.world.data(),64);std::memcpy(common.data()+64,values.derived.data(),64);
            std::memcpy(common.data()+128,values.position.data(),16);std::memcpy(common.data()+160,values.view.data(),64);
            auto& pixel=out.eyes[eye][1][b.pixelCommonSlot];if(!pixel.empty())std::memcpy(pixel.data()+192,values.position.data(),16);
            if(!out.eyes[eye][1][5].empty())out.eyes[eye][1][5]=common;
        }
        out.complete=true;out.reason="owned native VS/PS ordinary transform constants";return out;
    }catch(...){return fail("native draw constant allocation failure");}
}
}

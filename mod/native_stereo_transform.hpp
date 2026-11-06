#pragma once
#include "native_stereo_packets.hpp"
#include "native_stereo_classify_capture.hpp"
#include <array>
#include <cmath>
#include <xmmintrin.h>

namespace lmuqv {
// Preserve the signed preparation result and the exact original FP environment.
// Diagnostic failures never retry/suppress the original; original exceptions propagate.
template<class Begin,class Native,class End>
int invoke_native_stereo_transform(Begin&& begin,Native&& native,End&& end,bool observe=true) {
    if(!observe)return native();
    const unsigned entry=_mm_getcsr();
    using Token=decltype(begin(entry));Token token{};
    {NativeStereoFpGuard guard;try{token=begin(entry);}catch(...){}}
    const int returned=native();
    const unsigned post=_mm_getcsr();
    {NativeStereoFpGuard guard;try{end(token,post);}catch(...){}}
    return returned;
}
// The verified 0x562aca caller consumes no return value from 0x565260.
// Forward its five arguments once and preserve FP state around diagnostics.
template<class Begin,class Native,class End>
void invoke_native_stereo_transform_helper(Begin&& begin,Native&& native,End&& end,bool observe=true) {
    if(!observe){native();return;}
    const unsigned entry=_mm_getcsr();
    using Token=decltype(begin(entry));Token token{};
    {NativeStereoFpGuard guard;try{token=begin(entry);}catch(...){}}
    native();
    const unsigned post=_mm_getcsr();
    {NativeStereoFpGuard guard;try{end(token,post);}catch(...){}}
}
using NativeStereoMatrix = std::array<float,16>;
struct NativeStereoTransformInput {
    NativeStereoMatrix world{}, derived{}, view{};
    std::array<float,3> position{};
    bool localTransform{};
};
struct NativeStereoTransformConstants {
    NativeStereoMatrix derived{}, world{}, view{};
    std::array<float,4> position{};
};
struct NativeStereoTransformResult {
    bool available{}, renderingEligible{};
    const char* reason = "unsupported floating-point controls";
    NativeStereoTransformConstants constants;
};
class NativeStereoTransformFpRestore {
    unsigned saved_ = _mm_getcsr();
public:
    ~NativeStereoTransformFpRestore(){_mm_setcsr(saved_);}
};
inline bool native_stereo_transform_default_controls(unsigned mxcsr) noexcept {
    return (mxcsr & 0xffc0u) == 0x1f80u;
}

// Owned numbers only. This reproduces the ordinary 0x565260 transform payload,
// not its native cache effects or any visibility, lifetime or scheduling decision.
// Row-major matrices, pairwise products, default binary32 controls; compile with
// -ffp-contract=off and no fast-math (MSVC /fp:strict). Preserve caller FP state.
inline NativeStereoTransformResult prepare_native_stereo_transform(
    const NativeStereoTransformInput& in) noexcept {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
    static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float)==4);
    NativeStereoTransformFpRestore restore;
    NativeStereoTransformResult out;
    if (!native_stereo_transform_default_controls(_mm_getcsr())) return out;
    const auto finite=[](const auto& values){for(float f:values)if(!std::isfinite(f))return false;return true;};
    if (!finite(in.world)||!finite(in.derived)||!finite(in.view)||!finite(in.position)) {
        out.reason="non-finite transform input"; return out;
    }
    NativeStereoTransformConstants constants;
    constants.world=in.world;
    constants.position={in.position[0],in.position[1],in.position[2],0};
    if (!in.localTransform) {
        constants.derived=in.derived; constants.view=in.view;
    } else {
        const auto multiply=[&](const NativeStereoMatrix& b,NativeStereoMatrix& result){
            for(size_t row=0;row<4;++row)for(size_t col=0;col<4;++col){
                const float p0=in.world[row*4]*b[col], p1=in.world[row*4+1]*b[4+col];
                const float p2=in.world[row*4+2]*b[8+col], p3=in.world[row*4+3]*b[12+col];
                if(!std::isfinite(p0)||!std::isfinite(p1)||!std::isfinite(p2)||!std::isfinite(p3))return false;
                const float a=p0+p1,bottom=p2+p3;
                if(!std::isfinite(a)||!std::isfinite(bottom))return false;
                const float value=a+bottom;
                if(!std::isfinite(value))return false;
                result[row*4+col]=value;
            }
            return true;
        };
        if(!multiply(in.derived,constants.derived)||!multiply(in.view,constants.view)) {
            out.reason="non-finite transform arithmetic"; return out;
        }
    }
    out.constants=constants; out.available=true; out.reason="complete";
    return out;
}

enum class NativeStereoTransformBoundary { Preparation, Helper };
struct NativeStereoTransformArgs {
    const void* shader{}; const void* view{}; const void* mesh{}; const void* vsShadow{};
    uint32_t suppliedFlags{}; bool reset{};
    // The Helper boundary is set only by the original helper hook. It does not
    // relax admission around the larger preparation wrapper.
    NativeStereoTransformBoundary boundary=NativeStereoTransformBoundary::Preparation;
    bool alternateOwner{},skipEyeConstants{};
};
inline constexpr size_t nativeStereoTransformInputByteLimit=244;
struct NativeStereoTransformSnapshot {
    bool complete{};
    const char* reason="incomplete transform input";
    size_t bytesRead{};
    // Ephemeral immediate-call identity checks only. Never dereference later or
    // serialize these values. Copies of numbers do not retain native objects.
    std::array<uintptr_t,6> identities{};
    uint32_t meshFlags{};
    NativeStereoTransformInput input;
};
inline NativeStereoTransformSnapshot capture_native_stereo_transform(
    const NativeStereoTransformArgs& args,bool(*readable)(const void*,size_t)) noexcept {
    NativeStereoTransformSnapshot out;
    if(args.boundary!=NativeStereoTransformBoundary::Preparation&&args.boundary!=NativeStereoTransformBoundary::Helper){
        out.reason="unknown transform boundary";return out;
    }
    const bool helper=args.boundary==NativeStereoTransformBoundary::Helper;
    if(helper&&(args.alternateOwner||args.skipEyeConstants)){
        out.reason="unsupported transform helper mode";return out;
    }
    if(!helper&&(args.reset || (args.suppliedFlags && args.suppliedFlags!=1))) {
        out.reason="unsupported preparation flags or reset";return out;
    }
    NativeStereoCaptureReason reason=NativeStereoCaptureReason::complete;
    try {
        native_stereo_detail::Reader reader(readable,reason,out.bytesRead);
        uint32_t shaderFlags=0,meshFlags=0;
        uint64_t shaderEnabled=0,callback=0;
        uintptr_t geometry=0,owner=0;
        const auto shader=reinterpret_cast<uintptr_t>(args.shader);
        const auto view=reinterpret_cast<uintptr_t>(args.view);
        const auto mesh=reinterpret_cast<uintptr_t>(args.mesh);
        if((!helper&&!reader.read(shader,0xf8,shaderFlags))||!reader.read(shader,8,shaderEnabled)) {
            out.reason=native_stereo_capture_reason(reason);return out;
        }
        const auto effective=args.suppliedFlags ? args.suppliedFlags : shaderFlags;
        if(!helper&&effective!=1){out.reason="unsupported preparation flags";return out;}
        if(!shaderEnabled){out.reason="disabled transform shader";return out;}
        if(!reader.read(mesh,0,meshFlags)||!reader.read(mesh,0xcc,callback)||!reader.read(mesh,0x80,geometry)) {
            out.reason=native_stereo_capture_reason(reason);return out;
        }
        if((meshFlags&0x100000u)||callback) {out.reason="orientation path or mesh callback";return out;}
        if(!reader.read(geometry,0x130,owner)) {out.reason=native_stereo_capture_reason(reason);return out;}
        NativeStereoTransformInput input;
        input.localTransform=(meshFlags&0x8000u)!=0;
        if(!reader.read(owner,0x298,input.world)||!reader.read(view,0x1dc,input.view)||
           !reader.read(view,0x220,input.position)||!reader.read(view,0x3a0,input.derived)) {
            out.reason=native_stereo_capture_reason(reason);return out;
        }
        out.input=input;out.meshFlags=meshFlags;
        out.identities={shader,view,mesh,geometry,owner,reinterpret_cast<uintptr_t>(args.vsShadow)};
        out.complete=true;out.reason="complete";
    } catch(...) {out.reason="transform reader exception";}
    return out;
}
struct NativeStereoTransformObserved {
    bool complete{};const char* reason="incomplete transform output";size_t bytesRead{};
    NativeStereoTransformConstants constants;
};
inline NativeStereoTransformObserved capture_native_stereo_transform_constants(
    const void* shadow,bool(*readable)(const void*,size_t)) noexcept {
    NativeStereoTransformObserved out;
    NativeStereoCaptureReason reason=NativeStereoCaptureReason::complete;
    try {
        native_stereo_detail::Reader reader(readable,reason,out.bytesRead);
        NativeStereoTransformConstants values;
        const auto base=reinterpret_cast<uintptr_t>(shadow);
        if(!reader.read(base,0,values.derived)||!reader.read(base,4*16,values.world)||
           !reader.read(base,8*16,values.position)||!reader.read(base,16*16,values.view)) {
            out.reason=native_stereo_capture_reason(reason);return out;
        }
        out.constants=values;out.complete=true;out.reason="complete";
    } catch(...) {out.reason="transform reader exception";}
    return out;
}
struct NativeStereoTransformComparison {
    bool available{};const char* reason="incomplete transform invocation";
    unsigned comparedScalars{},mismatchedScalars{};
    unsigned firstMismatchRegister=~0u,firstMismatchComponent=~0u;
    uint32_t firstExpectedBits{},firstObservedBits{};
};
inline NativeStereoTransformComparison compare_native_stereo_transform(
    const NativeStereoTransformSnapshot& pre,const NativeStereoTransformSnapshot& post,
    const NativeStereoTransformObserved& actual,unsigned entryMxcsr,unsigned postMxcsr) noexcept {
    NativeStereoTransformComparison out;
    if(!pre.complete){out.reason=pre.reason;return out;}
    if(!post.complete){out.reason=post.reason;return out;}
    if(!actual.complete){out.reason=actual.reason;return out;}
    if(!native_stereo_transform_default_controls(entryMxcsr)||!native_stereo_transform_default_controls(postMxcsr)) {
        out.reason="unsupported floating-point controls";return out;
    }
    // Compare copied bytes so this check cannot raise FP exceptions under an
    // arbitrary export environment, including signaling NaNs in refused inputs.
    const auto same=[](const auto& a,const auto& b){return std::memcmp(a.data(),b.data(),sizeof(a))==0;};
    if(pre.identities!=post.identities||pre.meshFlags!=post.meshFlags||
       pre.input.localTransform!=post.input.localTransform||!same(pre.input.world,post.input.world)||
       !same(pre.input.view,post.input.view)||!same(pre.input.derived,post.input.derived)||
       !same(pre.input.position,post.input.position)) {out.reason="transform inputs changed";return out;}
    NativeStereoTransformFpRestore restore;
    _mm_setcsr(0x1f80);
    const auto predicted=prepare_native_stereo_transform(pre.input);
    if(!predicted.available){out.reason=predicted.reason;return out;}
    const auto compare=[&](const auto& a,const auto& b,unsigned firstRegister){
        for(size_t i=0;i<a.size();++i){
            ++out.comparedScalars;
            if(std::memcmp(&a[i],&b[i],sizeof(float))!=0){
                if(!out.mismatchedScalars){
                    out.firstMismatchRegister=firstRegister+unsigned(i/4);out.firstMismatchComponent=unsigned(i%4);
                    std::memcpy(&out.firstExpectedBits,&a[i],sizeof(float));
                    std::memcpy(&out.firstObservedBits,&b[i],sizeof(float));
                }
                ++out.mismatchedScalars;
            }
        }
    };
    compare(predicted.constants.derived,actual.constants.derived,0);
    compare(predicted.constants.world,actual.constants.world,4);
    compare(predicted.constants.position,actual.constants.position,8);
    compare(predicted.constants.view,actual.constants.view,16);
    out.available=true;out.reason=out.mismatchedScalars?"transform mismatch":"transform match";
    return out;
}
} // namespace lmuqv

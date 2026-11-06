#pragma once
#include "native_caller_context.hpp"
#include "native_stereo_world_pose.hpp"

namespace lmuqv {
inline bool capture_native_stereo_cockpit_settings(const CONTEXT& caller,uintptr_t expectedView,
    uintptr_t expectedManager,bool(*readable)(const void*,size_t),NativeStereoCockpitViewSettings& out) noexcept {
    NativeStereoFpGuard preserve;
    if(!expectedView||!expectedManager||caller.R14!=expectedView||caller.R15!=expectedManager||
       caller.R12!=0||caller.R13!=1||!caller.Rsi||!readable||
       !native_stereo_transform_default_controls(_mm_getcsr()))return false;
    const auto copy=[&](uintptr_t base,size_t offset,void* result,size_t bytes){
        if(!base||offset>UINTPTR_MAX-base||bytes>UINTPTR_MAX-(base+offset)||
           !readable(reinterpret_cast<const void*>(base+offset),bytes))return false;
        std::memcpy(result,reinterpret_cast<const void*>(base+offset),bytes);return true;
    };
    try {
        int32_t camera=0;if(!copy(expectedManager,0xa0,&camera,4)||camera<0||camera>8||camera==4)return false;
        // Jump table 0xc8c8e8: regular source-camera pointers. Camera 4 uses
        // a separately indexed array, which this producer does not borrow.
        constexpr size_t sourceOffsets[]{0x38,0x40,0x48,0x50,0,0x58,0x60,0x68,0x70};
        uintptr_t source=0;if(!copy(expectedManager,sourceOffsets[camera],&source,8)||source!=caller.Rsi)return false;
        std::array<float,3> clipping{};NativeStereoCockpitViewSettings settings;
        if(!copy(source,0x74,clipping.data(),12)||!copy(expectedView,0x4c,&settings.anchor,8)||
           !copy(expectedView,0x10c,&settings.mask,4)||!copy(expectedView,0x3e8,&settings.mode,4))return false;
        const auto farZ=clipping[0],nearZ=clipping[1];settings.scale=clipping[2];
        if(!std::isfinite(farZ)||!std::isfinite(nearZ)||!(nearZ>0)||!(farZ>nearZ)||
           !std::isfinite(settings.scale)||!(settings.scale>0)||!std::isfinite(settings.anchor.x)||
           !std::isfinite(settings.anchor.y)||settings.mode<0||settings.mode>2)return false;
        // Camera mode 0 uses source far/near directly (0xc8c3bc..0xc8c44c).
        // 0x556d60 writes range=(far-near), anchor.z=range*0.5 in both eyes.
        settings.range=native_world_pose_detail::sub(farZ,nearZ);
        settings.anchor.z=native_world_pose_detail::mul(settings.range,.5f);
        if(!std::isfinite(settings.range)||!std::isfinite(settings.anchor.z))return false;
        settings.complete=true;out=settings;return true;
    }catch(...){return false;}
}

// Pinned cockpit-camera ABI at return RVA 0xc8c17a. This uses unwind-recovered
// nonvolatile registers, plus bounded copies of the still-live caller stack.
// It never saves a stack/native pointer into the owned result.
inline bool capture_native_stereo_pose_input(const CONTEXT& caller,const void* cameraState,
    uintptr_t expectedView,uintptr_t expectedManager,bool(*readable)(const void*,size_t),
    NativeStereoWorldPoseInput& out) noexcept {
    if(!expectedView||!expectedManager||caller.R14!=expectedView||caller.R15!=expectedManager||
       caller.R12!=0||caller.R13!=1||caller.Rsp>UINTPTR_MAX-0x668||caller.Rbp!=caller.Rsp+0x100||
       !cameraState||!readable)return false;
    const auto copy=[&](uintptr_t from,void* to,size_t bytes){
        if(!from||from>UINTPTR_MAX-bytes||!readable(reinterpret_cast<const void*>(from),bytes))return false;
        std::memcpy(to,reinterpret_cast<const void*>(from),bytes);return true;
    };
    NativeStereoWorldPoseInput input;
    if(!copy(caller.Rsp+0x50,input.origin.data(),12)||
       !copy(caller.Rbp-0x58,input.rotation.data(),16)||
       !copy(caller.Rbp+0x560,input.rotation.data()+8,4))return false;
    // The copy at rsp+0x70 belongs to the mutable head-angle matrix. Calls
    // 0x362800/0x3628d0 can already have changed it. The native position path
    // at 0xc8c201..0xc8c246 still uses the original four coefficients in XMM15.
    std::memcpy(input.rotation.data()+4,&caller.Xmm15,16);
    const uintptr_t state=reinterpret_cast<uintptr_t>(cameraState);
    if(state>UINTPTR_MAX-0x410||!copy(state+0x3e0,input.scalars.data(),48))return false;
    std::memcpy(&input.offset[0],&caller.Xmm6.Low,4);
    std::memcpy(&input.offset[1],&caller.Xmm7.Low,4);
    std::memcpy(&input.offset[2],&caller.Xmm8.Low,4);
    out=input;return true;
}
}

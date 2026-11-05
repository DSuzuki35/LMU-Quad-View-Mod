#pragma once
#include "native_caller_context.hpp"
#include "native_stereo_world_pose.hpp"

namespace lmuqv {
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
       !copy(caller.Rsp+0x70,input.rotation.data()+4,16)||
       !copy(caller.Rbp+0x560,input.rotation.data()+8,4))return false;
    const uintptr_t state=reinterpret_cast<uintptr_t>(cameraState);
    if(state>UINTPTR_MAX-0x410||!copy(state+0x3e0,input.scalars.data(),48))return false;
    std::memcpy(&input.offset[0],&caller.Xmm6.Low,4);
    std::memcpy(&input.offset[1],&caller.Xmm7.Low,4);
    std::memcpy(&input.offset[2],&caller.Xmm8.Low,4);
    out=input;return true;
}
}

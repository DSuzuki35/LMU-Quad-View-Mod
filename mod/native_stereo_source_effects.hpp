#pragma once
#include <algorithm>
#include <cstdint>

namespace lmuqv {
enum class NativeStereoSourceFinalization {Unknown,Noop,Native};
struct NativeStereoOwnerLightInputs {
    bool available=false;
    uint8_t flags=0;
    int32_t pointLimit=0,spotLimit=0;
    uint64_t pointSceneBegin=0,pointSceneEnd=0,spotSceneBegin=0,spotSceneEnd=0;
    uint64_t pointBegin=0,pointEnd=0,spotBegin=0,spotEnd=0;
};
// 0x5db880/0x5dbc30 always clear the owner's local vectors first. These gates
// establish when no light scan, heap update, distance-cache write or view/owner
// callback follows. Non-empty positive-limit paths still need native effects.
inline bool native_stereo_owner_lights_scalar_only(const NativeStereoOwnerLightInputs& in) noexcept {
    if(!in.available)return false;
    return (in.pointSceneBegin==in.pointSceneEnd||(in.flags&2u)||in.pointLimit==0)&&
           (in.spotSceneBegin==in.spotSceneEnd||(in.flags&4u)||in.spotLimit==0);
}
inline NativeStereoSourceFinalization native_stereo_source_finalization(uint64_t lazy,uint32_t flags) noexcept {
    return !lazy||(flags&0x1000u)?NativeStereoSourceFinalization::Noop:NativeStereoSourceFinalization::Native;
}
}

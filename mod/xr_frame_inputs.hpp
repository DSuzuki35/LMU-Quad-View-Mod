#pragma once
#include "openxr/openxr.h"
#include <array>
#include <cstdint>

namespace lmuqv {
// Values identify one rendered locate result. Handles are opaque identity
// tokens, never borrowed engine objects or permission to use runtime images.
struct StereoFrameIdentity {
    uint64_t session=0,frame=0,space=0;
    int64_t displayTime=0;
    uint32_t thread=0;
    bool valid()const{return session&&frame&&space&&displayTime&&thread;}
};
inline bool operator==(const StereoFrameIdentity& a,const StereoFrameIdentity& b){
    return a.session==b.session&&a.frame==b.frame&&a.space==b.space&&
        a.displayTime==b.displayTime&&a.thread==b.thread;
}
inline bool operator!=(const StereoFrameIdentity& a,const StereoFrameIdentity& b){return !(a==b);}
struct QuadFrameInputs {
    StereoFrameIdentity identity;
    std::array<XrView,4> views{}; // Flat copies; next chains are not retained.
};
}

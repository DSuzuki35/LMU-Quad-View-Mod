#pragma once
#include "native_stereo_focus_inputs.hpp"
#include "native_stereo_owner_plan.hpp"
#include "native_stereo_view_math.hpp"

namespace lmuqv {
// These positions have different native meanings. Never derive an unverified
// engine-world position from an XR tracking-space pose, or reuse one for all.
struct NativeStereoViewMetadata {
    bool complete=false,parentNull=true;
    StereoFrameIdentity identity;
    uint64_t scene=0,view=0;
    unsigned eye=2;
    uint32_t mask=0;
    NativeStereoClassifyFloat3 classifyPosition{},boundsPosition{},constantPosition{};
    float scale=1,range=0,bias=0;
    int32_t mode=0;
};
struct NativeStereoOwnedView {
    NativeStereoOwnerView owner;
    NativeStereoClassifyInput classification;
    NativeStereoTransformInput transform;
};
struct NativeStereoViewInputs {
    bool complete=false,renderingEligible=false;
    const char* reason="missing view metadata";
    StereoFrameIdentity identity;
    uint64_t scene=0;
    std::array<NativeStereoOwnedView,2> eyes{};
};
inline NativeStereoViewInputs prepare_native_stereo_view_inputs(const NativeStereoFocusInputs& focus,
    const std::array<NativeStereoViewMetadata,2>& metadata) noexcept {
    NativeStereoFpGuard preserve;
    const auto refuse=[](const char* reason){NativeStereoViewInputs out;out.reason=reason;return out;};
    if(!focus.complete||!focus.identity.valid())return refuse("missing focus cameras");
    NativeStereoViewInputs out;out.identity=focus.identity;
    const auto finite=[](const NativeStereoClassifyFloat3& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
    if((_mm_getcsr()&0xffc0u)!=0x1f80u||std::fegetround()!=FE_TONEAREST)return refuse("unsupported view FP controls");
    for(unsigned eye=0;eye<2;++eye){
        const auto& m=metadata[eye];
        if(!m.complete||m.identity!=focus.identity||m.eye!=eye||!m.parentNull||!m.scene||!m.view||
           (eye&&m.scene!=metadata[0].scene))return refuse("stale or incomplete per-eye native metadata");
        if(!finite(m.classifyPosition)||!finite(m.boundsPosition)||!finite(m.constantPosition)||
           !(m.scale>0)||!std::isfinite(m.scale)||m.range<0||!std::isfinite(m.range)||m.bias<0||!std::isfinite(m.bias))
            return refuse("invalid native view metadata");
        const std::array<float,3> position{m.classifyPosition.x,m.classifyPosition.y,m.classifyPosition.z};
        const std::array<float,3> constants{m.constantPosition.x,m.constantPosition.y,m.constantPosition.z};
        for(unsigned i=0;i<3;++i){uint32_t a=0,b=0;std::memcpy(&a,&position[i],4);std::memcpy(&b,&constants[i],4);
            if(a!=(b^0x80000000u))return refuse("native classifier/constant position relationship differs");}
        const auto geometry=prepare_native_stereo_view_geometry(focus.eyes[eye].current,focus.eyes[eye].projection);
        if(!geometry.complete)return refuse(geometry.reason);
        auto& view=out.eyes[eye];view.owner.mask=m.mask;view.owner.parentNull=m.parentNull;
        view.owner.bounds={true,m.boundsPosition,m.range,geometry.planes};
        view.classification.viewPosition=m.classifyPosition;view.classification.viewScale=m.scale;
        view.classification.bias=m.bias;view.classification.viewMode=m.mode;
        view.classification.secondaryViewPosition=m.boundsPosition;view.classification.secondaryViewRadius=m.range;
        view.classification.planes=geometry.planes;
        view.transform.view=focus.eyes[eye].current;view.transform.derived=geometry.derived;
        view.transform.position={m.constantPosition.x,m.constantPosition.y,m.constantPosition.z};
    }
    out.scene=metadata[0].scene;out.complete=true;out.reason="owned independent native view inputs";return out;
}
}

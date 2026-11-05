#pragma once
#include "native_stereo_view_inputs.hpp"
#include "native_stereo_view_inverse.hpp"

namespace lmuqv {
inline constexpr size_t nativeStereoViewObservationByteLimit=436;
struct NativeStereoViewObservation {
    bool complete=false;
    const char* reason="missing native view observation";
    size_t bytesRead=0;
    StereoFrameIdentity identity;
    uint64_t scene=0,view=0;
    unsigned eye=2;
    uint32_t mask=0;
    int32_t mode=0;
    float scale=0,range=0;
    Matrix current{},inverse{},projection{},derived{};
    NativeStereoClassifyFloat3 classifyPosition{},constantPosition{},boundsPosition{},anchor{};
    std::array<NativeStereoClassifyPlane,6> planes{};
};
inline NativeStereoViewObservation capture_native_stereo_view_observation(const StereoFrameIdentity& key,
    uint64_t scene,const void* pointer,bool(*readable)(const void*,size_t)) noexcept {
    NativeStereoViewObservation out;
    const auto fail=[&](const char* reason){NativeStereoViewObservation empty;empty.reason=reason;empty.bytesRead=out.bytesRead;return empty;};
    if(!key.valid()||!scene||!pointer||!readable)return fail("invalid native view scope");
    const auto base=reinterpret_cast<uintptr_t>(pointer);
    const auto copy=[&](size_t offset,auto& result){
        if(offset>UINTPTR_MAX-base||sizeof(result)>UINTPTR_MAX-(base+offset))return false;
        out.bytesRead+=sizeof(result);
        if(!readable(reinterpret_cast<const void*>(base+offset),sizeof(result)))return false;
        std::memcpy(&result,reinterpret_cast<const void*>(base+offset),sizeof(result));return true;
    };
    try {
        uint64_t parent=0;int32_t eye=-1;
        if(!copy(0x4b0,parent)||!copy(0x3e0,out.scene)||!copy(0x5cc,eye)||parent||out.scene!=scene||eye<0||eye>1)
            return fail("native view identity/eye differs");
        if(!copy(0x1dc,out.current)||!copy(0x320,out.inverse)||!copy(0x360,out.projection)||!copy(0x3a0,out.derived)||
           !copy(0x178,out.planes)||!copy(0x34,out.classifyPosition)||!copy(0x220,out.constantPosition)||
           !copy(0x58,out.boundsPosition)||!copy(0x4c,out.anchor)||!copy(0x10c,out.mask)||
           !copy(0x3e8,out.mode)||!copy(0x114,out.scale)||!copy(0x100,out.range))return fail("unreadable native view values");
        out.identity=key;out.view=base;out.eye=unsigned(eye);out.complete=true;out.reason="owned native view observation";return out;
    }catch(...){return fail("native view reader failure");}
}

class NativeStereoViewInputFrame {
    NativeStereoViewInputs inputs_;
    NativeStereoFocusInputs focus_;
    std::array<NativeStereoViewMetadata,2> metadata_{};
    NativeStereoClassifyFloat3 anchor_{};
    uint64_t scene_=0,view_=0;
    unsigned observed_=0;
    const char* reason_="no native view inputs";
public:
    void reset(){inputs_={};focus_={};metadata_={};anchor_={};scene_=view_=0;observed_=0;reason_="no native view inputs";}
    bool prepare(const NativeStereoFocusInputs& focus,const NativeStereoWorldPosePair& pose,uint64_t scene,uint64_t view){
        reset();const auto fail=[&](const char* why){reason_=why;return false;};
        if(!focus.complete||!focus.nativeWorldPose||!pose.complete||!pose.settings.complete||!scene||!view)
            return fail("native cockpit view settings/world pose unavailable");
        const auto& settings=pose.settings;
        for(unsigned eye=0;eye<2;++eye){auto& m=metadata_[eye];
            if(!native_world_pose_detail::same(focus.eyes[eye].current,pose.current[eye]))return fail("native focus/world pose differs");
            m.identity=focus.identity;m.scene=scene;m.view=view;m.eye=eye;m.mask=settings.mask;
            m.mode=settings.mode;m.scale=settings.scale;m.range=settings.range;
            m.constantPosition={pose.constantPosition[eye][0],pose.constantPosition[eye][1],pose.constantPosition[eye][2]};
            // This sign-bit relationship is written by both native pose setter
            // and derived update. Bounds position has a separate inverse path.
            uint32_t bits[3]{};std::memcpy(bits,&m.constantPosition,12);for(auto& b:bits)b^=0x80000000u;
            std::memcpy(&m.classifyPosition,bits,12);
            if(!native_stereo_bounds_position(focus.eyes[eye].current,settings.anchor,m.boundsPosition))
                return fail("native inverse/bounds position unavailable");
            m.complete=true;
        }
        inputs_=prepare_native_stereo_view_inputs(focus,metadata_);
        if(!inputs_.complete)return fail(inputs_.reason);
        focus_=focus;scene_=scene;view_=view;anchor_=settings.anchor;
        reason_="paired native view inputs await original left comparison";return true;
    }
    bool observe(const NativeStereoViewObservation& native){
        const auto fail=[&](const char* why){inputs_={};reason_=why;return false;};
        if(!inputs_.complete||!native.complete||native.identity!=inputs_.identity||native.scene!=scene_||native.view!=view_||
           native.eye>1||observed_!=(native.eye?1u:0u))return fail("native view comparison order/identity differs");
        const auto eye=native.eye;const auto& expected=metadata_[eye];const auto& focus=focus_.eyes[eye];
        const auto same=[](const auto& a,const auto& b){return native_world_pose_detail::same(a,b);};
        if(native.mask!=expected.mask||native.mode!=expected.mode||!same(native.scale,expected.scale)||
           !same(native.range,expected.range)||!same(native.anchor,anchor_))return fail("native shared camera controls differ");
        Matrix inverse;if(!native_stereo_view_inverse(focus.current,inverse))return fail("native inverse unavailable");
        if(!same(native.current,focus.current)||!same(native.inverse,inverse)||!same(native.projection,focus.projection)||
           !same(native.derived,inputs_.eyes[eye].transform.derived)||!same(native.planes,inputs_.eyes[eye].classification.planes)||
           !same(native.constantPosition,expected.constantPosition)||!same(native.classifyPosition,expected.classifyPosition)||
           !same(native.boundsPosition,expected.boundsPosition))return fail("native per-eye view values differ");
        observed_|=1u<<eye;reason_=observed_==3?"both original native views match":"original left view matches; right comparison pending";return true;
    }
    const NativeStereoViewInputs* inputs(const StereoFrameIdentity& key)const{
        return inputs_.complete&&observed_&&inputs_.identity==key?&inputs_:nullptr;
    }
    bool accepts(const StereoFrameIdentity& key)const{return inputs_.complete&&inputs_.identity==key;}
    bool both_observed()const{return inputs_.complete&&observed_==3;}
    const char* reason()const{return reason_;}
};
}

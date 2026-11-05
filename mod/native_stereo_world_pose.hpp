#pragma once
#include "native_stereo_camera.hpp"
#include "native_stereo_transform.hpp"

namespace lmuqv {
// Numeric inputs at the cockpit camera's second VR-state read (0xc8c174),
// before either eye translation is added. The nine rotation coefficients are
// grouped by output world component; they are not a view matrix or XR pose.
struct NativeStereoWorldPoseInput {
    std::array<float,3> origin{},offset{},viewTranslation{};
    std::array<float,9> rotation{};
    std::array<std::array<float,6>,2> scalars{};
};
struct NativeStereoWorldPosePair {
    bool complete=false;
    const char* reason="missing native world pose";
    std::array<Matrix,2> current{};
    std::array<std::array<float,3>,2> constantPosition{};
};
namespace native_world_pose_detail {
inline float add(float a,float b){return _mm_cvtss_f32(_mm_add_ss(_mm_set_ss(a),_mm_set_ss(b)));}
inline float sub(float a,float b){return _mm_cvtss_f32(_mm_sub_ss(_mm_set_ss(a),_mm_set_ss(b)));}
inline float mul(float a,float b){return _mm_cvtss_f32(_mm_mul_ss(_mm_set_ss(a),_mm_set_ss(b)));}
template<class T> bool same(const T& a,const T& b){return !std::memcmp(&a,&b,sizeof(T));}
inline bool positions(const NativeStereoWorldPoseInput& in,std::array<std::array<float,3>,2>& out){
    using native_camera_detail::finite;
    if(!finite(in.origin)||!finite(in.offset)||!finite(in.viewTranslation)||!finite(in.rotation)||
       !finite(in.scalars[0])||!finite(in.scalars[1]))return false;
    // The native Euler path is shared only when all three selected head angles
    // have identical bits. Different/canted eye rotations need their own model.
    for(unsigned i=0;i<3;++i)if(!same(in.scalars[0][i],in.scalars[1][i]))return false;
    for(unsigned eye=0;eye<2;++eye){
        const std::array<float,3> p{add(in.offset[0],in.scalars[eye][3]),
            sub(in.offset[1],in.scalars[eye][4]),sub(in.offset[2],in.scalars[eye][5])};
        if(!finite(p))return false;
        out[eye]=in.origin;
        // Native zero-offset branch skips all rotation/addition operations.
        if(p[0]!=0||p[1]!=0||p[2]!=0)for(unsigned col=0;col<3;++col){
            const float xy=add(mul(in.rotation[col*3+1],p[1]),mul(in.rotation[col*3],p[0]));
            const float xyz=add(xy,mul(in.rotation[col*3+2],p[2]));
            out[eye][col]=add(in.origin[col],xyz);
        }
        // 0x5577a0 always adds the view's position offset, including zero.
        // Retaining that addition matters for signed-zero cached positions.
        for(unsigned col=0;col<3;++col)out[eye][col]=add(out[eye][col],in.viewTranslation[col]);
        if(!finite(out[eye]))return false;
    }
    return true;
}
inline Matrix set_position(Matrix m,const std::array<float,3>& p){
    // Ordinary mode-1 setter 0x58bbf0: (y*row1 + x*row0) + z*row2.
    for(unsigned col=0;col<3;++col)
        m[12+col]=add(add(mul(p[1],m[4+col]),mul(p[0],m[col])),mul(p[2],m[8+col]));
    m[15]=1;return m;
}
}

// Renderer-thread storage. Tokens are compared only; no retained native object
// is dereferenced. The original camera setter must finish before sealing.
class NativeStereoWorldPoseFrame {
    NativeStereoWorldPoseInput input_{};
    NativeStereoWorldPosePair pair_{};
    std::array<std::array<float,3>,2> positions_{};
    uintptr_t view_=0;
    bool pending_=false,consumed_=false;
public:
    void reset(){input_={};pair_={};positions_={};view_=0;pending_=consumed_=false;}
    bool begin(uintptr_t view,const NativeStereoWorldPoseInput& input){
        NativeStereoFpGuard preserve;
        reset();if(!view){pair_.reason="missing native view";return false;}
        if(!native_stereo_transform_default_controls(_mm_getcsr())){pair_.reason="unsupported world-pose FP controls";return false;}
        if(!native_world_pose_detail::positions(input,positions_)){pair_.reason="unsupported native world-pose inputs";return false;}
        input_=input;view_=view;pending_=true;return true;
    }
    bool finish(uintptr_t view,const Matrix& current,const std::array<float,3>& position,
        int32_t mode,uint32_t transformFlags){
        NativeStereoFpGuard preserve;
        const auto fail=[&](const char* reason){pending_=false;pair_={};pair_.reason=reason;return false;};
        if(!pending_||view!=view_)return fail("native pose setter order/view mismatch");
        pending_=false;
        if(!native_stereo_transform_default_controls(_mm_getcsr())||mode!=1||(transformFlags&2u)||
           !native_camera_detail::rigid(current))return fail("unsupported native pose transform");
        if(!native_world_pose_detail::same(position,positions_[0])||
           !native_world_pose_detail::same(current,native_world_pose_detail::set_position(current,position)))
            return fail("native left pose does not match produced position");
        pair_.constantPosition=positions_;pair_.current[0]=current;
        pair_.current[1]=native_world_pose_detail::set_position(current,positions_[1]);
        if(!native_camera_detail::finite(pair_.current[1]))return fail("non-finite right native pose");
        pair_.complete=true;pair_.reason="native cockpit world pose produced before left rendering";return true;
    }
    NativeStereoWorldPosePair take(uintptr_t view,const NativeStereoCameraSnapshot& camera){
        if(consumed_||!pair_.complete||view!=view_||!camera.complete||camera.eye!=0||camera.cameraMode!=0||
           camera.transformMode!=1||!native_world_pose_detail::same(camera.current,pair_.current[0])||
           !native_world_pose_detail::same(camera.scalars,input_.scalars))return {};
        for(float f:camera.translation)if(f!=0)return {};
        for(float f:camera.euler)if(f!=0)return {};
        consumed_=true;return pair_;
    }
    const char* reason()const{return pair_.reason;}
};
}

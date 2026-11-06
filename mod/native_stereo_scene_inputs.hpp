#pragma once
#include "native_stereo_owner_inputs.hpp"
#include "native_stereo_scene_plan.hpp"

namespace lmuqv {
struct NativeStereoSourceFinalizerInput {
    bool required=false;
    uint32_t flags=0;
    uint64_t token=0; // Immediate stability check only, not an owned engine object.
};
struct NativeStereoSceneInputCapture {
    bool complete=false,renderingEligible=false;
    const char* reason="missing native scene producer scope";
    size_t bytesRead=0;
    NativeStereoSceneInput input;
    std::vector<NativeStereoSourceFinalizerInput> finalizers;
};
struct NativeStereoOwnerBias {
    bool available=false,clamp=false;
    float root=0,floor=0;
};

// Native 0x5e1140 forwards the effective root bias through every child. The
// optional clamp uses MAXSS(floor, argument), retaining its signed-zero rule.
inline bool native_stereo_owner_bias(const NativeStereoOwnerBias& input,float& bias) noexcept {
    NativeStereoFpGuard preserve;
    if(!input.available||!std::isfinite(input.root)||(input.clamp&&!std::isfinite(input.floor)))return false;
    bias=input.clamp?_mm_cvtss_f32(_mm_max_ss(_mm_set_ss(input.floor),_mm_set_ss(input.root))):input.root;
    return std::isfinite(bias)&&bias>=0;
}

// At an unfiltered root entry, before either eye's admission: read all numeric
// source fields from their real objects, not from a late visible mesh list.
// Both current views, native root bias and verified bucket ordering are explicit.
// Source tokens are consumed only inside this call; returned packets own numbers.
inline NativeStereoSceneInputCapture capture_native_stereo_scene_inputs(
    const void* root,uint32_t globalMask,const NativeStereoViewInputs& views,
    const NativeStereoOwnerBias& bias,const std::vector<NativeStereoBucketOrder>& buckets,
    bool(*readable)(const void*,size_t),NativeStereoOwnerInputLimits limits={}) noexcept {
    NativeStereoFpGuard preserve;NativeStereoSceneInputCapture out;
    const auto fail=[&](const char* reason){NativeStereoSceneInputCapture empty;empty.reason=reason;empty.bytesRead=out.bytesRead;return empty;};
    float effectiveBias=0;
    if(!views.complete||!views.identity.valid()||!views.scene||!native_stereo_owner_bias(bias,effectiveBias)||
       buckets.empty()||buckets.size()>nativeStereoSceneBucketLimit||!readable)return fail("invalid native scene producer scope");
    for(auto order:buckets)if(order!=NativeStereoBucketOrder::Append&&order!=NativeStereoBucketOrder::NearToFar&&order!=NativeStereoBucketOrder::FarToNear)
        return fail("unverified native bucket ordering");
    try {
        auto owners=capture_native_stereo_owner_inputs(root,globalMask,readable,limits);
        out.bytesRead=owners.bytesRead;if(!owners.complete)return fail(owners.reason);
        const char* reason="incomplete native source numbers";
        native_owner_input_detail::Reader reader(readable,out.bytesRead,limits.bytes,reason);
        auto& input=out.input;input.identity=views.identity;input.scene=views.scene;input.views=views;
        input.buckets=buckets;input.owners.globalMask=globalMask;
        input.owners.owners=std::move(owners.owners);input.world=std::move(owners.world);
        input.owners.worldInputs=std::move(owners.worldInputs);
        for(unsigned eye=0;eye<2;++eye)input.views.eyes[eye].classification.bias=effectiveBias;
        input.sources.reserve(owners.sources.size());out.finalizers.reserve(owners.sources.size());
        for(const auto& description:owners.sources){
            NativeStereoSceneSource source;source.id=description.id;source.owner=description.owner;
            auto& candidate=source.candidate;candidate.candidateFlags=description.flags;
            const auto geometry=description.geometryToken;uint64_t position=0;
            if(!reader.read(geometry,0xdc,candidate.radius)||!reader.read(geometry,0xfc,candidate.nearCoefficient)||
               !reader.read(geometry,0x100,candidate.farCoefficient)||!reader.read(geometry,0x104,candidate.planeCount)||
               !reader.read(geometry,0x110,position)||!reader.read(geometry,0xe0,source.localPosition)||
               !reader.read(geometry,0xec,source.sortingPosition)||!reader.read(geometry,0x140,source.bucket)||
               !reader.read(owners.ownerTokens[source.owner],0xec,candidate.ownerScale))return fail(reason);
            if(source.bucket>=buckets.size()||candidate.planeCount<0||candidate.planeCount>6)return fail("unsupported native source bucket/planes");
            if(geometry>(std::numeric_limits<uint64_t>::max)()-0xec)return fail("native source position overflow");
            if(position==geometry+0xec){source.positionKind=NativeStereoPositionKind::SortingPoint;candidate.position=source.sortingPosition;}
            else{
                source.positionKind=NativeStereoPositionKind::IndependentPoint;
                if(!reader.read(position,0,candidate.position))return fail(reason);
            }
            for(uint32_t m=0;m<description.meshCount;++m)source.meshes.push_back(description.firstMesh+m);
            NativeStereoSourceFinalizerInput finalizer;finalizer.token=description.lazyStateToken;
            if(finalizer.token){
                if(!reader.read(finalizer.token,0,finalizer.flags))return fail(reason);
                finalizer.required=(finalizer.flags&0x1000u)==0;
            }
            source.finalization=native_stereo_source_finalization(finalizer.token,finalizer.flags);
            input.sources.push_back(std::move(source));out.finalizers.push_back(finalizer);
        }
        input.lights.resize(input.owners.owners.size());
        for(size_t i=0;i<input.lights.size();++i){
            auto& light=input.lights[i];const auto owner=owners.ownerTokens[i];uint64_t scene=0;
            if(!reader.read(owner,0x3f8,scene)||scene!=views.scene)return fail("native owner light scene differs");
            int32_t pointSelected=0,pointMaximum=0,spotSelected=0,spotMaximum=0;
            if(!reader.read(owner,0x420,light.flags)||!reader.read(owner,0x3c0,light.pointBegin)||
               !reader.read(owner,0x3c8,light.pointEnd)||!reader.read(owner,0x3d8,light.spotBegin)||!reader.read(owner,0x3e0,light.spotEnd)||
               !reader.read(scene,0x6c0+4*(light.flags&1u),pointSelected)||!reader.read(scene,0x908,pointMaximum)||
               !reader.read(scene,0x6c8+4*(light.flags&1u),spotSelected)||!reader.read(scene,0x90c,spotMaximum)||
               !reader.read(scene,0x948,light.pointSceneBegin)||!reader.read(scene,0x950,light.pointSceneEnd)||
               !reader.read(scene,0x968,light.spotSceneBegin)||!reader.read(scene,0x970,light.spotSceneEnd))return fail(reason);
            light.pointLimit=(std::min)(pointSelected,pointMaximum);light.spotLimit=(std::min)(spotSelected,spotMaximum);
            light.available=true;
        }
        out.complete=true;out.reason="owned native root, source numbers, world inputs and exact bias";return out;
    }catch(const std::bad_alloc&){return fail("native scene producer allocation failure");}
     catch(...){return fail("native scene producer reader failure");}
}
}

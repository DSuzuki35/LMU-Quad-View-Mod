#pragma once
#include "native_stereo_view_inputs.hpp"
#include <unordered_map>

namespace lmuqv {
inline constexpr size_t nativeStereoSceneMeshLimit=32768,nativeStereoSceneBucketLimit=256;
enum class NativeStereoBucketOrder {Append,NearToFar,FarToNear};
enum class NativeStereoPositionKind {SortingPoint,IndependentPoint};
struct NativeStereoSceneSource {
    uint32_t id=0,owner=0,bucket=0;
    NativeStereoClassifyInput candidate;
    NativeStereoClassifyFloat3 localPosition{},sortingPosition{};
    NativeStereoPositionKind positionKind=NativeStereoPositionKind::SortingPoint;
    std::vector<uint32_t> meshes; // Owned IDs, not engine mesh-array pointers.
};
struct NativeStereoSceneInput {
    StereoFrameIdentity identity;
    uint64_t scene=0;
    NativeStereoOwnerPlanInput owners;
    NativeStereoViewInputs views;
    std::vector<NativeStereoMatrix> world;
    std::vector<NativeStereoSceneSource> sources;
    // Native comparator/order must be identified by the producer. There is no
    // default inference from a material ID or the other eye's visible list.
    std::vector<NativeStereoBucketOrder> buckets;
};
struct NativeStereoScenePacket {
    uint32_t source=0,mesh=0,owner=0,bucket=0;
    float squaredDistance=0;
    uint64_t key()const{return (uint64_t(source)<<32)|mesh;}
};
enum class NativeStereoSceneEffectKind {SourceFlags,PositionCache,FinalizeSource,OwnerSubmission};
struct NativeStereoSceneEffect {
    unsigned eye=0;
    NativeStereoSceneEffectKind kind{};
    uint32_t owner=0,source=0,flags=0;
};
struct NativeStereoScenePlan {
    bool complete=false,renderingEligible=false;
    const char* reason="incomplete scene input";
    StereoFrameIdentity identity;
    NativeStereoOwnerPlanResult owners;
    std::array<std::vector<NativeStereoScenePacket>,2> eyes;
    std::vector<NativeStereoSceneEffect> effects;
    std::vector<uint32_t> finalSourceFlags;
    size_t positionCalculations=0;
};

// Restricted immutable-input scene preparation. The returned effects describe
// native work whose implementation/ownership is still required; this function
// does not call it, retain native objects, or authorize suppressing any draw.
inline NativeStereoScenePlan prepare_native_stereo_scene(const NativeStereoSceneInput& in) noexcept {
    NativeStereoFpGuard preserve;
    const auto refuse=[](const char* reason){NativeStereoScenePlan out;out.reason=reason;return out;};
    if(!in.identity.valid()||!in.views.complete||in.views.identity!=in.identity||!in.scene||in.views.scene!=in.scene)
        return refuse("stale or incomplete scene/view identity");
    if((_mm_getcsr()&0xffc0u)!=0x1f80u||std::fegetround()!=FE_TONEAREST)return refuse("unsupported scene FP controls");
    if(in.sources.size()>nativeStereoOwnerSourceLimit||in.world.size()!=in.owners.owners.size()||
       in.buckets.size()>nativeStereoSceneBucketLimit)return refuse("scene input limits");
    try {
        auto ownerInput=in.owners;for(unsigned eye=0;eye<2;++eye)ownerInput.views[eye]=in.views.eyes[eye].owner;
        auto ownerPlan=build_native_stereo_owner_plan(ownerInput);if(!ownerPlan.complete)return refuse("owner admission unavailable");
        std::unordered_map<uint32_t,size_t> sourceIndex;size_t meshCount=0;
        const auto finite=[](const NativeStereoClassifyFloat3& p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);};
        for(size_t i=0;i<in.sources.size();++i){const auto& s=in.sources[i];
            if(s.owner>=ownerInput.owners.size()||s.bucket>=in.buckets.size()||!sourceIndex.emplace(s.id,i).second)
                return refuse("invalid/duplicate scene source");
            if(s.meshes.size()>nativeStereoSceneMeshLimit-meshCount)return refuse("scene mesh limit");meshCount+=s.meshes.size();
            auto ids=s.meshes;std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return refuse("duplicate source mesh");
            if(!finite(s.localPosition)||!finite(s.sortingPosition)||!finite(s.candidate.position))return refuse("non-finite source position");
            if(s.positionKind!=NativeStereoPositionKind::SortingPoint&&s.positionKind!=NativeStereoPositionKind::IndependentPoint)
                return refuse("unknown source position kind");
            // View-dependent owner refresh/special emission is not bypassed.
            if(ownerInput.owners[s.owner].state.flags&(0x21000000u|0x102000u|0x40000u))return refuse("native source preparation path required");
            for(float value:in.world[s.owner])if(!std::isfinite(value))return refuse("non-finite owner world matrix");
        }
        size_t referenced=0;
        for(size_t owner=0;owner<ownerInput.owners.size();++owner)for(auto id:ownerInput.owners[owner].sourceIds){
            const auto found=sourceIndex.find(id);if(found==sourceIndex.end()||in.sources[found->second].owner!=owner)return refuse("source/owner correspondence differs");++referenced;}
        if(referenced!=in.sources.size())return refuse("unreferenced scene source");
        for(auto order:in.buckets)if(order!=NativeStereoBucketOrder::Append&&order!=NativeStereoBucketOrder::NearToFar&&order!=NativeStereoBucketOrder::FarToNear)
            return refuse("unsupported bucket ordering");
        NativeStereoScenePlan out;out.identity=in.identity;out.owners=std::move(ownerPlan);
        out.finalSourceFlags.reserve(in.sources.size());for(const auto& s:in.sources)out.finalSourceFlags.push_back(s.candidate.candidateFlags);
        std::vector<NativeStereoClassifyFloat3> positions;positions.reserve(in.sources.size());for(const auto& s:in.sources)positions.push_back(s.sortingPosition);
        out.effects.reserve(in.sources.size()*8+ownerInput.owners.size()*2);
        const auto point=[&](const NativeStereoClassifyFloat3& p,const NativeStereoMatrix& m,NativeStereoClassifyFloat3& result){
            float values[3]{};
            for(unsigned col=0;col<3;++col){const float x=p.x*m[col],y=p.y*m[4+col],z=p.z*m[8+col];
                const float xy=y+x,zw=z+m[12+col],value=xy+zw;
                if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(xy)||!std::isfinite(zw)||!std::isfinite(value))return false;values[col]=value;}
            result={values[0],values[1],values[2]};return true;
        };
        for(unsigned eye=0;eye<2;++eye){
            std::vector<std::vector<std::pair<size_t,float>>> buckets(in.buckets.size());
            const auto& traversal=out.owners.eyes[eye];
            for(size_t at=0;at<traversal.ownerOrder.size();++at){const auto owner=traversal.ownerOrder[at];const auto& state=traversal.emissionStates[at];bool appended=false;
                for(auto id:ownerInput.owners[owner].sourceIds){const auto index=sourceIndex.at(id);const auto& source=in.sources[index];auto& flags=out.finalSourceFlags[index];
                    const auto write=[&](uint32_t value){if(flags!=value)out.effects.push_back({eye,NativeStereoSceneEffectKind::SourceFlags,owner,id,value});flags=value;};
                    if(state.flags&0x40000000u)write(flags&~0x8040u);
                    if(!(flags&0x8000u)){
                        if(!point(source.localPosition,in.world[owner],positions[index]))return refuse("non-finite source transform");
                        ++out.positionCalculations;write(flags|0x8000u);
                        out.effects.push_back({eye,NativeStereoSceneEffectKind::PositionCache,owner,id,flags});
                    }
                    auto candidate=source.candidate;const auto& view=in.views.eyes[eye].classification;
                    candidate.candidateFlags=flags;candidate.ownerFlags=state.flags;
                    if(source.positionKind==NativeStereoPositionKind::SortingPoint)candidate.position=positions[index];
                    candidate.viewPosition=view.viewPosition;candidate.viewScale=view.viewScale;candidate.bias=view.bias;
                    candidate.planes=view.planes;candidate.viewMode=view.viewMode;
                    candidate.secondaryViewPosition=view.secondaryViewPosition;candidate.secondaryViewRadius=view.secondaryViewRadius;
                    const auto classification=classify_native_stereo_numbers(candidate);if(!classification.available)return refuse("source classification unavailable");
                    write(classification.storedCandidateFlags);
                    if((classification.returnedFlags&0x20000u)&&!(state.flags&0x80000000u))return refuse("native intersection preparation required");
                    if(flags&0x1000000u){write(flags|0x40000u);continue;}
                    if(flags&0x50000u)continue;
                    out.effects.push_back({eye,NativeStereoSceneEffectKind::FinalizeSource,owner,id,flags});
                    if(flags&0x220u)continue;
                    const auto& p=positions[index];const auto& v=view.viewPosition;
                    const float dx=p.x-v.x,dy=p.y-v.y,dz=p.z-v.z;
                    const float xx=dx*dx,yy=dy*dy,zz=dz*dz,yx=yy+xx,distance=yx+zz;
                    if(!std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(dz)||!std::isfinite(xx)||!std::isfinite(yy)||
                       !std::isfinite(zz)||!std::isfinite(yx)||!std::isfinite(distance))return refuse("non-finite sorting distance");
                    buckets[source.bucket].push_back({index,distance});appended=true;
                }
                if(appended&&!state.attachmentPresent)out.effects.push_back({eye,NativeStereoSceneEffectKind::OwnerSubmission,owner,0,0});
            }
            out.eyes[eye].reserve(meshCount);
            for(size_t b=0;b<buckets.size();++b){auto& items=buckets[b];const auto order=in.buckets[b];
                if(order!=NativeStereoBucketOrder::Append){
                    std::sort(items.begin(),items.end(),[&](const auto& a,const auto& c){return order==NativeStereoBucketOrder::NearToFar?a.second<c.second:a.second>c.second;});
                    for(size_t i=1;i<items.size();++i)if(items[i-1].second==items[i].second)return refuse("native distance tie ordering not established");
                }
                for(const auto& item:items){const auto& source=in.sources[item.first];for(auto mesh:source.meshes)
                    out.eyes[eye].push_back({source.id,mesh,source.owner,uint32_t(b),item.second});}
            }
        }
        out.complete=true;out.reason="owned independent source classification/order; native effects still required";return out;
    }catch(const std::bad_alloc&){return refuse("scene preparation allocation failure");}
     catch(...){return refuse("scene preparation failure");}
}
}

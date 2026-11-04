#pragma once
#include "native_stereo_owner_plan.hpp"
#include "native_stereo_transform.hpp"
#include "xr_frame_inputs.hpp"
#include <unordered_set>

namespace lmuqv {
inline constexpr size_t nativeStereoOwnerMeshLimit=32768;
inline constexpr size_t nativeStereoOwnerInputByteLimit=8*1024*1024;
struct NativeStereoOwnerInputLimits {
    size_t owners=nativeStereoOwnerNodeLimit,sources=nativeStereoOwnerSourceLimit;
    size_t meshes=nativeStereoOwnerMeshLimit,bytes=nativeStereoOwnerInputByteLimit;
};
struct NativeStereoOwnerMeshInput {
    uint32_t source=0,index=0,flags=0;
    uint64_t geometryToken=0,callbackToken=0; // Identity only; never dereferenced later.
};
struct NativeStereoOwnerSourceInput {
    uint32_t id=0,owner=0,flags=0,firstMesh=0,meshCount=0;
    uint64_t geometryToken=0,arrayToken=0,lazyStateToken=0; // Not retained engine objects.
};
struct NativeStereoOwnerInputs {
    bool complete=false;
    const char* reason="incomplete owner inputs";
    size_t bytesRead=0;
    uint32_t globalMask=0;
    // Entire unfiltered tree, including branches rejected by the first eye.
    std::vector<NativeStereoOwner> owners;
    std::vector<NativeStereoMatrix> world;
    std::vector<uint64_t> ownerTokens; // Ephemeral identity keys, no lifetime claim.
    std::vector<NativeStereoOwnerSourceInput> sources;
    std::vector<NativeStereoOwnerMeshInput> meshes;
    bool renderingEligible=false;
};
namespace native_owner_input_detail {
class Reader {
    bool(*readable_)(const void*,size_t);
    size_t& bytes_;size_t limit_;
    const char*& reason_;
public:
    Reader(bool(*readable)(const void*,size_t),size_t& bytes,size_t limit,const char*& reason)
        :readable_(readable),bytes_(bytes),limit_(limit),reason_(reason){}
    template<class T> bool read(uint64_t base,size_t offset,T& value){
        if(!base){reason_="missing native input";return false;}
        const auto maximum=(std::numeric_limits<uintptr_t>::max)();
        if(base>maximum||offset>maximum-uintptr_t(base)||sizeof(T)>maximum-(uintptr_t(base)+offset)){
            reason_="native input address overflow";return false;}
        if(sizeof(T)>limit_-bytes_){reason_="native input byte limit";return false;}
        bytes_+=sizeof(T);const auto address=uintptr_t(base)+offset;
        if(!readable_||!readable_(reinterpret_cast<const void*>(address),sizeof(T))){reason_="unreadable native input";return false;}
        std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof(T));return true;
    }
};
struct Vector {uint64_t begin=0,end=0,capacity=0;size_t count=0;};
inline bool vector(Reader& reader,uint64_t base,size_t offset,size_t limit,Vector& out,const char*& reason){
    if(!reader.read(base,offset,out.begin)||!reader.read(base,offset+8,out.end)||!reader.read(base,offset+16,out.capacity))return false;
    if(((out.begin|out.end|out.capacity)&7)||out.end<out.begin||out.capacity<out.end||
       (!out.begin&&(out.end||out.capacity))){reason="malformed native owner vector";return false;}
    out.count=size_t((out.end-out.begin)/8);
    // Capacity can exceed the admitted content bound after prior allocations.
    // Only bounded current elements are copied; no capacity span is touched.
    if(out.count>limit){reason="native owner vector limit";return false;}return true;
}
}

// At the verified root traversal entry, before pending/current assignment and
// owner admission. Copies numeric inputs; never calls engine retain, refresh,
// classifier or cleanup methods, and never follows a saved token after return.
// The source/mesh descriptions do not own GPU resources or mutable native arrays.
inline NativeStereoOwnerInputs capture_native_stereo_owner_inputs(
    const void* root,uint32_t globalMask,bool(*readable)(const void*,size_t),
    NativeStereoOwnerInputLimits limits={}) noexcept {
    NativeStereoFpGuard fp(true);
    NativeStereoOwnerInputs out;out.globalMask=globalMask;
    const auto refuse=[&](const char* reason){NativeStereoOwnerInputs failed;failed.reason=reason;failed.bytesRead=out.bytesRead;return failed;};
    if(!root||!readable)return refuse("missing owner input reader/root");
    if(!limits.owners||limits.owners>nativeStereoOwnerNodeLimit||limits.sources>nativeStereoOwnerSourceLimit||
       limits.meshes>nativeStereoOwnerMeshLimit||limits.bytes>nativeStereoOwnerInputByteLimit)return refuse("invalid owner input limits");
    try {
        using namespace native_owner_input_detail;
        const char* reason="incomplete owner input read";
        Reader reader(readable,out.bytesRead,limits.bytes,reason);
        std::unordered_set<uint64_t> ownerSet,sourceSet;
        const auto rootToken=uint64_t(reinterpret_cast<uintptr_t>(root));
        ownerSet.insert(rootToken);out.ownerTokens.push_back(rootToken);out.owners.emplace_back();out.world.emplace_back();
        for(size_t index=0;index<out.owners.size();++index){
            const auto token=out.ownerTokens[index];NativeStereoOwner node;uint64_t attachment=0;
            if(!reader.read(token,0,node.state.flags)||!reader.read(token,0x74,node.state.mask)||
               !reader.read(token,0x80,attachment)||!reader.read(token,0x3d8,node.state.pendingValue)||
               !reader.read(token,0x3e0,node.state.currentValue))return refuse(reason);
            node.state.attachmentPresent=attachment!=0;
            if(!attachment&&(node.state.flags&0x480000u))return refuse("unsupported native refresh/bounds path");
            Vector children,sources;
            if(!vector(reader,token,0xa0,limits.owners-out.owners.size(),children,reason)||
               !vector(reader,token,0x88,limits.sources-out.sources.size(),sources,reason))return refuse(reason);
            if(sources.count){
                if(!reader.read(token,0x298,out.world[index]))return refuse(reason);
                for(float value:out.world[index])if(!std::isfinite(value))return refuse("non-finite native owner matrix");
            }
            for(size_t i=0;i<sources.count;++i){
                uint64_t geometry=0;if(!reader.read(sources.begin,i*8,geometry))return refuse(reason);
                if(!geometry||!sourceSet.insert(geometry).second)return refuse("missing/aliased native source");
                NativeStereoOwnerSourceInput source;source.id=uint32_t(out.sources.size());source.owner=uint32_t(index);source.geometryToken=geometry;
                uint64_t geometryOwner=0;int32_t count=0;
                if(!reader.read(geometry,0,source.flags)||!reader.read(geometry,0x130,geometryOwner)||
                   !reader.read(geometry,0x138,source.lazyStateToken)||!reader.read(geometry,0x168,count)||
                   !reader.read(geometry,0x170,source.arrayToken))return refuse(reason);
                if(geometryOwner!=token)return refuse("source has a different native owner");
                if(count<0||count>16384||size_t(count)>limits.meshes-out.meshes.size())return refuse("native mesh input limit");
                source.meshCount=uint32_t(count);source.firstMesh=uint32_t(out.meshes.size());
                constexpr uint64_t stride=0xe0;
                if(count&&(!source.arrayToken||source.arrayToken%8||uint64_t(count)*stride>(std::numeric_limits<uintptr_t>::max)()-source.arrayToken))
                    return refuse("invalid native source array");
                for(uint32_t m=0;m<source.meshCount;++m){
                    NativeStereoOwnerMeshInput mesh;mesh.source=source.id;mesh.index=m;
                    const auto address=source.arrayToken+uint64_t(m)*stride;
                    if(!reader.read(address,0,mesh.flags)||!reader.read(address,0x80,mesh.geometryToken)||
                       !reader.read(address,0xcc,mesh.callbackToken))return refuse(reason);
                    // No callback is invoked and no embedded pointer becomes an
                    // owning reference. Unsupported draw eligibility is separate.
                    out.meshes.push_back(mesh);
                }
                node.sourceIds.push_back(source.id);out.sources.push_back(source);
            }
            // Read every child regardless of current flags/masks. Filtering
            // during copying would lose a right-only subtree.
            for(size_t i=0;i<children.count;++i){
                uint64_t child=0;if(!reader.read(children.begin,i*8,child))return refuse(reason);
                if(!child||!ownerSet.insert(child).second)return refuse("native owner graph is not a tree");
                node.children.push_back(uint32_t(out.owners.size()));
                out.ownerTokens.push_back(child);out.owners.emplace_back();out.world.emplace_back();
            }
            out.owners[index]=std::move(node);
        }
        out.complete=true;out.reason="owned unfiltered owner/source/mesh descriptions";return out;
    } catch(const std::bad_alloc&){return refuse("owner input allocation failure");}
      catch(...){return refuse("owner input reader failure");}
}

// Both view masks must be supplied by the caller. Do not silently reuse the
// left mask for the right eye. The planner remains admission-only.
inline NativeStereoOwnerPlanResult plan_native_stereo_owner_inputs(
    const NativeStereoOwnerInputs& inputs,const std::array<NativeStereoOwnerView,2>& views) noexcept {
    if(!inputs.complete)return {};
    try {
        NativeStereoOwnerPlanInput plan;plan.owners=inputs.owners;plan.globalMask=inputs.globalMask;plan.views=views;
        return build_native_stereo_owner_plan(plan);
    } catch(const std::bad_alloc&){NativeStereoOwnerPlanResult result;result.reason=NativeStereoOwnerPlanReason::AllocationFailure;return result;}
}

class NativeStereoOwnerInputFrame {
    StereoFrameIdentity identity_;
    uint64_t scene_=0,view_=0;
    NativeStereoOwnerInputLimits remaining_{};
    std::vector<NativeStereoOwnerInputs> roots_;
    std::unordered_set<uint64_t> owners_;
    bool complete_=false,sealed_=false,maskObserved_=false;
    uint32_t globalMask_=0;
    NativeStereoOwnerView firstView_{};
    const char* reason_="no owner frame";
public:
    void reset(){identity_={};scene_=view_=0;remaining_={};roots_.clear();owners_.clear();complete_=sealed_=maskObserved_=false;reason_="no owner frame";}
    void begin(const StereoFrameIdentity& identity,uint64_t scene,uint64_t view,NativeStereoOwnerView firstView){
        reset();if(!identity.valid()||!scene||!view)return;
        identity_=identity;scene_=scene;view_=view;firstView_=firstView;complete_=true;reason_="owner frame open";
    }
    bool accepts(const StereoFrameIdentity& identity,uint64_t scene,uint64_t view)const{
        return complete_&&!sealed_&&identity_==identity&&scene_==scene&&view_==view;
    }
    bool append(const StereoFrameIdentity& identity,uint64_t scene,uint64_t view,NativeStereoOwnerView observedView,const void* root,uint32_t globalMask,
        bool(*readable)(const void*,size_t)) noexcept {
        if(!accepts(identity,scene,view))return false;
        if(observedView.mask!=firstView_.mask||observedView.parentNull!=firstView_.parentNull||
           (maskObserved_&&globalMask!=globalMask_)){
            reason_="native owner view/global mask changed";complete_=false;roots_.clear();owners_.clear();return false;}
        maskObserved_=true;globalMask_=globalMask;
        auto captured=capture_native_stereo_owner_inputs(root,globalMask,readable,remaining_);
        if(!captured.complete){reason_=captured.reason;complete_=false;roots_.clear();owners_.clear();return false;}
        try {
            for(const auto token:captured.ownerTokens)if(!owners_.insert(token).second){
                reason_="overlapping native root trees";complete_=false;roots_.clear();owners_.clear();return false;}
            remaining_.owners-=captured.owners.size();remaining_.sources-=captured.sources.size();
            remaining_.meshes-=captured.meshes.size();remaining_.bytes-=captured.bytesRead;
            roots_.push_back(std::move(captured));return true;
        } catch(...){reason_="owner frame allocation failure";complete_=false;roots_.clear();owners_.clear();return false;}
    }
    size_t root_count()const{return complete_?roots_.size():0;}
    bool seal(const StereoFrameIdentity& identity){
        if(!complete_||identity_!=identity||roots_.empty())return false;
        sealed_=true;reason_="owned selected-root frame sealed";return true;
    }
    const std::vector<NativeStereoOwnerInputs>* inputs(const StereoFrameIdentity& identity)const{
        return complete_&&sealed_&&identity_==identity?&roots_:nullptr;
    }
    NativeStereoOwnerView first_view()const{return firstView_;}
    const char* reason()const{return reason_;}
};
}

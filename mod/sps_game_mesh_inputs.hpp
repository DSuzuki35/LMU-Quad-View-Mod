#pragma once
#include "stereo_draw.hpp"
#include "quad_math.hpp"
#include <limits>

namespace lmuqv {
// Numeric/handle inputs from the pinned game's unfiltered mesh records. Tokens
// are valid only at the synchronous game-data boundary; they are never a draw
// list, an ownership claim, or permission to call a game rendering callback.
struct SpsGameMeshInputs {
    bool complete=false,localTransform=false,skinned=false;
    const char* reason="SPS mesh input unavailable";
    size_t bytesRead=0;
    uint32_t meshFlags=0,geometryFlags=0,shaderFlags=0,pass=0;
    uint64_t mesh=0,geometry=0,owner=0,material=0,effect=0;
    uint64_t vertexBuffer=0,indexBuffer=0,layout=0,vertexShader=0,pixelShader=0,vertexParameters=0,pixelParameters=0;
    UINT vertexStride=0,indexCount=0,startIndex=0;
    INT baseVertex=0;
    DXGI_FORMAT indexFormat=DXGI_FORMAT_UNKNOWN;
    D3D11_PRIMITIVE_TOPOLOGY topology=D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    std::vector<Matrix> palette;
};
namespace sps_game_input_detail {
class Reader {
    bool(*readable_)(const void*,size_t);
    size_t& bytes_;
public:
    Reader(bool(*readable)(const void*,size_t),size_t& bytes):readable_(readable),bytes_(bytes){}
    bool copy(uint64_t base,size_t offset,void* value,size_t bytes){
        const auto maximum=(std::numeric_limits<uintptr_t>::max)();
        if(!base||base>maximum||offset>maximum-uintptr_t(base)||bytes>maximum-(uintptr_t(base)+offset)||
           bytes_>32768||bytes>32768-bytes_)return false;
        const auto* address=reinterpret_cast<const void*>(uintptr_t(base)+offset);
        if(!readable_||!readable_(address,bytes))return false;
        std::memcpy(value,address,bytes);bytes_+=bytes;return true;
    }
    template<class T>bool read(uint64_t base,size_t offset,T& value){return copy(base,offset,&value,sizeof(value));}
};
template<size_t N,class T>T at(const std::array<uint8_t,N>& bytes,size_t offset){T value;std::memcpy(&value,bytes.data()+offset,sizeof(value));return value;}
}

// These layouts are established by the pinned source-array, geometry bind and
// shader-handle accessors. Copy source numbers once; do not enter 0x51f040,
// 0x5629e0, a mesh callback, or a D3D native-eye draw to obtain them.
inline SpsGameMeshInputs read_sps_game_mesh(uint64_t mesh,uint64_t geometry,unsigned profile,
    bool(*readable)(const void*,size_t)) noexcept {
    SpsGameMeshInputs out;
    const auto fail=[&](const char* why){SpsGameMeshInputs empty;empty.reason=why;empty.bytesRead=out.bytesRead;return empty;};
    if(profile>1||!mesh||!geometry||!readable)return fail("invalid SPS game mesh scope");
    try{
        using namespace sps_game_input_detail;Reader reader(readable,out.bytesRead);
        std::array<uint8_t,0xe0> record{};std::array<uint8_t,0x1b4> source{};
        if(!reader.copy(mesh,0,record.data(),record.size())||!reader.copy(geometry,0,source.data(),source.size()))return fail("unreadable SPS mesh/source record");
        const auto array=at<source.size(),uint64_t>(source,0x170);const auto count=at<source.size(),int32_t>(source,0x168);
        if(count<=0||count>16384||!array||mesh<array||(mesh-array)%0xe0||uint64_t(count)*0xe0<=mesh-array)
            return fail("SPS mesh is outside its unfiltered source array");
        if(at<record.size(),uint64_t>(record,0x80)!=geometry)return fail("SPS mesh/source ownership differs");
        out.meshFlags=at<record.size(),uint32_t>(record,0);out.geometryFlags=at<source.size(),uint32_t>(source,0);
        out.owner=at<source.size(),uint64_t>(source,0x130);out.material=at<record.size(),uint64_t>(record,0x40);
        const auto pass=at<record.size(),int32_t>(record,0x48);if(pass<0||pass>=256)return fail("invalid SPS mesh pass");out.pass=unsigned(pass);
        if(out.meshFlags&0x200000u)return fail("disabled SPS game mesh");
        if(out.meshFlags&0x10000000u)return fail("SPS dynamic geometry update input required");
        if(at<record.size(),uint64_t>(record,0xcc))return fail("SPS mesh callback effects not imported");
        out.indexCount=at<record.size(),UINT>(record,0x10);
        const uint64_t index=uint64_t(at<record.size(),UINT>(record,0x38))+at<source.size(),UINT>(source,0x1b0);
        const int64_t vertex=int64_t(at<record.size(),INT>(record,0x28))+at<source.size(),INT>(source,0x1a0);
        if(!out.indexCount||index>UINT_MAX||vertex<INT_MIN||vertex>INT_MAX)return fail("invalid SPS geometry draw range");
        out.startIndex=UINT(index);out.baseVertex=INT(vertex);
        const auto topology=at<record.size(),UINT>(record,0x4c);
        if(topology==4)out.topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        else if(topology==5)out.topology=D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        else return fail("SPS mesh primitive type unsupported");
        const auto vertices=at<record.size(),uint64_t>(record,0x20),indices=at<record.size(),uint64_t>(record,0x30);
        std::array<uint8_t,0x40> vertexRecord{};std::array<uint8_t,0x30> indexRecord{};
        if(!reader.copy(vertices,0,vertexRecord.data(),vertexRecord.size())||!reader.copy(indices,0,indexRecord.data(),indexRecord.size()))return fail("SPS geometry handles unavailable");
        const auto format=at<vertexRecord.size(),UINT>(vertexRecord,0x20);
        if(format!=0x1000&&format!=0x4000&&format!=0x8000&&format!=0x10000&&format!=0x20000&&format!=0x40000)
            return fail("SPS separate vertex streams not imported");
        const auto descriptor=at<vertexRecord.size(),uint64_t>(vertexRecord,0x38);
        std::array<uint8_t,0x30> vertexDescriptor{};std::array<uint8_t,0x10> vertexBinding{};
        if(!reader.copy(descriptor,0,vertexDescriptor.data(),vertexDescriptor.size()))return fail("SPS vertex descriptor unavailable");
        const auto binding=at<vertexDescriptor.size(),uint64_t>(vertexDescriptor,0x28);
        if(!reader.copy(binding,0,vertexBinding.data(),vertexBinding.size())||at<vertexBinding.size(),uint8_t>(vertexBinding,0xc))
            return fail("SPS pending vertex upload input required");
        out.vertexBuffer=at<vertexBinding.size(),uint64_t>(vertexBinding,0);out.layout=at<vertexDescriptor.size(),uint64_t>(vertexDescriptor,0x20);
        out.vertexStride=at<vertexDescriptor.size(),UINT>(vertexDescriptor,0x14);
        const auto indexType=at<indexRecord.size(),UINT>(indexRecord,0x1c);
        const auto indexBinding=at<indexRecord.size(),uint64_t>(indexRecord,0x28);
        if(indexType==0x46)out.indexFormat=DXGI_FORMAT_R16_UINT;else if(indexType==0x47)out.indexFormat=DXGI_FORMAT_R32_UINT;
        else return fail("SPS index format unsupported");
        if(!reader.read(indexBinding,0,out.indexBuffer))return fail("SPS index resource unavailable");
        if(!out.vertexBuffer||!out.indexBuffer||!out.layout||!out.vertexStride||out.vertexStride>2048)return fail("incomplete SPS geometry bindings");
        if(!reader.read(out.material,0x550,out.effect))return fail("SPS material effect unavailable");
        const auto shader=[&](size_t offset,uint64_t& object,uint64_t& parameters){uint64_t begin=0,end=0,handle=0,group=0,program=0;
            if(!reader.read(out.effect,offset,begin)||!reader.read(out.effect,offset+8,end)||!begin||end<begin||end-begin<8||end-begin>256*8)return false;
            if(!reader.read(begin,0,handle)||!reader.read(handle,8,group)||!reader.read(group,0x30+profile*8,program)||!reader.read(program,0x28,object)||!object)return false;
            parameters=handle;return true;
        };
        if(!shader(0x130,out.vertexShader,out.vertexParameters)||!shader(0x148,out.pixelShader,out.pixelParameters)||
           !reader.read(out.vertexParameters,0xf8,out.shaderFlags))return fail("SPS shader creation handles unavailable");
        out.localTransform=(out.meshFlags&0x8000u)!=0;
        uint64_t pose=at<source.size(),uint64_t>(source,0x138);
        if((out.geometryFlags&0x4000u)&&pose){
            uint32_t flags=0;int32_t paletteCount=at<record.size(),int32_t>(record,0x88);uint64_t mapping=at<record.size(),uint64_t>(record,0x90),bones=0;
            if(!reader.read(pose,0,flags)||!(flags&0x1000u))return fail("SPS skeletal update inputs not finalized");
            if(paletteCount<0)return fail("invalid SPS bone count");paletteCount=(std::min)(paletteCount,40);
            if(paletteCount&&!reader.read(pose,0x28,bones))return fail("SPS skeleton unavailable");
            out.palette.resize(size_t(paletteCount));
            for(unsigned i=0;i<out.palette.size();++i){uint8_t boneIndex=0;uint64_t bone=0;
                if(!reader.read(mapping,i,boneIndex)||!reader.read(bones,size_t(boneIndex)*8,bone)||!reader.read(bone,0x4c,out.palette[i]))return fail("SPS bone palette unavailable");
                for(float f:out.palette[i])if(!std::isfinite(f))return fail("non-finite SPS bone palette");}
            out.skinned=true;
        }
        out.mesh=mesh;out.geometry=geometry;out.complete=true;out.reason="owned SPS mesh numbers, geometry/shader handles and bone palette";return out;
    }catch(...){return fail("SPS mesh input allocation/reader failure");}
}
}

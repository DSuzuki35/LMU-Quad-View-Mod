#pragma once
#include "native_stereo_transform.hpp"
#include "stereo_shader.hpp"
#include <vector>

namespace lmuqv {
using StereoConstantBuffers=std::array<std::vector<uint8_t>,14>;
struct StereoTransformPayloads {
    bool complete=false;
    const char* reason="incomplete transform payloads";
    std::array<std::vector<uint8_t>,2> shadow;
};
// These are owned helper-shadow payloads, NOT an assumed VS b0 layout. Native
// shadow-to-CB windows/bindings must be supplied separately by a verified adapter.
inline StereoTransformPayloads prepare_stereo_transform_payloads(
    const std::array<NativeStereoTransformInput,2>& inputs,const std::array<std::vector<uint8_t>,2>& base) noexcept {
    NativeStereoFpGuard preserve;
    const auto refuse=[](const char* reason){StereoTransformPayloads out;out.reason=reason;return out;};
    if(std::fegetround()!=FE_TONEAREST)return refuse("unsupported transform FP controls");
    for(const auto& payload:base)if(payload.size()<20*16||payload.size()>65536||payload.size()%16)return refuse("invalid owned transform shadow");
    try {
        std::array<NativeStereoTransformResult,2> prepared;
        for(unsigned eye=0;eye<2;++eye){prepared[eye]=prepare_native_stereo_transform(inputs[eye]);if(!prepared[eye].available)return refuse(prepared[eye].reason);}
        StereoTransformPayloads out;out.shadow=base;
        for(unsigned eye=0;eye<2;++eye){const auto& c=prepared[eye].constants;auto* bytes=out.shadow[eye].data();
            std::memcpy(bytes,c.derived.data(),64);std::memcpy(bytes+4*16,c.world.data(),64);
            std::memcpy(bytes+8*16,c.position.data(),16);std::memcpy(bytes+16*16,c.view.data(),64);}
        out.complete=true;out.reason="owned per-eye 52-float transform payloads";return out;
    }catch(...){return refuse("transform payload allocation failure");}
}
struct StereoPackedConstants {
    bool complete=false;
    const char* reason="invalid packed constants";
    unsigned slot=0;
    std::vector<uint8_t> bytes;
};
template<class Read>inline StereoPackedConstants pack_stereo_constants_from(const StereoShaderVariant& variant,Read&& source) noexcept {
    const auto refuse=[](const char* reason){StereoPackedConstants out;out.reason=reason;return out;};
    if(variant.packedSlot>=14||variant.eyeStride>2048||variant.constants.size()>14)return refuse("packed constant limits");
    if(variant.constants.empty()){
        if(variant.eyeStride)return refuse("empty packed constant layout has a stride");
        StereoPackedConstants out;out.complete=true;out.slot=variant.packedSlot;out.reason="no packed constant payload required";return out;
    }
    if(!variant.eyeStride)return refuse("missing packed eye stride");
    std::array<uint8_t,2048> occupied{};uint32_t slots=0;
    for(const auto& range:variant.constants){
        if(range.sourceSlot>=14||!range.registers||range.offset>variant.eyeStride||range.registers>variant.eyeStride-range.offset||
           (slots&(1u<<range.sourceSlot)))return refuse("invalid/duplicate packed constant range");
        slots|=1u<<range.sourceSlot;
        for(unsigned r=range.offset;r<range.offset+range.registers;++r){if(occupied[r])return refuse("overlapping packed constant ranges");occupied[r]=1;}
        for(unsigned eye=0;eye<2;++eye){const auto& payload=source(eye,range.sourceSlot);
            if(payload.size()<size_t(range.registers)*16||payload.size()>65536||payload.size()%16)return refuse("missing or malformed owned constant buffer");}
    }
    try {
        StereoPackedConstants out;out.slot=variant.packedSlot;out.bytes.resize(size_t(variant.eyeStride)*2*16);
        for(unsigned eye=0;eye<2;++eye)for(const auto& range:variant.constants)
            std::memcpy(out.bytes.data()+(size_t(eye)*variant.eyeStride+range.offset)*16,
                source(eye,range.sourceSlot).data(),size_t(range.registers)*16);
        out.complete=true;out.reason="owned packed eye constant banks";return out;
    }catch(...){return refuse("packed constant allocation failure");}
}
inline StereoPackedConstants pack_stereo_constants(const StereoShaderVariant& variant,const std::array<StereoConstantBuffers,2>& source) noexcept {
    return pack_stereo_constants_from(variant,[&](unsigned eye,unsigned slot)->const std::vector<uint8_t>&{return source[eye][slot];});
}
}

#pragma once
#include "native_stereo_constants.hpp"
#include "stereo_draw_capture.hpp"

namespace lmuqv {
// The native draw hook supplies the current renderer and the shader/layout
// registry's already-cached pipeline. Constants are copied from the pinned CPU
// shadows and checked against actual bindings in capture_stereo_draw.
inline StereoDrawCaptureResult capture_native_stereo_draw_inputs(ID3D11DeviceContext* context,
    StereoGpuResourcePool& pool,uintptr_t renderer,uintptr_t image,
    StereoDrawCaptureRequest request,bool(*readable)(const void*,size_t)) noexcept {
    if(!request.pipeline)return {};
    std::array<uint32_t,2> required{};
    for(unsigned stage=0;stage<2;++stage){const auto& shader=stage?request.pipeline->pixel:request.pipeline->vertex;
        for(unsigned slot=0;slot<14;++slot)if(shader.constantRegisters[slot])required[stage]|=1u<<slot;}
    auto constants=capture_native_stereo_constants(renderer,image,request.identity,required,readable);
    if(!constants.complete){StereoDrawCaptureResult out;out.reason=constants.reason;return out;}
    request.constants=&constants.stages;request.constantTokens=&constants.bufferTokens;
    return capture_stereo_draw(context,pool,request);
}
}

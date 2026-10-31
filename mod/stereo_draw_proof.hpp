#pragma once
#include <d3d11.h>
#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace lmuqv {
struct StereoProofDraw {
    uint64_t key=0;
    UINT indexCount=0,startIndex=0;INT baseVertex=0;
    const void* vertexCode=nullptr;size_t vertexBytes=0;
    const void* pixelCode=nullptr;size_t pixelBytes=0;
    uint32_t vertexConstants=0,pixelConstants=0;
    bool forceGeometry=false; // Diagnostic/testing fallback; capability checks still apply.
};
struct StereoProofResult {
    std::string status="disabled",reason,route;
    uint64_t key=0,observed=0,querySkips=0,timingQueryOverlaps=0;
    unsigned width=0,height=0;
    std::array<uint64_t,2> covered{},colorDifferent{},depthDifferent{};
    float maximumColorError=0,maximumDepthError=0;
};
// Bounded diagnostic. Replays one matching draw pair into PRIVATE color/depth
// targets and compares the converted single draw with two original references.
// It never suppresses a game draw, writes an XR image, or reads mapped WRITE
// memory. Call only on the owning immediate-context thread, outside queries
// that count rendering or whose type is unknown. Timestamp-disjoint scopes may
// contain the diagnostic, but their timings include its extra GPU work.
// ExecuteCommandList(TRUE) preserves application pipeline state. Source textures
// are retained, not deep-copied: a left/right resource-content change can fail
// this check. A passing sampled pair is not permission to reuse it next frame.
class StereoDrawProof {
    struct State;
    std::unique_ptr<State> state_;
    StereoProofResult result_;
public:
    StereoDrawProof();~StereoDrawProof();
    StereoDrawProof(const StereoDrawProof&)=delete;
    StereoDrawProof& operator=(const StereoDrawProof&)=delete;
    void begin();
    void observe(ID3D11DeviceContext*,bool right,const StereoProofDraw&,bool outsideResultQueries,bool insideTimingQuery=false);
    void finish();
    void reset();
    const StereoProofResult& result()const{return result_;}
};
}

#include "../quad_math.hpp"
#include <cmath>
#include <iostream>
using namespace lmuqv;
static void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    Matrix p{};
    Fov f{-0.8f, 0.7f, 0.6f, -0.65f};
    require(make_engine_projection(f, 0.1f, 1000.0f, 1, 1, p), "finite projection rejected");
    auto project = [&](float x, float y, float z, unsigned axis) {
        const float v[4]{x,y,z,1}; float clip=0,w=0;
        for (unsigned i=0;i<4;++i) { clip+=v[i]*p[i*4+axis]; w+=v[i]*p[i*4+3]; }
        return clip/w;
    };
    require(std::abs(project(0,0,0.1f,2))<1e-5f, "near does not map to zero");
    require(std::abs(project(0,0,1000,2)-1)<1e-5f, "far does not map to one");
    require(std::abs(project(std::tan(f.left),0,1,0)+1)<1e-5f, "left edge");
    require(std::abs(project(std::tan(f.right),0,1,0)-1)<1e-5f, "right edge");
    require(std::abs(project(0,std::tan(f.up),1,1)-1)<1e-5f, "top edge");
    require(std::abs(project(0,std::tan(f.down),1,1)+1)<1e-5f, "bottom edge");
    Matrix focus{};
    require(make_engine_projection({-0.15f,0.20f,0.18f,-0.17f},0.1f,1000,1,1,focus), "focus rejected");
    require(focus[0]>p[0]*3 && focus[5]>p[5]*3, "focus not narrower");
    // Actual LMU snapshot: near=.075, far=20000; reversed depth, near=1 and far=0.
    Matrix liveOuter=p;
    liveOuter[10]=-0.075f/(20000.f-0.075f);
    liveOuter[14]=0.075f*20000.f/(20000.f-0.075f);
    const Matrix liveFocus=replace_fov(liveOuter,focus);
    require(std::abs((liveFocus[10]*0.075f+liveFocus[14])/0.075f-1)<1e-6f,
            "focus reversed-depth near plane is wrong");
    require(std::abs((liveFocus[10]*20000.f+liveFocus[14])/20000.f)<1e-8f,
            "focus reversed-depth far plane is wrong");
    require(liveFocus[0]>liveOuter[0]*3&&liveFocus[10]==liveOuter[10]&&liveFocus[14]==liveOuter[14],
            "focus must change FOV while retaining exact native depth coefficients");
    Matrix model{1,0,0,0, 0,1,0,0, 0,0,1,0, 2,3,4,1};
    Matrix composite=multiply(model,p), expected=multiply(model,focus);
    Matrix remapped=remap_fov(composite,p,focus);
    for(unsigned i=0;i<16;++i) require(std::abs(remapped[i]-expected[i])<2e-5f, "UI projection remap");
    Matrix preserved=p;
    require(!make_engine_projection(f,0.1f,0.1f,1,1,p) && p==preserved, "invalid clipping mutated output");
    require(!make_engine_projection({0,0,0,0},0.1f,1000,1,1,p), "empty FOV accepted");
    std::cout << "projection tests passed\n";
}

#include <cassert>
#include <iostream>
#if __has_include("../native_stereo_transform.hpp")
#include "../native_stereo_transform.hpp"
#include <cstring>
#include <limits>
#include <vector>
using namespace lmuqv;
namespace {
using Matrix = std::array<float,16>;
const Matrix identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
NativeStereoTransformInput input() {
    NativeStereoTransformInput in;
    in.world = identity; in.view = identity; in.derived = identity;
    in.position = {3,4,5};
    return in;
}
void numerical() {
    auto in = input();
    in.world = {2,0,0,0, 0,3,0,0, 0,0,4,0, 7,8,9,1};
    in.derived = {5,0,0,0, 0,6,0,0, 0,0,7,0, 10,11,12,1};
    auto out = prepare_native_stereo_transform(in);
    assert(out.available && !out.renderingEligible);
    assert(out.constants.derived == in.derived && out.constants.world == in.world);
    assert(out.constants.view == identity);
    assert((out.constants.position == std::array<float,4>{3,4,5,0}));
    in.localTransform = true;
    out = prepare_native_stereo_transform(in);
    const Matrix expected{10,0,0,0, 0,18,0,0, 0,0,28,0, 45,59,75,1};
    assert(out.available && out.constants.derived == expected && out.constants.view == in.world);
    // A second eye owns a different result; source mutation cannot change either.
    auto right = in; right.derived[12] = 20; right.position[0] = -3;
    auto r = prepare_native_stereo_transform(right);
    assert(r.available && r.constants.derived[12] == 55 && r.constants.position[0] == -3);
    right = {}; in = {};
    assert(out.constants.derived == expected && r.constants.derived[12] == 55);

    in = input(); in.localTransform = true;
    in.world[0] = in.world[1] = in.world[2] = in.world[3] = 1;
    in.derived = {67108864,0,0,0, 1,1,0,0, -67108864,0,1,0, 1,0,0,1};
    out = prepare_native_stereo_transform(in);
    assert(out.available && out.constants.derived[0] == 0); // serial accumulation yields 1
    const auto saved = _mm_getcsr(); _mm_setcsr(0x1fbf);
    out = prepare_native_stereo_transform(in);
    assert(out.available && _mm_getcsr() == 0x1fbf);
    _mm_setcsr(0x9fc0);
    out = prepare_native_stereo_transform(in);
    assert(!out.available && _mm_getcsr() == 0x9fc0);
    _mm_setcsr(saved);
    in = input(); in.position[2] = std::numeric_limits<float>::infinity();
    assert(!prepare_native_stereo_transform(in).available);
    in = input(); in.view[5] = std::numeric_limits<float>::quiet_NaN();
    assert(!prepare_native_stereo_transform(in).available);
    in = input(); in.localTransform = true;
    in.world[0] = std::numeric_limits<float>::max(); in.derived[0] = 2;
    out = prepare_native_stereo_transform(in);
    assert(!out.available && out.constants.world == Matrix{} && out.constants.derived == Matrix{});
}
struct Fixture {
    std::array<unsigned char,0x110> shader{};
    std::array<unsigned char,0xe0> mesh{};
    std::array<unsigned char,0x178> geometry{};
    std::array<unsigned char,0x318> owner{};
    std::array<unsigned char,0x3e0> view{};
    std::array<float,80> shadow{};
    template<class T, class A> static void put(A& a, size_t at, const T& v) {
        assert(at + sizeof(v) <= sizeof(a)); std::memcpy(reinterpret_cast<unsigned char*>(a.data())+at,&v,sizeof(v));
    }
    Fixture() {
        put(shader,8,uint64_t{1}); put(shader,0xf8,uint32_t{1});
        put(mesh,0x80,reinterpret_cast<uintptr_t>(geometry.data()));
        put(geometry,0x130,reinterpret_cast<uintptr_t>(owner.data()));
        put(owner,0x298,identity); put(view,0x1dc,identity); put(view,0x3a0,identity);
        put(view,0x220,std::array<float,3>{3,4,5});
        put(shadow,0,identity); put(shadow,64,identity);
        put(shadow,128,std::array<float,4>{3,4,5,0}); put(shadow,256,identity);
    }
    NativeStereoTransformArgs args() const {return {shader.data(),view.data(),mesh.data(),shadow.data(),1,false};}
};
std::vector<std::pair<uintptr_t,size_t>> spans;
size_t reads=0;
bool readable(const void* p,size_t size) {
    ++reads; auto at=reinterpret_cast<uintptr_t>(p);
    for(const auto& s:spans) if(at>=s.first && at-s.first<=s.second && size<=s.second-(at-s.first))return true;
    return false;
}
void allow(Fixture& f) {
    spans={{reinterpret_cast<uintptr_t>(f.shader.data()),sizeof(f.shader)},
           {reinterpret_cast<uintptr_t>(f.mesh.data()),sizeof(f.mesh)},
           {reinterpret_cast<uintptr_t>(f.geometry.data()),sizeof(f.geometry)},
           {reinterpret_cast<uintptr_t>(f.owner.data()),sizeof(f.owner)},
           {reinterpret_cast<uintptr_t>(f.view.data()),sizeof(f.view)},
           {reinterpret_cast<uintptr_t>(f.shadow.data()),sizeof(f.shadow)}};
}
void capture() {
    Fixture f; allow(f);
    auto pre = capture_native_stereo_transform(f.args(),readable);
    assert(pre.complete && pre.bytesRead <= nativeStereoTransformInputByteLimit);
    auto actual = capture_native_stereo_transform_constants(f.shadow.data(),readable);
    assert(actual.complete && actual.bytesRead == 208);
    auto post = capture_native_stereo_transform(f.args(),readable);
    auto c = compare_native_stereo_transform(pre,post,actual,0x1fbf,0x1f80);
    assert(c.available && c.mismatchedScalars == 0 && c.comparedScalars == 52);
    f.shadow[0] = 2;
    actual = capture_native_stereo_transform_constants(f.shadow.data(),readable);
    c = compare_native_stereo_transform(pre,post,actual,0x1f80,0x1f80);
    assert(c.available && c.mismatchedScalars == 1);
    assert(c.firstMismatchRegister == 0 && c.firstMismatchComponent == 0);
    actual.constants.derived[0]=1; actual.constants.position[3]=-0.0f;
    c=compare_native_stereo_transform(pre,post,actual,0x1f80,0x1f80);
    assert(c.mismatchedScalars==1 && c.firstMismatchRegister==8 && c.firstMismatchComponent==3);
    Fixture::put(f.view,0x220,std::array<float,3>{9,4,5});
    post = capture_native_stereo_transform(f.args(),readable);
    assert(!compare_native_stereo_transform(pre,post,actual,0x1f80,0x1f80).available);
    assert(!compare_native_stereo_transform(pre,pre,actual,0x9fc0,0x1f80).available);
    // Shadow c9..c15 is unrelated and must never be read/compared.
    f = Fixture{}; // reset internal pointer fields explicitly after copy
    Fixture::put(f.mesh,0x80,reinterpret_cast<uintptr_t>(f.geometry.data()));
    Fixture::put(f.geometry,0x130,reinterpret_cast<uintptr_t>(f.owner.data()));
    f.shadow[36] = std::numeric_limits<float>::quiet_NaN();
    pre = capture_native_stereo_transform(f.args(),readable);
    actual = capture_native_stereo_transform_constants(f.shadow.data(),readable);
    assert(compare_native_stereo_transform(pre,pre,actual,0x1f80,0x1f80).mismatchedScalars == 0);
    auto args=f.args(); args.suppliedFlags=8; reads=0;
    assert(!capture_native_stereo_transform(args,readable).complete && reads == 0);
    args=f.args(); args.reset=true; reads=0;
    assert(!capture_native_stereo_transform(args,readable).complete && reads == 0);
    args=f.args(); args.suppliedFlags=0;
    assert(capture_native_stereo_transform(args,readable).complete);
    Fixture::put(f.mesh,0,uint32_t{0x100000});
    assert(!capture_native_stereo_transform(f.args(),readable).complete);
    Fixture::put(f.mesh,0,uint32_t{0}); Fixture::put(f.mesh,0xcc,uint64_t{1});
    assert(!capture_native_stereo_transform(f.args(),readable).complete);
    Fixture::put(f.mesh,0xcc,uint64_t{0}); Fixture::put(f.geometry,0x130,uint64_t{0});
    assert(!capture_native_stereo_transform(f.args(),readable).complete);
    Fixture::put(f.geometry,0x130,reinterpret_cast<uintptr_t>(f.owner.data()));
    spans.clear();
    assert(!capture_native_stereo_transform(f.args(),readable).complete);
    assert(!capture_native_stereo_transform_constants(f.shadow.data(),readable).complete);
    assert(!capture_native_stereo_transform(f.args(),nullptr).complete);
}
void forwarding() {
    const auto saved=_mm_getcsr(); int calls=0, ends=0;
    _mm_setcsr(0x1fbf);
    const int result=invoke_native_stereo_transform(
        [](unsigned entry){assert(entry==0x1fbf);_mm_setcsr(0x9fc0);return 42;},
        [&]{++calls;assert(_mm_getcsr()==0x1fbf);_mm_setcsr(0x1f82);return -37;},
        [&](int token,unsigned post){++ends;assert(token==42 && post==0x1f82);_mm_setcsr(0x9fc0);throw 1;});
    assert(result==-37 && calls==1 && ends==1 && _mm_getcsr()==0x1f82);
    bool caught=false;
    try {invoke_native_stereo_transform([](unsigned){return 0;},[&]()->int{++calls;throw 7;},[&](int,unsigned){++ends;});}
    catch(int code){caught=code==7;}
    assert(caught && calls==2 && ends==1);
    _mm_setcsr(saved);
}
}
int main(){numerical();capture();forwarding();std::cout<<"Owned stereo transforms: numeric, lifetime, bounded capture and comparison passed\n";}
#else
int main(){std::cerr<<"FAIL: owned native stereo transform preparation is missing\n";return 1;}
#endif

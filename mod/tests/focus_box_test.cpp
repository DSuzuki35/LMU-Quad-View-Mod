#include "../focus_box.hpp"
#include <cassert>
#include <iostream>
#include <limits>

int main() {
    assert(lmuqv::parse_focus_box(nullptr).percent==100);
    assert(lmuqv::parse_focus_box("").valid);
    for(auto text:{"25","50.5","100","200"}){
        auto box=lmuqv::parse_focus_box(text);assert(box.valid);
        assert(box.percent==std::strtod(text,nullptr));
    }
    for(auto text:{"NaN","inf","24.9","200.1","-50","100junk","0x64","1e2"," 50","+50"}){
        auto box=lmuqv::parse_focus_box(text);assert(!box.valid&&box.percent==100);
    }
    const XrFovf original{std::atan(-.25f),std::atan(.75f),std::atan(.5f),std::atan(-.5f)};
    auto size=lmuqv::parse_focus_box_size(nullptr,nullptr,nullptr);
    assert(size.width.percent==100&&size.height.percent==100);
    size=lmuqv::parse_focus_box_size("50.5","150","200");
    assert(size.width.percent==50.5&&size.height.percent==150);
    size=lmuqv::parse_focus_box_size("50",nullptr,"200");
    assert(size.width.percent==50&&size.height.percent==200);
    size=lmuqv::parse_focus_box_size(nullptr,"150","25");
    assert(size.width.percent==25&&size.height.percent==150);
    size=lmuqv::parse_focus_box_size("NaN","150","200");
    assert(!size.width.valid&&size.width.percent==100&&size.height.valid&&size.height.percent==150);
    size=lmuqv::parse_focus_box_size("50","201","200");
    assert(size.width.valid&&size.width.percent==50&&!size.height.valid&&size.height.percent==100);
    auto asymmetric=original;assert(lmuqv::scale_focus_fov(asymmetric,50,150));
    assert(std::abs(std::tan(asymmetric.angleLeft))<1e-6&&std::abs(std::tan(asymmetric.angleRight)-.5f)<1e-6);
    assert(std::abs(std::tan(asymmetric.angleUp)-.75f)<1e-6&&std::abs(std::tan(asymmetric.angleDown)+.75f)<1e-6);
    auto widthOnly=original;assert(lmuqv::scale_focus_fov(widthOnly,50,100));
    assert(widthOnly.angleUp==original.angleUp&&widthOnly.angleDown==original.angleDown);
    auto heightOnly=original;assert(lmuqv::scale_focus_fov(heightOnly,100,150));
    assert(heightOnly.angleLeft==original.angleLeft&&heightOnly.angleRight==original.angleRight);
    auto exact=original;assert(lmuqv::scale_focus_fov(exact,100));
    assert(std::memcmp(&exact,&original,sizeof(exact))==0);
    auto small=original;assert(lmuqv::scale_focus_fov(small,50));
    assert(std::abs(std::tan(small.angleLeft))<1e-6&&std::abs(std::tan(small.angleRight)-.5f)<1e-6);
    assert(std::abs(std::tan(small.angleUp)-.25f)<1e-6&&std::abs(std::tan(small.angleDown)+.25f)<1e-6);
    auto large=original;assert(lmuqv::scale_focus_fov(large,200));
    assert(std::abs(std::tan(large.angleLeft)+.75f)<1e-6&&std::abs(std::tan(large.angleRight)-1.25f)<1e-6);
    assert(std::abs(std::tan(large.angleUp)-1.f)<1e-6&&std::abs(std::tan(large.angleDown)+1.f)<1e-6);
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();
    for(auto invalid:{XrFovf{0,0,.3f,-.3f},XrFovf{.4f,-.4f,.3f,-.3f},
                     XrFovf{-.4f,.4f,-.3f,.3f},XrFovf{nan,.4f,.3f,-.3f},
                     XrFovf{-.4f,inf,.3f,-.3f},XrFovf{-1.571f,.4f,.3f,-.3f},
                     XrFovf{-.4f,1.571f,.3f,-.3f}}){
        const auto before=invalid;assert(!lmuqv::scale_focus_fov(invalid,50));
        assert(std::memcmp(&invalid,&before,sizeof(invalid))==0);
    }
    for(double invalid:{0.,24.,201.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
        auto fov=original;assert(!lmuqv::scale_focus_fov(fov,invalid));
        assert(std::memcmp(&fov,&original,sizeof(fov))==0);
        assert(!lmuqv::scale_focus_fov(fov,100,invalid));
        assert(std::memcmp(&fov,&original,sizeof(fov))==0);
    }
    std::cout<<"Focus box tests passed: decimal parsing, safe fallback, tangent geometry, exact default, invalid FOV rejection\n";
}

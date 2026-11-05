#pragma once
#include <cstdlib>
#include <new>
#include <cstddef>
// Test-only global allocation interception. Include in one translation unit.
namespace allocation_fixture {
inline size_t count=0,fail=0;
inline bool enabled=false;
}
void* operator new(size_t bytes){
    if(allocation_fixture::enabled&&++allocation_fixture::count==allocation_fixture::fail)throw std::bad_alloc();
    if(void* p=std::malloc(bytes?bytes:1))return p;throw std::bad_alloc();
}
void* operator new[](size_t bytes){return ::operator new(bytes);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
template<class F,class Check> void allocation_refusals(F operation,Check check){
    using namespace allocation_fixture;
    count=fail=0;enabled=true;auto baseline=operation();enabled=false;const auto total=count;check(baseline,true);
    for(size_t i=1;i<=total;++i){count=0;fail=i;enabled=true;auto result=operation();enabled=false;check(result,false);}
    count=fail=0;
}

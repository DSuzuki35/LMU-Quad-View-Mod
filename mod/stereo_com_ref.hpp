#pragma once
#include <utility>
namespace lmuqv {
template<class T>class StereoComRef {
    T* value_=nullptr;
public:
    StereoComRef()=default;
    explicit StereoComRef(T* value):value_(value){if(value_)value_->AddRef();}
    ~StereoComRef(){if(value_)value_->Release();}
    StereoComRef(const StereoComRef& other):StereoComRef(other.value_){}
    StereoComRef(StereoComRef&& other)noexcept:value_(other.value_){other.value_=nullptr;}
    StereoComRef& operator=(StereoComRef other)noexcept{std::swap(value_,other.value_);return *this;}
    void reset(){StereoComRef empty;std::swap(value_,empty.value_);}
    T* get()const{return value_;}
    T* operator->()const{return value_;}
    explicit operator bool()const{return value_!=nullptr;}
    T** put(){reset();return &value_;}
};
}

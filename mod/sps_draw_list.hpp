#pragma once
#include "stereo_draw.hpp"

namespace lmuqv {
// A frame's ordering references persistent prepared draws. Reordering/culling
// does not clone material constants, geometry or resource bindings per eye.
// The scene owns the packets until synchronous batch preparation completes.
class SpsDrawList {
    std::vector<const StereoDrawPacket*> draws_;
public:
    class Iterator {
        std::vector<const StereoDrawPacket*>::const_iterator at_;
    public:
        explicit Iterator(std::vector<const StereoDrawPacket*>::const_iterator at):at_(at){}
        const StereoDrawPacket& operator*()const{return **at_;}
        Iterator& operator++(){++at_;return *this;}
        bool operator!=(const Iterator& other)const{return at_!=other.at_;}
    };
    void clear(){draws_.clear();}
    void reserve(size_t count){draws_.reserve(count);}
    void push_back(const StereoDrawPacket& draw){draws_.push_back(&draw);}
    size_t size()const{return draws_.size();}
    const StereoDrawPacket& operator[](size_t index)const{return *draws_[index];}
    Iterator begin()const{return Iterator(draws_.begin());}
    Iterator end()const{return Iterator(draws_.end());}
};
}

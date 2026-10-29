#pragma once
#include <d3d11.h>
#include <memory>
namespace lmuqv {
enum class NvVrsOwnership { Unknown, ExclusiveKnownDisabled };
using NvVrsLog=void(*)(void*,const char*);
class NvVrs {
    struct Impl;std::unique_ptr<Impl> impl_;
    bool resetFault_=false;
public:
    NvVrs();~NvVrs();
    NvVrs(const NvVrs&)=delete;NvVrs& operator=(const NvVrs&)=delete;
    bool configure(ID3D11DeviceContext*,unsigned,unsigned,unsigned rate=2,bool enable=false,
                   NvVrsOwnership ownership=NvVrsOwnership::Unknown,NvVrsLog callback=nullptr,void* user=nullptr);
    bool begin_outer(ID3D11DeviceContext*);
    bool end_outer(ID3D11DeviceContext*);
    void reset();
    bool ready()const;bool active()const;bool faulted()const;
};
}

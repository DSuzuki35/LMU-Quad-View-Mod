#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace lmuqv {
// Recover nonvolatile registers using the compiler's Windows unwind records.
// No native code is executed or modified by this reader. The return slot must
// be captured in the detour itself, not in an out-of-line helper.
inline bool capture_native_caller_context(uintptr_t returnAddress,uintptr_t returnSlot,
    CONTEXT& context,bool(*readable)(const void*,size_t)) noexcept {
    if(!returnAddress||!returnSlot||!readable||returnSlot>UINTPTR_MAX-8)return false;
    RtlCaptureContext(&context);
    for(unsigned i=0;i<8;++i){
        if(context.Rip==returnAddress&&context.Rsp==returnSlot+8)return true;
        if(!context.Rip||context.Rsp>returnSlot)return false;
        DWORD64 image=0;const auto entry=RtlLookupFunctionEntry(context.Rip,&image,nullptr);
        const auto previous=context.Rsp;
        if(entry){
            PVOID handler=nullptr;DWORD64 establisher=0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER,image,context.Rip,entry,&context,&handler,&establisher,nullptr);
        }else{
            if(context.Rsp>UINTPTR_MAX-8||!readable(reinterpret_cast<const void*>(context.Rsp),8))return false;
            std::memcpy(&context.Rip,reinterpret_cast<const void*>(context.Rsp),8);context.Rsp+=8;
        }
        if(context.Rsp<=previous)return false;
    }
    return false;
}
}

#pragma once

// Keep this at the detour call site. A helper function would observe its own
// caller instead of the pinned game's caller, especially in unoptimized tests.
#ifndef LMU_CALLER_ADDRESS
#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)
#define LMU_CALLER_ADDRESS _ReturnAddress()
#else
#define LMU_CALLER_ADDRESS __builtin_return_address(0)
#endif
#endif
#ifndef LMU_CALLER_RETURN_SLOT
#if defined(_MSC_VER)
#include <intrin.h>
#pragma intrinsic(_AddressOfReturnAddress)
#define LMU_CALLER_RETURN_SLOT _AddressOfReturnAddress()
#else
#define LMU_CALLER_RETURN_SLOT (static_cast<char*>(__builtin_frame_address(0))+sizeof(void*))
#endif
#endif

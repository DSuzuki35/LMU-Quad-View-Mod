#pragma once
#include "bridge.hpp"
#include "openxr/openxr_platform.h"
#include "openxr/openxr_loader_negotiation.h"

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo*, const char*, XrNegotiateApiLayerRequest*);

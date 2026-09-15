#pragma once

// Win32 SECURITY_ATTRIBUTES is a typedef (not struct SECURITY_ATTRIBUTES).
// Never forward-declare it - include Windows.h first.
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace rxext::genesis_remover {

// Everyone+Admin DACL for CreateSharedHandle (same as ExtSampleD3D).
const SECURITY_ATTRIBUTES& sharedSecurityAttributes();

} // namespace rxext::genesis_remover

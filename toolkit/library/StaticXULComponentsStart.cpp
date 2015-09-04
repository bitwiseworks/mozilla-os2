#include "mozilla/Module.h"

#if defined(__EMX__)
__asm__(".stabs \"___kPStaticModules__\", 21, 0, 0, 0xFFFFFFFF");
#else
NSMODULE_DEFN(start_kPStaticModules) = nullptr;
#endif

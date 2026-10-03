#pragma once

#if defined(BH_TRACY)
#include <tracy/Tracy.hpp>
#define BH_ZONE ZoneScoped
#else
#define BH_ZONE ((void)0)
#endif

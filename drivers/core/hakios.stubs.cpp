/*
 * The IOKit half of drivers/core/hakios.stubs.c: what the rest of the kernel calls in a driver that is left out of it,
 * for the calls that are C++ (docs/drivers-config.md).
 */
#include "MiniIOSurface.h"

#ifndef HAKIOS_ADRENO
/* the display's driver asks whether the GPU is done with a surface before it shows it: with no Adreno, only the Mali can say */
bool
MiniIOSurfaceDrawn(MiniIOSurface * surface, uint64_t milliseconds)
{
	return hakios_mali_surface_drawn(surface, milliseconds);
}
#endif

#ifndef HAKIOS_MALI
/* no Mali: none of its jobs draws into a surface */
bool
hakios_mali_surface_drawn(__unused MiniIOSurface * surface, __unused uint64_t milliseconds)
{
	return true;
}
#endif

#include "Pomme.h"
#include "PommeGraphics.h"
#include "PommeMemory.h"
#include "Utilities/structpack.h"

#include <iostream>
#include <cstring>

// ----------------------------------------------------------------------------
// Icons

static Handle Get4bitIconAsARGB(Handle colorIcon, Ptr bwMask, int width)
{
	int height = width;

	if (!colorIcon || !bwMask)
		return nil;

	if (width*height/2 != GetHandleSize(colorIcon))
		return nil;

	Handle icon = NewHandle(width * height * 4);

	for (int y = 0; y < height; y++)
	{
		uint32_t* out = (uint32_t*) (*icon + y * width * 4);

		uint32_t scanlineMask = 0xFFFFFFFF;
		if (!bwMask)
			;
		else if (width == 32)
			scanlineMask = UnpackU32BE(bwMask + y*4);
		else if (width == 16)
			scanlineMask = UnpackU16BE(bwMask + y*2);

		for (int x = 0; x < width; x++)
		{
			uint8_t paletteEntry = (*colorIcon)[y * (width>>1) + (x>>1)];
			if (!(x & 1))
				paletteEntry >>= 4;
			paletteEntry &= 0x0F;

			uint32_t argb = Pomme::Graphics::clut4[paletteEntry];

			bool masked = scanlineMask & (1 << (width - 1 - x));
			if (!masked)
				argb &= 0x00FFFFFF;

			*out++ = argb;
		}
	}

	return icon;
}

static Handle Get8bitIconAsARGB(Handle colorIcon, Ptr bwMask, int width)
{
	int height = width;

	if (!colorIcon || !bwMask)
		return nil;

	if (width*height != GetHandleSize(colorIcon))
		return nil;

	Handle icon = NewHandle(width * height * 4);

	for (int y = 0; y < height; y++)
	{
		uint32_t* out = (uint32_t*) (*icon + y * width * 4);

		uint32_t scanlineMask = 0xFFFFFFFF;
		if (!bwMask)
			;
		else if (width == 32)
			scanlineMask = UnpackU32BE(bwMask + y*4);
		else if (width == 16)
			scanlineMask = UnpackU16BE(bwMask + y*2);

		for (int x = 0; x < width; x++)
		{
			uint8_t paletteEntry = (*colorIcon)[y * width + x];
			uint32_t argb = Pomme::Graphics::clut8[paletteEntry];

			bool masked = scanlineMask & (1 << (width - 1 -x));
			if (!masked)
				argb &= 0x00FFFFFF;

			*out++ = argb;
		}
	}

	return icon;
}

Handle Pomme::Graphics::GetIcl8AsARGB(short id)
{
	Handle colorIcon	= GetResource('icl8', id);
	Handle bwIcon		= GetResource('ICN#', id);

	Pomme::Memory::DisposeHandleGuard autoDisposeColorIcon(colorIcon);
	Pomme::Memory::DisposeHandleGuard autoDisposeBwIcon(bwIcon);

	Ptr mask = nil;
	if (bwIcon && 256 == GetHandleSize(bwIcon))
		mask = *bwIcon + 128;		// Mask data for icl8 starts 128 bytes into ICN#

	return Get8bitIconAsARGB(colorIcon, mask, 32);
}

Handle Pomme::Graphics::GetIcs8AsARGB(short id)
{
	Handle colorIcon	= GetResource('ics8', id);
	Handle bwIcon		= GetResource('ics#', id);

	Pomme::Memory::DisposeHandleGuard autoDisposeColorIcon(colorIcon);
	Pomme::Memory::DisposeHandleGuard autoDisposeBwIcon(bwIcon);

	Ptr mask = nil;
	if (bwIcon && 64 == GetHandleSize(bwIcon))
		mask = *bwIcon + 32;		// Mask data for ics8 starts 32 bytes into ics#

	return Get8bitIconAsARGB(colorIcon, mask, 16);
}

Handle Pomme::Graphics::GetIcl4AsARGB(short id)
{
	Handle colorIcon	= GetResource('icl4', id);
	Handle bwIcon		= GetResource('ICN#', id);

	Pomme::Memory::DisposeHandleGuard autoDisposeColorIcon(colorIcon);
	Pomme::Memory::DisposeHandleGuard autoDisposeBwIcon(bwIcon);

	Ptr mask = nil;
	if (bwIcon && 256 == GetHandleSize(bwIcon))
		mask = *bwIcon + 128;		// Mask data for icl8 starts 128 bytes into ICN#

	return Get4bitIconAsARGB(colorIcon, mask, 32);
}

Handle Pomme::Graphics::GetIcs4AsARGB(short id)
{
	Handle colorIcon	= GetResource('ics4', id);
	Handle bwIcon		= GetResource('ics#', id);

	Pomme::Memory::DisposeHandleGuard autoDisposeColorIcon(colorIcon);
	Pomme::Memory::DisposeHandleGuard autoDisposeBwIcon(bwIcon);

	Ptr mask = nil;
	if (bwIcon && 64 == GetHandleSize(bwIcon))
		mask = *bwIcon + 32;		// Mask data for ics8 starts 32 bytes into ics#

	return Get4bitIconAsARGB(colorIcon, mask, 16);
}

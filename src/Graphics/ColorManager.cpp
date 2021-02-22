#include "Pomme.h"

void ProtectEntry(short index, Boolean protect)
{
	TODOMINOR();
}

void ReserveEntry(short index, Boolean reserve)
{
	TODOMINOR();
}

void SetEntries(short start, short count, CSpecArray aTable)
{
	TODOMINOR();
}

void GetEntryColor(PaletteHandle srcPalette, short srcEntry, RGBColor* dstRGB)
{
	TODOMINOR();
}

void SetEntryColor(PaletteHandle dstPalette, short dstEntry, const RGBColor* srcRGB)
{
	TODOMINOR();
}

PaletteHandle NewPalette(short entries, CTabHandle srcColors, short srcUsage, short srcTolerance)
{
	TODOMINOR();
	return nil;
}

void CopyPalette(PaletteHandle srcPalette, PaletteHandle dstPalette, short srcEntry,short dstEntry, short dstLength)
{
	TODOMINOR();
}

void RestoreDeviceClut(GDHandle gdh)
{
	TODOMINOR();
}
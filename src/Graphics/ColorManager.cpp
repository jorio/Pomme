#include "Pomme.h"

void ProtectEntry(short index, Boolean protect)
{
	(void) index;
	(void) protect;
	TODOMINOR();
}

void ReserveEntry(short index, Boolean reserve)
{
	(void) index;
	(void) reserve;
	TODOMINOR();
}

void SetEntries(short start, short count, CSpecArray aTable)
{
	(void) start;
	(void) count;
	(void) aTable;
	TODOMINOR();
}

void GetEntryColor(PaletteHandle srcPalette, short srcEntry, RGBColor* dstRGB)
{
	(void) srcPalette;
	(void) srcEntry;
	(void) dstRGB;
	TODOMINOR();
}

void SetEntryColor(PaletteHandle dstPalette, short dstEntry, const RGBColor* srcRGB)
{
	(void) dstPalette;
	(void) dstEntry;
	(void) srcRGB;
	TODOMINOR();
}

PaletteHandle NewPalette(short entries, CTabHandle srcColors, short srcUsage, short srcTolerance)
{
	(void) entries;
	(void) srcColors;
	(void) srcUsage;
	(void) srcTolerance;
	TODOMINOR();
	return nil;
}

void CopyPalette(PaletteHandle srcPalette, PaletteHandle dstPalette, short srcEntry, short dstEntry, short dstLength)
{
	(void) srcPalette;
	(void) dstPalette;
	(void) srcEntry;
	(void) dstEntry;
	(void) dstLength;
	TODOMINOR();
}

void RestoreDeviceClut(GDHandle gdh)
{
	(void) gdh;
	TODOMINOR();
}
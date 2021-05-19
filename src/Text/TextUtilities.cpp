#include "Pomme.h"
#include "PommeTypes.h"
#include "PommeDebug.h"

#include <cstring>
#include <Utilities/memstream.h>
#include <Utilities/bigendianstreams.h>

void NumToString(long theNum, Str255 theString)
{
	int rc = snprintf(theString+1, 254, "%ld", theNum);
	theString[0] = (rc < 0 || rc > 255) ? 0 : (unsigned char)rc;
}

int NumToStringC(long theNum, Str255 theString)
{
	return snprintf(theString, 256, "%ld", theNum);
}

void GetIndStringC(Str255 theStringC, short strListID, short index)
{
	theStringC[0] = '\0';

	Handle strListHandle = GetResource('STR#', strListID);

	if (!strListHandle)
		return;

	memstream substream(*strListHandle, GetHandleSize(strListHandle));
	Pomme::BigEndianIStream f(substream);

	int16_t nStrings = f.Read<int16_t>();

	if (index > nStrings)		// index starts at 1, hence '>' rather than '>='
	{
		ReleaseResource(strListHandle);
		return;
	}

	// Skip to requested string
	uint8_t pstrlen = 0;
	for (int i = 1; i < index; i++)		// index starts at 1
	{
		pstrlen = f.Read<uint8_t>();
		f.Skip(pstrlen);
	}

	pstrlen = f.Read<uint8_t>();
	f.Read(theStringC, pstrlen);
	theStringC[pstrlen] = '\0';
	static_assert(sizeof(Str255) == 256);

	ReleaseResource(strListHandle);
}

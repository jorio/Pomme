#include "Pomme.h"
#include "PommeFiles.h"
#include "PommeMemory.h"
#include "Utilities/bigendianstreams.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <cstring>
#include "CompilerSupport/filesystem.h"

#if _DEBUG
#include "PommeSound.h"
#endif

#define LOG POMME_GENLOG(POMME_DEBUG_RESOURCES, "RSRC")

using namespace Pomme;
using namespace Pomme::Files;

//-----------------------------------------------------------------------------
// State

static OSErr gLastResError = noErr;

static std::vector<ResourceFork> gResForkStack;

static int gResForkStackIndex = 0;

//-----------------------------------------------------------------------------
// Internal

static void ResourceAssert(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

static ResourceFork& GetCurRF()
{
	return gResForkStack[gResForkStackIndex];
}

//-----------------------------------------------------------------------------
// Resource file management

OSErr ResError(void)
{
	return gLastResError;
}

short FSpOpenResFile(const FSSpec* spec, char permission)
{
	short slot;

	gLastResError = FSpOpenRF(spec, permission, &slot);

	if (noErr != gLastResError)
	{
		return -1;
	}

	auto f = Pomme::BigEndianIStream(Pomme::Files::GetStream(slot));
	std::streamoff resForkOff = f.Tell();

	// ----------------
	// Load resource fork

	gResForkStack.emplace_back();
	gResForkStackIndex = int(gResForkStack.size() - 1);
	GetCurRF().fileRefNum = slot;
	GetCurRF().resourceMap.clear();

	// -------------------
	// Resource Header
	std::streamoff dataSectionOff = f.Read<UInt32>() + resForkOff;
	std::streamoff mapSectionOff = f.Read<UInt32>() + resForkOff;
	f.Skip(4); // UInt32 dataSectionLen
	f.Skip(4); // UInt32 mapSectionLen
	f.Skip(112 + 128); // system- (112) and app- (128) reserved data

	ResourceAssert(f.Tell() == dataSectionOff, "FSpOpenResFile: Unexpected data offset");

	f.Goto(mapSectionOff);

	// map header
	f.Skip(16 + 4 + 2); // junk
	f.Skip(2); // UInt16 fileAttr
	std::streamoff typeListOff = f.Read<UInt16>() + mapSectionOff;
	std::streamoff resNameListOff = f.Read<UInt16>() + mapSectionOff;

	// all resource types
	int nResTypes = 1 + f.Read<UInt16>();
	for (int i = 0; i < nResTypes; i++)
	{
		OSType resType = f.Read<OSType>();
		int    resCount = f.Read<UInt16>() + 1;
		std::streamoff resRefListOff = f.Read<UInt16>() + typeListOff;

		// The guard will rewind the file cursor to the pos in the next iteration
		auto guard1 = f.GuardPos();

		f.Goto(resRefListOff);

		for (int j = 0; j < resCount; j++)
		{
			SInt16 resID = f.Read<UInt16>();
			UInt16 resNameRelativeOff = f.Read<UInt16>();
			UInt32 resPackedAttr = f.Read<UInt32>();
			f.Skip(4); // junk

			// The guard will rewind the file cursor to the pos in the next iteration
			auto guard2 = f.GuardPos();

			// unpack attributes
			Byte   resFlags = (resPackedAttr & 0xFF000000) >> 24;
			std::streamoff resDataOff = (resPackedAttr & 0x00FFFFFF) + dataSectionOff;

			// Check compressed flag
			ResourceAssert(!(resFlags & 1), "FSpOpenResFile: Compressed resources not supported yet");

			// Fetch name
			std::string name;
			if (resNameRelativeOff != 0xFFFF)
			{
				f.Goto(resNameListOff + resNameRelativeOff);
				name = f.ReadPascalString();
			}

			// Fetch size
			f.Goto(resDataOff);
			SInt32 size = f.Read<SInt32>();

			ResourceMetadata resMetadata;
			resMetadata.forkRefNum = slot;
			resMetadata.type       = resType;
			resMetadata.id         = resID;
			resMetadata.flags      = resFlags;
			resMetadata.dataOffset = resDataOff + 4;
			resMetadata.size       = size;
			resMetadata.name       = name;
			GetCurRF().resourceMap[resType][resID] = resMetadata;
		}
	}

	//PrintStack(__func__);

	return slot;
}

short OpenResFile(const char* cName)
{
	FSSpec spec;
	FSMakeFSSpec(0, 0, cName, &spec);
	return FSpOpenResFile(&spec, fsRdPerm);
}

void UseResFile(short refNum)
{
	// See MoreMacintoshToolbox:1-69

	gLastResError = unimpErr;

	ResourceAssert(refNum != 0, "UseResFile: Using the System file's resource fork is not implemented.");
	ResourceAssert(refNum >= 0, "UseResFile: Illegal refNum");
	ResourceAssert(IsStreamOpen(refNum), "UseResFile: Resource stream not open");

	for (size_t i = 0; i < gResForkStack.size(); i++)
	{
		if (gResForkStack[i].fileRefNum == refNum)
		{
			gLastResError = noErr;
			gResForkStackIndex = (int) i;
			return;
		}
	}

	std::cerr << "no RF open with refNum " << rfNumErr << "\n";
	gLastResError = rfNumErr;
}

short CurResFile()
{
	return GetCurRF().fileRefNum;
}

void CloseResFile(short refNum)
{
	ResourceAssert(refNum != 0, "CloseResFile: Closing the System file's resource fork is not implemented.");
	ResourceAssert(refNum >= 0, "CloseResFile: Illegal refNum");
	ResourceAssert(IsStreamOpen(refNum), "CloseResFile: Resource stream not open");

	//UpdateResFile(refNum); // MMT:1-110
	Pomme::Files::CloseStream(refNum);

	auto it = gResForkStack.begin();
	while (it != gResForkStack.end())
	{
		if (it->fileRefNum == refNum)
			it = gResForkStack.erase(it);
		else
			it++;
	}

	gResForkStackIndex = std::min(gResForkStackIndex, (int) gResForkStack.size() - 1);
}

short Count1Resources(ResType theType)
{
	gLastResError = noErr;

	try
	{
		return (short) GetCurRF().resourceMap.at(theType).size();
	}
	catch (std::out_of_range&)
	{
		return 0;
	}
}

short Count1Types()
{
	return (short) GetCurRF().resourceMap.size();
}

void Get1IndType(ResType* theType, short index)
{
	const auto& resourceMap = GetCurRF().resourceMap;

	for (auto& it : resourceMap)
	{
		if (index == 1)			// remember, index is 1-based here
		{
			*theType = it.first;
			return;
		}

		index--;
	}

	*theType = 0;
}

Handle GetResource(ResType theType, short theID)
{
	gLastResError = noErr;

	for (int i = gResForkStackIndex; i >= 0; i--)
	{
		const auto& fork = gResForkStack[i];

		if (fork.resourceMap.end() == fork.resourceMap.find(theType))
			continue;

		auto& resourcesOfType = fork.resourceMap.at(theType);
		if (resourcesOfType.end() == resourcesOfType.find(theID))
			continue;

		// Found it!
		const auto& meta = fork.resourceMap.at(theType).at(theID);
		auto& forkStream = Pomme::Files::GetStream(gResForkStack[i].fileRefNum);

		// Allocate handle
		Handle handle = NewHandle(meta.size);

		// Set pointer to resource metadata
		Pomme::Memory::BlockDescriptor::HandleToBlock(handle)->rezMeta = &meta;

		forkStream.seekg(meta.dataOffset, std::ios::beg);
		forkStream.read(*handle, meta.size);

		return handle;
	}

	gLastResError = resNotFound;
	return nil;
}

Handle Get1IndResource(ResType theType, short index)
{
	gLastResError = noErr;

	const auto& idsToResources = GetCurRF().resourceMap.at(theType);

	for (auto& it : idsToResources)
	{
		if (index == 1)			// remember, index is 1-based here
		{
			return GetResource(theType, it.second.id);
		}

		index--;
	}

	gLastResError = resNotFound;
	return nullptr;
}

void GetResInfo(Handle theResource, short* theID, ResType* theType, char* name256)
{
	gLastResError = noErr;

	if (!theResource)
	{
		gLastResError = resNotFound;
		return;
	}

	auto blockDescriptor = Pomme::Memory::BlockDescriptor::HandleToBlock(theResource);

	if (!blockDescriptor->rezMeta)
	{
		gLastResError = resNotFound;
		return;
	}

	if (theID)
		*theID = blockDescriptor->rezMeta->id;

	if (theType)
		*theType = blockDescriptor->rezMeta->type;

	if (name256)
		snprintf(name256, 256, "%s", blockDescriptor->rezMeta->name.c_str());
}

void ReleaseResource(Handle theResource)
{
	DisposeHandle(theResource);
}

void RemoveResource(Handle theResource)
{
	DisposeHandle(theResource);
	TODO();
}

void AddResource(Handle theData, ResType theType, short theID, const char* name)
{
	(void) theData;
	(void) theType;
	(void) theID;
	(void) name;

	TODO();
}

void ChangedResource(Handle theResource)
{
	(void) theResource;

	TODO();
}

void WriteResource(Handle theResource)
{
	(void) theResource;

	TODO();
}

void DetachResource(Handle theResource)
{
	gLastResError = noErr;

	auto* blockDescriptor = Pomme::Memory::BlockDescriptor::HandleToBlock(theResource);

	if (!blockDescriptor->rezMeta)
		gLastResError = resNotFound;

	blockDescriptor->rezMeta = nullptr;
}

long GetResourceSizeOnDisk(Handle theResource)
{
	(void) theResource;

	TODO();
	return -1;
}

long SizeResource(Handle theResource)
{
	return GetResourceSizeOnDisk(theResource);
}

#pragma once

#include "PommeTypes.h"

#include <iostream>
#include <map>
#include "CompilerSupport/filesystem.h"

namespace Pomme::Files
{
	struct ResourceMetadata
	{
		short			forkRefNum;
		OSType			type;
		SInt16			id;
		Byte			flags;
		SInt32			size;
		std::streamoff	dataOffset;
		std::string		name;
	};

	struct ResourceFork
	{
		SInt16 fileRefNum;
		std::map<ResType, std::map<SInt16, ResourceMetadata> > resourceMap;
	};

	void Init();

	bool IsRefNumLegal(short refNum);

	bool IsStreamOpen(short refNum);

	bool IsStreamPermissionAllowed(short refNum, char perm);

	std::iostream& GetStream(short refNum);

	const FSSpec& GetSpec(short refNum);

	void CloseStream(short refNum);

	FSSpec HostPathToFSSpec(const fs::path& fullPath);
}

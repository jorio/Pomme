#include <iostream>
#include <vector>
#include <cstring>

#include "Pomme.h"
#include "Memory/BlockDescriptor.h"

using namespace Pomme;
using namespace Pomme::Memory;

#define LOG POMME_GENLOG(POMME_DEBUG_MEMORY, "MEMO")

//-----------------------------------------------------------------------------
// Implementation-specific stuff

BlockDescriptor::BlockDescriptor(Size theSize)
{
	buf = new char[theSize];
	magic = 'LIVE';
	size = theSize;
}

BlockDescriptor::~BlockDescriptor()
{
	delete[] buf;
	buf = nullptr;
	size = 0;
	rezMeta = nullptr;
	magic = 'DEAD';
}

BlockDescriptor* BlockDescriptor::HandleToBlock(Handle h)
{
	auto bd = (BlockDescriptor*) h;
	if (bd->magic != 'LIVE')
		throw std::runtime_error("corrupted handle");
	return bd;
}

//-----------------------------------------------------------------------------
// Memory: Handle

Handle NewHandle(Size size)
{
	if (size < 0)
		throw std::invalid_argument("trying to alloc negative size handle");

	BlockDescriptor* block = new BlockDescriptor(size);

	if ((Ptr) &block->buf != (Ptr) block)
		throw std::runtime_error("buffer address mismatches block address");

	return &block->buf;
}

Handle NewHandleClear(Size s)
{
	Handle h = NewHandle(s);
	memset(*h, 0, s);
	return h;
}

Handle NewHandleSys(Size s)
{
	return NewHandle(s);
}

Handle NewHandleSysClear(Size s)
{
	return NewHandleClear(s);
}

Handle TempNewHandle(Size s, OSErr* err)
{
	Handle h = NewHandle(s);
	*err = noErr;
	return h;
}

Size GetHandleSize(Handle h)
{
	return BlockDescriptor::HandleToBlock(h)->size;
}

void SetHandleSize(Handle handle, Size byteCount)
{
	TODOFATAL();
}

void DisposeHandle(Handle h)
{
	delete BlockDescriptor::HandleToBlock(h);
}

OSErr PtrToHand(const void* srcPtr, Handle* dstHndl, Size size)
{
	if (!dstHndl
		|| (!srcPtr && size > 0)
		|| size < 0)
	{
		return paramErr;
	}

	Handle h = NewHandle(size);
	if (!h)
		return memFullErr;

	*dstHndl = h;

	memcpy(*h, srcPtr, size);

	return noErr;
}

//-----------------------------------------------------------------------------
// Memory: Ptr

Ptr NewPtr(Size byteCount)
{
	if (byteCount < 0) throw std::invalid_argument("trying to NewPtr negative size");
	return new char[byteCount];
}

Ptr NewPtrSys(Size byteCount)
{
	if (byteCount < 0) throw std::invalid_argument("trying to NewPtrSys negative size");
	return new char[byteCount];
}

Ptr NewPtrClear(Size byteCount)
{
	if (byteCount < 0) throw std::invalid_argument("trying to NewPtrClear negative size");
	Ptr ptr = new char[byteCount];
	memset(ptr, 0, byteCount);
	return ptr;
}

void DisposePtr(Ptr p)
{
	delete[] p;
}

//-----------------------------------------------------------------------------
// Memory: BlockMove

void BlockMove(const void* srcPtr, void* destPtr, Size byteCount)
{
	memcpy(destPtr, srcPtr, byteCount);
}

void BlockMoveData(const void* srcPtr, void* destPtr, Size byteCount)
{
	TODOFATAL();
}

#include <iostream>
#include <cstring>

#include "Pomme.h"
#include "PommeMemory.h"

using namespace Pomme;
using namespace Pomme::Memory;

#define LOG POMME_GENLOG(POMME_DEBUG_MEMORY, "MEMO")

#if POMME_PTR_TRACKING
#include <set>
static uint32_t gCurrentPtrBatch = 0;
static uint32_t gCurrentNumPtrsInBatch = 0;
static std::set<uint32_t> gLivePtrNums;
#endif

static constexpr int kBlockDescriptorPadding = 32;
static_assert(sizeof(BlockDescriptor) <= kBlockDescriptorPadding);

static size_t gTotalHeapSize = 0;
static size_t gNumBlocksAllocated = 0;

//-----------------------------------------------------------------------------
// Implementation-specific stuff

BlockDescriptor* BlockDescriptor::Allocate(uint32_t size)
{
	char* buf = new char[kBlockDescriptorPadding + size];

	BlockDescriptor* block = (BlockDescriptor*) buf;

	block->magic = 'LIVE';
	block->size = size;
	block->ptrToData = buf + kBlockDescriptorPadding;
	block->rezMeta = nullptr;

	gTotalHeapSize += kBlockDescriptorPadding + size;
	gNumBlocksAllocated++;

#if POMME_PTR_TRACKING
	block->ptrBatch = gCurrentPtrBatch;
	block->ptrNumInBatch = gCurrentNumPtrsInBatch++;
	gLivePtrNums.insert(block->ptrNumInBatch);
#endif

	return block;
}

void BlockDescriptor::Free(BlockDescriptor* block)
{
	if (!block)
		return;

	gTotalHeapSize -= kBlockDescriptorPadding + block->size;
	gNumBlocksAllocated--;

	block->magic = 'DEAD';
	block->size = 0;
	block->ptrToData = nullptr;
	block->rezMeta = nullptr;
#if POMME_PTR_TRACKING
	if (block->ptrBatch == gCurrentPtrBatch)
		gLivePtrNums.erase(block->ptrNumInBatch);
#endif

	char* buf = (char*) block;
	delete[] buf;
}

void BlockDescriptor::CheckIsLive() const
{
	if (magic == 'DEAD')
		throw std::runtime_error("ptr/handle double free?");

	if (magic != 'LIVE')
		throw std::runtime_error("corrupted ptr/handle");
}

BlockDescriptor* BlockDescriptor::HandleToBlock(Handle h)
{
	if (!h || !*h)
		return nullptr;
	BlockDescriptor* bd = (BlockDescriptor*) (*h - kBlockDescriptorPadding);
	bd->CheckIsLive();
	return bd;
}

BlockDescriptor* BlockDescriptor::PtrToBlock(Ptr p)
{
	if (!p)
		return nullptr;
	BlockDescriptor* bd = (BlockDescriptor*) (p - kBlockDescriptorPadding);
	bd->CheckIsLive();
	return bd;
}

//-----------------------------------------------------------------------------
// Memory: Handle

Handle NewHandle(Size size)
{
	if (size < 0)
		throw std::invalid_argument("trying to alloc negative size handle");
	if (size > 0x7FFFFFFF)
		throw std::invalid_argument("trying to alloc massive handle");

	BlockDescriptor* block = BlockDescriptor::Allocate((UInt32) size);
	return &block->ptrToData;
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
	(void) handle;
	(void) byteCount;
	TODOFATAL();
}

void DisposeHandle(Handle h)
{
	BlockDescriptor::Free(BlockDescriptor::HandleToBlock(h));
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
	if (byteCount < 0)
		throw std::invalid_argument("trying to NewPtr negative size");
	if (byteCount > 0x7FFFFFFF)
		throw std::invalid_argument("trying to alloc massive ptr");

	BlockDescriptor* bd = BlockDescriptor::Allocate((UInt32) byteCount);
	return bd->ptrToData;
}

Ptr NewPtrSys(Size byteCount)
{
	return NewPtr(byteCount);
}

Ptr NewPtrClear(Size byteCount)
{
	Ptr ptr = NewPtr(byteCount);
	memset(ptr, 0, byteCount);
	return ptr;
}

Size GetPtrSize(Ptr p)
{
	const BlockDescriptor* block = BlockDescriptor::PtrToBlock(p);
	return block->size;
}

void DisposePtr(Ptr p)
{
	BlockDescriptor::Free(BlockDescriptor::PtrToBlock(p));
}

//-----------------------------------------------------------------------------
// Memory: pointer tracking

long Pomme_GetNumAllocs()
{
	return (long) gNumBlocksAllocated;
}

Size Pomme_GetHeapSize()
{
	return (Size) gTotalHeapSize;
}

void Pomme_FlushPtrTracking(bool issueWarnings)
{
#if POMME_PTR_TRACKING
	if (issueWarnings && !gLivePtrNums.empty())
	{
		for (uint32_t ptrNum : gLivePtrNums)
			printf("%s: ptr/handle %d:%d is still live!\n", __func__, gCurrentPtrBatch, ptrNum);
	}

	gLivePtrNums.clear();
	gCurrentPtrBatch++;
	gCurrentNumPtrsInBatch = 0;
#else
	(void) issueWarnings;
#endif
}

//-----------------------------------------------------------------------------
// Memory: BlockMove

void BlockMove(const void* srcPtr, void* destPtr, Size byteCount)
{
	memcpy(destPtr, srcPtr, byteCount);
}

void BlockMoveData(const void* srcPtr, void* destPtr, Size byteCount)
{
	(void) srcPtr;
	(void) destPtr;
	(void) byteCount;
	TODOFATAL();
}

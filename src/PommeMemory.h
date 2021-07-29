#pragma once

#if !defined(POMME_PTR_TRACKING)
	#define POMME_PTR_TRACKING		_DEBUG
#endif

namespace Pomme::Files
{
	struct ResourceMetadata;
}

namespace Pomme::Memory
{
	struct BlockDescriptor
	{
		uint32_t magic;
		uint32_t size;
		uint32_t ptrBatch;
		uint32_t ptrNumInBatch;
		Ptr ptrToData;
		const Pomme::Files::ResourceMetadata* rezMeta;

		static BlockDescriptor* Allocate(uint32_t size);

		static void Free(BlockDescriptor* block);

		void CheckIsLive() const;

		static BlockDescriptor* HandleToBlock(Handle h);

		static BlockDescriptor* PtrToBlock(Ptr p);
	};

	class DisposeHandleGuard
	{
	public:
		DisposeHandleGuard(Handle theHandle)
			: h(theHandle)
		{}

		~DisposeHandleGuard()
		{
			if (h)
				DisposeHandle(h);
		}

	private:
		Handle h;
	};
}

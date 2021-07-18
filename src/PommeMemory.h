#pragma once

namespace Pomme::Files
{
	struct ResourceMetadata;
}

namespace Pomme::Memory
{
	class BlockDescriptor
	{
	public:
		Ptr buf;
		OSType magic;
		Size size;
		const Pomme::Files::ResourceMetadata* rezMeta;

		BlockDescriptor(Size theSize);

		~BlockDescriptor();

		static BlockDescriptor* HandleToBlock(Handle h);
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

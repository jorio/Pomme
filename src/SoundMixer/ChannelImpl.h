#pragma once

#include "Pomme.h"
#include "SoundMixer/cmixer.h"

enum ApplyParametersMask
{
	kApplyParameters_PanAndGain		= 1 << 0,
	kApplyParameters_Pitch			= 1 << 1,
	kApplyParameters_Loop			= 1 << 2,
	kApplyParameters_Interpolation	= 1 << 3,
	kApplyParameters_All            = 0xFFFFFFFF
};

struct ChannelImpl
{
private:
	ChannelImpl* prev;
	ChannelImpl* next;

public:
	// Pointer to application-facing interface
	SndChannelPtr macChannel;

	bool macChannelStructAllocatedByPomme;
	cmixer::WavStream source;

	// Parameters coming from Mac sound commands, passed back to cmixer source
	double pan;
	double gain;
	Byte baseNote;
	Byte playbackNote;
	double pitchMult;
	bool loop;
	bool interpolate;

	ChannelImpl(SndChannelPtr _macChannel, bool transferMacChannelOwnership);

	~ChannelImpl();

	void Recycle();

	void SetInitializationParameters(long initBits);

	void ApplyParametersToSource(int mask);

	inline ChannelImpl* GetPrev() const
	{
		return prev;
	}

	inline ChannelImpl* GetNext() const
	{
		return next;
	}

	inline void SetPrev(ChannelImpl* newPrev)
	{
		prev = newPrev;
	}

	inline void SetNext(ChannelImpl* newNext)
	{
		next = newNext;
		macChannel->nextChan = newNext ? newNext->macChannel : nullptr;
	}

	void Link();

	void Unlink();
};

#include "PommeSound.h"
#include "SoundMixer/ChannelImpl.h"
#include <cassert>

#ifndef POMME_MAX_CHANNEL_GAIN
	#define POMME_MAX_CHANNEL_GAIN 2.5
#endif

namespace Pomme::Sound
{
	extern ChannelImpl* gHeadChan;
	extern int gNumManagedChans;
}

ChannelImpl::ChannelImpl(SndChannelPtr _macChannel, bool transferMacChannelOwnership)
	: macChannel(_macChannel)
	, macChannelStructAllocatedByPomme(transferMacChannelOwnership)
	, source()
	, pan(0.0)
	, gain(1.0)
	, baseNote(kMiddleC)
	, playbackNote(kMiddleC)
	, pitchMult(1.0)
	, loop(false)
	, interpolate(false)
{
	macChannel->channelImpl = (Ptr) this;

	Link();  // Link chan into our list of managed chans
}

ChannelImpl::~ChannelImpl()
{
	Unlink();  // Unlink chan from list of managed chans

	macChannel->channelImpl = nullptr;

	if (macChannelStructAllocatedByPomme)
	{
		delete macChannel;
	}

	// Make sure we've stopped mixing the source before we allow its destructor
	// to be called. Otherwise, the WavSource's buffer may be freed as it is still
	// being processed!
	source.RemoveFromMixer();
}

void ChannelImpl::Recycle()
{
	source.Clear();
}

void ChannelImpl::SetInitializationParameters(long initBits)
{
	interpolate = !(initBits & initNoInterp);
	source.SetInterpolation(interpolate);
}

void ChannelImpl::ApplyParametersToSource(int mask)
{
	// Pitch
	if (mask & kApplyParameters_Pitch)
	{
		double baseFreq = Pomme::Sound::GetMidiNoteFrequency(baseNote);
		double playbackFreq = Pomme::Sound::GetMidiNoteFrequency(playbackNote);
		source.SetPitch(pitchMult * playbackFreq / baseFreq);
	}

	// Pan and gain
	if (mask & kApplyParameters_PanAndGain)
	{
		if (gain > POMME_MAX_CHANNEL_GAIN)
		{
#if _DEBUG
			printf("Capping extreme channel gain (%f)\n", gain);
#endif
			gain = POMME_MAX_CHANNEL_GAIN;
		}

		source.SetPan(pan);
		source.SetGain(gain);
	}

	// Interpolation
	if (mask & kApplyParameters_Interpolation)
	{
		source.SetInterpolation(interpolate);
	}

	// Interpolation
	if (mask & kApplyParameters_Loop)
	{
		source.SetLoop(loop);
	}
}

void ChannelImpl::Link()
{
	if (!Pomme::Sound::gHeadChan)
	{
		SetNext(nullptr);
	}
	else
	{
		assert(nullptr == Pomme::Sound::gHeadChan->GetPrev());
		Pomme::Sound::gHeadChan->SetPrev(this);
		SetNext(Pomme::Sound::gHeadChan);
	}

	Pomme::Sound::gHeadChan = this;
	SetPrev(nullptr);

	Pomme::Sound::gNumManagedChans++;
}

void ChannelImpl::Unlink()
{
	if (Pomme::Sound::gHeadChan == this)
	{
		Pomme::Sound::gHeadChan = GetNext();
	}

	if (nullptr != GetPrev())
	{
		GetPrev()->SetNext(GetNext());
	}

	if (nullptr != GetNext())
	{
		GetNext()->SetPrev(GetPrev());
	}

	SetPrev(nullptr);
	SetNext(nullptr);

	Pomme::Sound::gNumManagedChans--;
}

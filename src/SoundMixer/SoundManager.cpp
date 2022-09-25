#include "Pomme.h"
#include "PommeFiles.h"
#include "PommeSound.h"
#include "SoundMixer/ChannelImpl.h"
#include "SoundMixer/cmixer.h"
#include "Utilities/bigendianstreams.h"
#include "Utilities/IEEEExtended.h"
#include "Utilities/memstream.h"

#include <thread>
#include <chrono>
#include <iostream>
#include <cassert>
#include <cstring>

#define LOG POMME_GENLOG(POMME_DEBUG_SOUND, "SOUN")
#define LOG_NOPREFIX POMME_GENLOG_NOPREFIX(POMME_DEBUG_SOUND)

//-----------------------------------------------------------------------------
// Internal channel info

namespace Pomme::Sound
{
	struct ChannelImpl* gHeadChan = nullptr;
	int gNumManagedChans = 0;
}

//-----------------------------------------------------------------------------
// Internal utilities

static inline ChannelImpl& GetChannelImpl(SndChannelPtr chan)
{
	return *(ChannelImpl*) chan->channelImpl;
}

//-----------------------------------------------------------------------------
// Sound Manager

OSErr GetDefaultOutputVolume(long* stereoLevel)
{
	unsigned short g = (unsigned short) (cmixer::GetMasterGain() * 256.0);
	*stereoLevel = (g << 16) | g;
	return noErr;
}

// See IM:S:2-139, "Controlling Volume Levels".
OSErr SetDefaultOutputVolume(long stereoLevel)
{
	unsigned short left  = 0xFFFF & stereoLevel;
	unsigned short right = 0xFFFF & (stereoLevel >> 16);
	if (right != left)
		TODOMINOR2("setting different volumes for left & right is not implemented");
	LOG << left / 256.0 << "\n";
	cmixer::SetMasterGain(left / 256.0);
	return noErr;
}

// IM:S:2-127
OSErr SndNewChannel(SndChannelPtr* macChanPtr, short synth, long init, SndCallBackProcPtr userRoutine)
{
	if (synth != sampledSynth)
	{
		TODO2("unimplemented synth type " << sampledSynth);
		return unimpErr;
	}

	//---------------------------
	// Allocate Mac channel record if needed

	bool transferMacChannelOwnership = false;

	if (!*macChanPtr)
	{
		*macChanPtr = new SndChannel;
		(**macChanPtr) = {};
		transferMacChannelOwnership = true;
	}

	//---------------------------
	// Set up

	(**macChanPtr).callBack = userRoutine;
	auto channelImpl = new ChannelImpl(*macChanPtr, transferMacChannelOwnership);

	channelImpl->SetInitializationParameters(init);

	//---------------------------
	// Done

	LOG << "New channel created, init = $" << std::hex << init << std::dec
		<< ", total managed channels = " << Pomme::Sound::gNumManagedChans << "\n";

	return noErr;
}

// IM:S:2-129
OSErr SndDisposeChannel(SndChannelPtr macChanPtr, Boolean quietNow)
{
	if (!quietNow)
	{
		TODO2("SndDisposeChannel: quietNow == false is not implemented");
	}
	delete &GetChannelImpl(macChanPtr);
	return noErr;
}

OSErr SndChannelStatus(SndChannelPtr chan, short theLength, SCStatusPtr theStatus)
{
	// Must have room to write an entire SCStatus struct
	if ((size_t) theLength < sizeof(SCStatus))
	{
		return paramErr;
	}

	*theStatus = {};

	auto& source = GetChannelImpl(chan).source;

	int state = source.GetState();

	theStatus->scChannelPaused = state == cmixer::CM_STATE_PAUSED;
	theStatus->scChannelBusy   = state != cmixer::CM_STATE_STOPPED;

	return noErr;
}

// Install a sampled sound as a voice in a channel.
static void InstallSoundInChannel(SndChannelPtr chan, const Ptr sampledSoundHeader, bool forceCopy=false)
{
	//---------------------------------
	// Get internal channel

	auto& impl = GetChannelImpl(chan);
	impl.Recycle();

	//---------------------------------
	// Distill sound info

	Pomme::Sound::SampledSoundInfo info;
	GetSoundInfo(sampledSoundHeader, info);

	//---------------------------------
	// Set cmixer source data

	auto spanIn = std::span(info.dataStart, info.compressedLength);

	if (info.isCompressed)
	{
		auto spanOut = impl.source.GetBuffer(info.decompressedLength);

		std::unique_ptr<Pomme::Sound::Codec> codec = Pomme::Sound::GetCodec(info.compressionType);
		codec->Decode(info.nChannels, spanIn, spanOut);
		impl.source.Init(info.sampleRate, 16, info.nChannels, kIsBigEndianNative, spanOut);
	}
	else if (forceCopy)
	{
		auto spanOut = impl.source.GetBuffer(info.decompressedLength);
		memcpy(spanOut.data(), spanIn.data(), spanIn.size());
		impl.source.Init(info.sampleRate, info.codecBitDepth, info.nChannels, info.bigEndian, spanOut);
	}
	else
	{
		impl.source.Init(info.sampleRate, info.codecBitDepth, info.nChannels, info.bigEndian, spanIn);
	}

	//---------------------------------
	// Base note

	impl.baseNote = info.baseNote;

	//---------------------------------
	// Loop

	if (info.loopEnd - info.loopStart >= 2)
	{
		impl.source.SetLoop(true);

		// Set sustain loop start frame
		if ((int) info.loopStart >= impl.source.length)
		{
			TODO2("Warning: Illegal sustain loop start frame");
		}
		else
		{
			impl.source.sustainOffset = info.loopStart;
		}

		// Check sustain loop end frame
		if ((int) info.loopEnd != impl.source.length)
		{
			TODO2("Warning: Unsupported sustain loop end frame");
		}
	}

	//---------------------------------
	// Pass Mac channel parameters to cmixer source.

	// The loop param is a special case -- we're detecting it automatically according
	// to the sound header. If your application needs to force set the loop, it must
	// issue pommeSetLoopCmd *after* bufferCmd/soundCmd.
	impl.ApplyParametersToSource(kApplyParameters_All & ~kApplyParameters_Loop);

	// Get it going!
	impl.source.Play();
}

OSErr SndDoImmediate(SndChannelPtr chan, const SndCommand* cmd)
{
	auto& impl = GetChannelImpl(chan);

	// Discard the high bit of the command (it indicates whether an 'snd ' resource has associated data).
	switch (cmd->cmd & 0x7FFF)
	{
	case nullCmd:
		break;

	case flushCmd:
		// flushCmd is a no-op for now because we don't support queuing commands--
		// all commands are executed immediately in the current implementation.
		break;

	case quietCmd:
		impl.source.Stop();
		break;

	case bufferCmd:
	case soundCmd:
		InstallSoundInChannel(chan, cmd->ptr);
		GetChannelImpl(chan).source.Play();
		break;

	case ampCmd:
		impl.gain = cmd->param1 / 256.0;
		impl.ApplyParametersToSource(kApplyParameters_PanAndGain);
		break;

	case volumeCmd:
	{
		uint16_t lvol = (cmd->param2      ) & 0xFFFF;
		uint16_t rvol = (cmd->param2 >> 16) & 0xFFFF;
		uint32_t volsum = lvol + rvol;

		double pan = 0;
		if (volsum != 0)  // don't divide by zero
		{
			pan = (double)rvol / volsum;
			pan = 2*pan - 1;  // Transpose pan from [0...1] to [-1...+1]
		}

		impl.pan = pan;
		impl.gain = std::max(lvol, rvol) / 256.0;

		impl.ApplyParametersToSource(kApplyParameters_PanAndGain);
		break;
	}

	case freqCmd:
		LOG << "freqCmd " << cmd->param2 << " "
			<< Pomme::Sound::GetMidiNoteName(cmd->param2) << " " << Pomme::Sound::GetMidiNoteFrequency(cmd->param2) << "\n";
		impl.playbackNote = Byte(cmd->param2);
		impl.ApplyParametersToSource(kApplyParameters_Pitch);
		break;

	case rateCmd:
		// IM:S says it's a fixed-point multiplier of 22KHz, but Nanosaur uses rate "1" everywhere,
		// even for sounds sampled at 44Khz, so I'm treating it as just a pitch multiplier.
		impl.pitchMult = cmd->param2 / 65536.0;
		impl.ApplyParametersToSource(kApplyParameters_Pitch);
		break;

	case rateMultiplierCmd:
		impl.pitchMult = cmd->param2 / 65536.0;
		impl.ApplyParametersToSource(kApplyParameters_Pitch);
		break;

	case reInitCmd:
		impl.SetInitializationParameters(cmd->param2);
		break;

	case pommeSetLoopCmd:
		impl.loop = cmd->param1;
		impl.ApplyParametersToSource(kApplyParameters_Loop);
		break;

	case pommePausePlaybackCmd:
		if (impl.source.state == cmixer::CM_STATE_PLAYING)
		{
			impl.source.Pause();
		}
		break;

	case pommeResumePlaybackCmd:
		if (impl.source.state == cmixer::CM_STATE_PAUSED)	// only resume paused channels -- don't resurrect stopped channels
		{
			impl.source.Play();
		}
		break;

	default:
		TODOMINOR2(cmd->cmd << "(" << cmd->param1 << "," << cmd->param2 << ")");
	}

	return noErr;
}

// Not implemented yet, but you can probably use SndDoImmediateInstead.
OSErr SndDoCommand(SndChannelPtr chan, const SndCommand* cmd, Boolean noWait)
{
	(void) chan;
	(void) cmd;
	(void) noWait;
	TODOMINOR2("SndDoCommand isn't implemented yet, but you can probably use SndDoImmediate instead.");
	return noErr;
}

OSErr SndStartFilePlay(
	SndChannelPtr						chan,	
	short								fRefNum,
	short								resNum,
	long								bufferSize,
	Ptr									theBuffer,
	/*AudioSelectionPtr*/ void*			theSelection,
	FilePlayCompletionUPP				theCompletion,
	Boolean								async)
{
	(void) bufferSize;
	(void) theBuffer;

	if (resNum != 0)
	{
		TODO2("playing snd resource not implemented yet, resource " << resNum);
		return unimpErr;
	}

	if (!chan)
	{
		if (async) // async requires passing in a channel
			return badChannel;
		TODO2("nullptr chan for sync play, check IM:S:1-37");
		return unimpErr;
	}

	if (theSelection)
	{
		TODO2("audio selection record not implemented");
		return unimpErr;
	}

	SndListHandle sndListHandle = Pomme_SndLoadFileAsResource(fRefNum);

	if (!sndListHandle)
	{
		return badFileFormat;
	}

	long offset = 0;
	GetSoundHeaderOffset(sndListHandle, &offset);
	InstallSoundInChannel(chan, ((Ptr) *sndListHandle) + offset, true);
	DisposeHandle((Handle) sndListHandle);
	sndListHandle = nullptr;

	auto& impl = GetChannelImpl(chan);
	if (theCompletion)
	{
		impl.source.onComplete = [=]() { theCompletion(chan); };
	}
	impl.source.Play();

	if (!async)
	{
		while (impl.source.GetState() != cmixer::CM_STATE_STOPPED)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}
		impl.Recycle();
		return noErr;
	}

	return noErr;
}

OSErr SndPauseFilePlay(SndChannelPtr chan)
{
	// TODO: check that chan is being used for play from disk
	GetChannelImpl(chan).source.TogglePause();
	return noErr;
}

OSErr SndStopFilePlay(SndChannelPtr chan, Boolean quietNow)
{
	// TODO: check that chan is being used for play from disk
	if (!quietNow)
		TODO2("quietNow==false not supported yet, sound will be cut off immediately instead");
	GetChannelImpl(chan).source.Stop();
	return noErr;
}

NumVersion SndSoundManagerVersion()
{
	NumVersion v = {};
	v.majorRev = 3;
	v.minorAndBugRev = 9;
	v.stage = 0x80;
	v.nonRelRev = 0;
	return v;
}

//-----------------------------------------------------------------------------
// Init Sound Manager

void Pomme::Sound::InitMixer()
{
	cmixer::InitWithSDL();
}

void Pomme::Sound::ShutdownMixer()
{
	while (Pomme::Sound::gHeadChan)
	{
		SndDisposeChannel(Pomme::Sound::gHeadChan->macChannel, true);
	}
	cmixer::ShutdownWithSDL();
}

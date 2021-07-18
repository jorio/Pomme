#include "Pomme.h"
#include "PommeFiles.h"
#include "cmixer.h"
#include "PommeSound.h"
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

static struct ChannelImpl* headChan = nullptr;
static int nManagedChans = 0;
static double midiNoteFrequencies[128];

//-----------------------------------------------------------------------------
// Internal channel info

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

	bool temporaryPause = false;

	ChannelImpl(SndChannelPtr _macChannel, bool transferMacChannelOwnership)
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

	~ChannelImpl()
	{
		Unlink();

		macChannel->channelImpl = nullptr;

		if (macChannelStructAllocatedByPomme)
		{
			delete macChannel;
		}
	}

	void Recycle()
	{
		source.Clear();
	}

	void SetInitializationParameters(long initBits)
	{
		interpolate = !(initBits & initNoInterp);
		source.SetInterpolation(interpolate);
	}

	void ApplyParametersToSource(uint32_t mask, bool evenIfInactive = false)
	{
		if (!evenIfInactive && !source.active)
		{
			return;
		}

		// Pitch
		if (mask & kApplyParameters_Pitch)
		{
			double baseFreq = midiNoteFrequencies[baseNote];
			double playbackFreq = midiNoteFrequencies[playbackNote];
			source.SetPitch(pitchMult * playbackFreq / baseFreq);
		}

		// Pan and gain
		if (mask & kApplyParameters_PanAndGain)
		{
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

	ChannelImpl* GetPrev() const
	{
		return prev;
	}

	ChannelImpl* GetNext() const
	{
		return next;
	}

	void SetPrev(ChannelImpl* newPrev)
	{
		prev = newPrev;
	}

	void SetNext(ChannelImpl* newNext)
	{
		next = newNext;
		macChannel->nextChan = newNext ? newNext->macChannel : nullptr;
	}

	void Link()
	{
		if (!headChan)
		{
			SetNext(nullptr);
		}
		else
		{
			assert(nullptr == headChan->GetPrev());
			headChan->SetPrev(this);
			SetNext(headChan);
		}

		headChan = this;
		SetPrev(nullptr);

		nManagedChans++;
	}

	void Unlink()
	{
		if (headChan == this)
		{
			headChan = GetNext();
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

		nManagedChans--;
	}
};

//-----------------------------------------------------------------------------
// Internal utilities

static inline ChannelImpl& GetImpl(SndChannelPtr chan)
{
	return *(ChannelImpl*) chan->channelImpl;
}

//-----------------------------------------------------------------------------
// MIDI note utilities

// Note: these names are according to IM:S:2-43.
// These names won't match real-world names.
// E.g. for note 67 (A 440Hz), this will return "A6", whereas the real-world
// convention for that note is "A4".
static std::string GetMidiNoteName(int i)
{
	static const char* gamme[12] = {"A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#"};

	int octave = 1 + (i + 3) / 12;
	int semitonesFromA = (i + 3) % 12;

	std::stringstream ss;
	ss << gamme[semitonesFromA] << octave;
	return ss.str();
}

static void InitMidiFrequencyTable()
{
	// powers of twelfth root of two
	double gamme[12];
	gamme[0] = 1.0;
	for (int i = 1; i < 12; i++)
	{
		gamme[i] = gamme[i - 1] * 1.059630943592952646;
	}

	for (int i = 0; i < 128; i++)
	{
		int octave = 1 + (i + 3) / 12; // A440 and middle C are in octave 7
		int semitone = (i + 3) % 12; // halfsteps up from A in current octave
		if (octave < 7)
			midiNoteFrequencies[i] = gamme[semitone] * 440.0 / (1 << (7 - octave)); // 440/(2**octaveDiff)
		else
			midiNoteFrequencies[i] = gamme[semitone] * 440.0 * (1 << (octave - 7)); // 440*(2**octaveDiff)
		//LOG << i << "\t" << GetMidiNoteName(i) << "\t" << midiNoteFrequencies[i] << "\n";
	}
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

	LOG << "New channel created, init = $" << std::hex << init << std::dec << ", total managed channels = " << nManagedChans << "\n";

	return noErr;
}

// IM:S:2-129
OSErr SndDisposeChannel(SndChannelPtr macChanPtr, Boolean quietNow)
{
	if (!quietNow)
	{
		TODO2("SndDisposeChannel: quietNow == false is not implemented");
	}
	delete &GetImpl(macChanPtr);
	return noErr;
}

OSErr SndChannelStatus(SndChannelPtr chan, short theLength, SCStatusPtr theStatus)
{
	*theStatus = {};

	auto& source = GetImpl(chan).source;

	theStatus->scChannelPaused = source.GetState() == cmixer::CM_STATE_PAUSED;
	theStatus->scChannelBusy   = source.GetState() == cmixer::CM_STATE_PLAYING;

	return noErr;
}

// Install a sampled sound as a voice in a channel.
static void InstallSoundInChannel(SndChannelPtr chan, const Ptr sampledSoundHeader)
{
	//---------------------------------
	// Get internal channel

	auto& impl = GetImpl(chan);
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
		impl.source.Init(info.sampleRate, 16, info.nChannels, false, spanOut);
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

		if (info.loopStart != 0)
			TODO2("Warning: looping on a portion of the snd isn't supported yet");
	}

	//---------------------------------
	// Pass Mac channel parameters to cmixer source.

	// The loop param is a special case -- we're detecting it automatically according
	// to the sound header. If your application needs to force set the loop, it must
	// issue pommeSetLoopCmd *after* bufferCmd/soundCmd.
	impl.ApplyParametersToSource(kApplyParameters_All & ~kApplyParameters_Loop, true);

	// Override systemwide audio pause.
	impl.temporaryPause = false;

	// Get it going!
	impl.source.Play();
}

OSErr SndDoImmediate(SndChannelPtr chan, const SndCommand* cmd)
{
	auto& impl = GetImpl(chan);

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
		LOG << "freqCmd " << cmd->param2 << " " << GetMidiNoteName(cmd->param2) << " " << midiNoteFrequencies[cmd->param2] << "\n";
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

	default:
		TODOMINOR2(cmd->cmd << "(" << cmd->param1 << "," << cmd->param2 << ")");
	}

	return noErr;
}

// Not implemented yet, but you can probably use SndDoImmediateInstead.
OSErr SndDoCommand(SndChannelPtr chan, const SndCommand* cmd, Boolean noWait)
{
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

	auto& impl = GetImpl(chan);
	impl.Recycle();

	auto& stream = Pomme::Files::GetStream(fRefNum);
	// Rewind -- the file might've been fully played already and we might just be trying to loop it
	stream.seekg(0, std::ios::beg);
	Pomme::Sound::ReadAIFF(stream, impl.source);

	if (theCompletion)
	{
		impl.source.onComplete = [=]() { theCompletion(chan); };
	}

	impl.temporaryPause = false;
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
	GetImpl(chan).source.TogglePause();
	return noErr;
}

OSErr SndStopFilePlay(SndChannelPtr chan, Boolean quietNow)
{
	// TODO: check that chan is being used for play from disk
	if (!quietNow)
		TODO2("quietNow==false not supported yet, sound will be cut off immediately instead");
	GetImpl(chan).source.Stop();
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
// Extension: pause/unpause channels that are currently playing

void Pomme_PauseAllChannels(Boolean pause)
{
	for (auto* chan = headChan; chan; chan = chan->GetNext())
	{
		auto& source = chan->source;
		if (pause && source.state == cmixer::CM_STATE_PLAYING && !chan->temporaryPause)
		{
			source.Pause();
			chan->temporaryPause = true;
		}
		else if (!pause && source.state == cmixer::CM_STATE_PAUSED && chan->temporaryPause)
		{
			source.Play();
			chan->temporaryPause = false;
		}
	}
}

//-----------------------------------------------------------------------------
// Init Sound Manager

void Pomme::Sound::Init()
{
	InitMidiFrequencyTable();
	cmixer::InitWithSDL();
}

void Pomme::Sound::Shutdown()
{
	cmixer::ShutdownWithSDL();
	while (headChan)
	{
		SndDisposeChannel(headChan->macChannel, true);
	}
}

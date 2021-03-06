#include "Pomme.h"
#include "PommeFiles.h"
#include "Sound/cmixer.h"
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
// Cookie-cutter sound command list.
// Used to generate 'snd ' resources.

static const uint8_t kSampledSoundCommandList[20] = {
	0,1,			// format
	0,1,			// modifier count
	0,5,			// modifier "sampled synth"
	0,0,0,0,		// init bits
	0,1,			// command count
	0x80,soundCmd,	// command soundCmd (high bit set)
	0,0,			// param1
	0,0,0,20,		// param2 (offset)
	// Sample data follows
};

constexpr int kSampledSoundCommandListLength = sizeof(kSampledSoundCommandList);

//-----------------------------------------------------------------------------
// 'snd ' resource header

struct SampledSoundHeader
{
	UInt32	zero;
	union						// the meaning of union this is decided by the encoding type
	{
		SInt32	stdSH_nBytes;
		SInt32	cmpSH_nChannels;
		SInt32	extSH_nChannels;
		SInt32	nativeSH_nBytes;
	};
	UnsignedFixed	fixedSampleRate;
	UInt32	loopStart;
	UInt32	loopEnd;
	Byte	encoding;
	Byte	baseFrequency;		 // 0-127, see Table 2-2, IM:S:2-43
};

static_assert(sizeof(SampledSoundHeader) >= 22 && sizeof(SampledSoundHeader) <= 24,
	"unexpected SampledSoundHeader size");

constexpr int kSampledSoundHeaderLength = 22;
constexpr const char* kSampledSoundHeaderPackFormat = "IiIIIbb";

enum SampledSoundEncoding
{
	stdSH				= 0x00,
	nativeSH_mono16		= 0x10,		// pomme extension
	nativeSH_stereo16	= 0x11,		// pomme extension
	cmpSH				= 0xFE,
	extSH				= 0xFF,
};

struct SampledSoundInfo
{
	int16_t nChannels;
	uint32_t nPackets;
	int16_t codecBitDepth;
	bool bigEndian;
	double sampleRate;
	bool isCompressed;
	uint32_t compressionType;
	char* dataStart;
	int compressedLength;
	int decompressedLength;
	int8_t baseNote;
	uint32_t loopStart;
	uint32_t loopEnd;
};

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

static void GetSoundInfo(const Ptr sndhdr, SampledSoundInfo& info)
{
	// Prep the BE reader on the header.
	memstream headerInput(sndhdr, kSampledSoundHeaderLength + 42);
	Pomme::BigEndianIStream f(headerInput);

	// Read in SampledSoundHeader and unpack it.
	SampledSoundHeader header;
	f.Read(reinterpret_cast<char*>(&header), kSampledSoundHeaderLength);
	ByteswapStructs(kSampledSoundHeaderPackFormat, kSampledSoundHeaderLength, 1, reinterpret_cast<char*>(&header));

	if (header.zero != 0)
	{
		// The first field can be a pointer to the sampled-sound data.
		// In practice it's always gonna be 0.
		TODOFATAL2("expected 0 at the beginning of an snd");
	}


	memset(&info, 0, sizeof(info));

	info.sampleRate = static_cast<uint32_t>(header.fixedSampleRate) / 65536.0;
	info.baseNote = header.baseFrequency;
	info.loopStart = header.loopStart;
	info.loopEnd = header.loopEnd;

	switch (header.encoding)
	{
	case 0x00: // stdSH - standard sound header (noncompressed 8-bit mono sample data)
		info.compressionType = 'raw ';  // unsigned (in AIFF-C files, 'NONE' means signed!)
		info.isCompressed = false;
		info.bigEndian = false;
		info.codecBitDepth = 8;
		info.nChannels = 1;
		info.nPackets = header.stdSH_nBytes;
		info.dataStart = sndhdr + f.Tell();
		info.compressedLength = header.stdSH_nBytes;
		info.decompressedLength = info.compressedLength;
		break;

	case nativeSH_mono16: // pomme extension for little-endian PCM data
	case nativeSH_stereo16:
		info.compressionType = 'sowt';
		info.isCompressed = false;
		info.bigEndian = false;
		info.codecBitDepth = 16;
		info.nChannels = header.encoding == nativeSH_mono16 ? 1 : 2;
		info.nPackets = header.nativeSH_nBytes / (2 * info.nChannels);
		info.dataStart = sndhdr + f.Tell();
		info.compressedLength = header.nativeSH_nBytes;
		info.decompressedLength = info.compressedLength;
		break;

	case 0xFE: // cmpSH - compressed sound header
	{
		info.nPackets = f.Read<int32_t>();
		f.Skip(14);
		info.compressionType = f.Read<uint32_t>();
		f.Skip(20);

		if (info.compressionType == 0)  // Assume MACE-3
		{
			// Assume MACE-3. It should've been set in the init options in the snd pre-header,
			// but Nanosaur doesn't actually init the sound channels for MACE-3. So I guess the Mac
			// assumes by default that any unspecified compression is MACE-3.
			// If it wasn't MACE-3, it would've been caught by GetSoundHeaderOffset.
			info.compressionType = 'MAC3';
		}

		std::unique_ptr<Pomme::Sound::Codec> codec = Pomme::Sound::GetCodec(info.compressionType);

		info.isCompressed = true;
		info.bigEndian = false;
		info.nChannels = header.cmpSH_nChannels;
		info.dataStart = sndhdr + f.Tell();
		info.codecBitDepth = codec->AIFFBitDepth();
		info.compressedLength   = info.nChannels * info.nPackets * codec->BytesPerPacket();
		info.decompressedLength = info.nChannels * info.nPackets * codec->SamplesPerPacket() * 2;
		break;
	}

	case 0xFF: // extSH - extended sound header (noncompressed 8/16-bit mono or stereo)
	{
		info.nPackets = f.Read<int32_t>();
		f.Skip(22);
		info.codecBitDepth = f.Read<int16_t>();
		f.Skip(14);

		info.isCompressed = false;
		info.bigEndian = true;
		info.compressionType = 'twos';  // TODO: if 16-bit, should we use 'raw ' or 'NONE'/'twos'?
		info.nChannels = header.extSH_nChannels;
		info.dataStart = sndhdr + f.Tell();
		info.compressedLength = header.extSH_nChannels * info.nPackets * info.codecBitDepth / 8;
		info.decompressedLength = info.compressedLength;

		if (info.codecBitDepth == 8)
			TODO2("should an 8-bit extSH be 'twos' or 'raw '?");
		break;
	}

	default:
		TODOFATAL2("unsupported snd header encoding " << (int)header.encoding);
	}
}

static void GetSoundInfoFromSndResource(Handle sndHandle, SampledSoundInfo& info)
{
	long offsetToHeader;

	GetSoundHeaderOffset((SndListHandle) sndHandle, &offsetToHeader);

	Ptr sndhdr = (Ptr) (*sndHandle) + offsetToHeader;

	GetSoundInfo(sndhdr, info);
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

	SampledSoundInfo info;
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

		double pan = (double)rvol / (rvol + lvol);
		pan = (pan - 0.5) * 2.0;  // Transpose pan from [0...1] to [-1...+1]

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

template<typename T>
static void Expect(const T a, const T b, const char* msg)
{
	if (a != b)
		throw std::runtime_error(msg);
}

// IM:S:2-58 "MyGetSoundHeaderOffset"
OSErr GetSoundHeaderOffset(SndListHandle sndHandle, long* offset)
{
	memstream sndStream((Ptr) *sndHandle, GetHandleSize((Handle) sndHandle));
	Pomme::BigEndianIStream f(sndStream);

	// Read header
	SInt16 format = f.Read<SInt16>();
	switch (format)
	{
		case 1:  // Standard 'snd ' resource
		{
			Expect<SInt16>(1, f.Read<SInt16>(), "'snd ' modifier count");
			Expect<SInt16>(5, f.Read<SInt16>(), "'snd ' sampledSynth");
			UInt32 initBits = f.Read<UInt32>();
			if (initBits & initMACE6)
				TODOFATAL2("MACE-6 not supported yet");
			break;
		}

		case 2:  // HyperCard sampled-sound format
			f.Skip(2);  // Skip reference count (for application use)
			break;

		default:
			return badFormat;
	}

	// Now read sound commands
	SInt16 nCmds = f.Read<SInt16>();
	//LOG << nCmds << " commands\n";
	for (; nCmds >= 1; nCmds--)
	{
		UInt16 cmd = f.Read<UInt16>();
		f.Skip(2); // SInt16 param1
		SInt32 param2 = f.Read<SInt32>();
		cmd &= 0x7FFF; // See IM:S:2-75
		// When a sound command contained in an 'snd ' resource has associated sound data,
		// the high bit of the command is set. This changes the meaning of the param2 field of the
		// command from a pointer to a location in RAM to an offset value that specifies the offset
		// in bytes from the resource's beginning to the location of the associated sound data (such
		// as a sampled sound header). 
		if (cmd == bufferCmd || cmd == soundCmd)
		{
			*offset = param2;
			return noErr;
		}
	}

	LOG << "didn't find offset in snd resource\n";
	return badFormat;
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
// Extension: decompress

Boolean Pomme_DecompressSoundResource(SndListHandle* sndHandlePtr, long* offsetToHeader)
{
	SampledSoundInfo info;
	GetSoundInfoFromSndResource((Handle) *sndHandlePtr, info);

	// We only handle cmpSH (compressed) 'snd ' resources.
	if (!info.isCompressed)
	{
		return false;
	}

	int outInitialSize = kSampledSoundCommandListLength + kSampledSoundHeaderLength;

	std::unique_ptr<Pomme::Sound::Codec> codec = Pomme::Sound::GetCodec(info.compressionType);

	// Decompress
	SndListHandle outHandle = (SndListHandle) NewHandle(outInitialSize + info.decompressedLength);
	auto spanIn = std::span(info.dataStart, info.compressedLength);
	auto spanOut = std::span((char*) *outHandle + outInitialSize, info.decompressedLength);
	codec->Decode(info.nChannels, spanIn, spanOut);

	// ------------------------------------------------------
	// Now we have the PCM data.
	// Put the output 'snd ' resource together.

	SampledSoundHeader shOut = {};
	shOut.zero = 0;
	shOut.nativeSH_nBytes = info.decompressedLength;
	shOut.fixedSampleRate = static_cast<UnsignedFixed>(info.sampleRate * 65536.0);
	shOut.loopStart = info.loopStart;
	shOut.loopEnd = info.loopEnd;
	shOut.encoding = info.nChannels == 2 ? nativeSH_stereo16 : nativeSH_mono16;
	shOut.baseFrequency = info.baseNote;

	ByteswapStructs(kSampledSoundHeaderPackFormat, kSampledSoundHeaderLength, 1, reinterpret_cast<char*>(&shOut));

	memcpy(*outHandle, kSampledSoundCommandList, kSampledSoundCommandListLength);
	memcpy((char*) *outHandle + kSampledSoundCommandListLength, &shOut, kSampledSoundHeaderLength);

	// Nuke compressed sound handle, replace it with the decopmressed one we've just created
	DisposeHandle((Handle) *sndHandlePtr);
	*sndHandlePtr = outHandle;
	*offsetToHeader = kSampledSoundCommandListLength;

	long offsetCheck = 0;
	OSErr err = GetSoundHeaderOffset(outHandle, &offsetCheck);
	if (err != noErr || offsetCheck != kSampledSoundCommandListLength)
	{
		throw std::runtime_error("Incorrect decompressed sound header offset");
	}

	return true;
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


std::unique_ptr<Pomme::Sound::Codec> Pomme::Sound::GetCodec(uint32_t fourCC)
{
	switch (fourCC)
	{
	case 0: // Assume MACE-3 by default.
	case 'MAC3':
		return std::make_unique<Pomme::Sound::MACE>();
	case 'ima4':
		return std::make_unique<Pomme::Sound::IMA4>();
	case 'alaw':
	case 'ulaw':
		return std::make_unique<Pomme::Sound::xlaw>(fourCC);
	default:
		throw std::runtime_error("Unknown audio codec: " + Pomme::FourCCString(fourCC));
	}
}

//-----------------------------------------------------------------------------
// Dump 'snd ' resource to AIFF

void Pomme::Sound::DumpSoundResourceToAIFF(Handle sndHandle, std::ostream& output, const std::string& resourceName)
{
	class AIFFChunkGuard
	{
	public:
		AIFFChunkGuard(Pomme::BigEndianOStream& theOutput, uint32_t chunkID)
				: output(theOutput)
		{
			output.Write<uint32_t>(chunkID);
			lengthFieldPosition = output.Tell();
			output.Write<uint32_t>('#LEN');  // placeholder
		}

		~AIFFChunkGuard()
		{
			std::streampos endOfChunk = output.Tell();
			std::streamoff chunkLength = endOfChunk - lengthFieldPosition - static_cast<std::streamoff>(4);

			// Add zero pad byte if chunk length is odd
			if (0 != (chunkLength & 1))
			{
				output.Write<uint8_t>(0);
				endOfChunk += 1;
			}

			output.Goto(lengthFieldPosition);
			output.Write<int32_t>(chunkLength);
			output.Goto(endOfChunk);
		}

	private:
		Pomme::BigEndianOStream& output;
		std::streampos lengthFieldPosition;
	};


	SampledSoundInfo info;
	GetSoundInfoFromSndResource(sndHandle, info);

	char sampleRate80bit[10];
	ConvertToIeeeExtended(info.sampleRate, sampleRate80bit);

	Pomme::BigEndianOStream of(output);

	bool hasLoop = info.loopEnd - info.loopStart > 1;

	{
		AIFFChunkGuard form(of, 'FORM');
		of.Write<uint32_t>('AIFC');

		{
			AIFFChunkGuard chunk(of, 'FVER');
			of.Write<uint32_t>(0xA2805140u);
		}

		{
			AIFFChunkGuard chunk(of, 'COMM');
			of.Write<int16_t>(info.nChannels);
			of.Write<uint32_t>(info.nPackets);
			of.Write<int16_t>(info.codecBitDepth);
			of.Write(sampleRate80bit, sizeof(sampleRate80bit));
			of.Write<uint32_t>(info.compressionType);

			std::string compressionName;
			switch (info.compressionType)
			{
				case 'MAC3': compressionName = "MACE 3-to-1"; break;
				case 'ima4': compressionName = "IMA 16 bit 4-to-1"; break;
				case 'NONE': compressionName = "Signed PCM"; break;
				case 'twos': compressionName = "Signed big-endian PCM"; break;
				case 'sowt': compressionName = "Signed little-endian PCM"; break;
				case 'raw ': compressionName = "Unsigned PCM"; break;
				case 'ulaw': compressionName = "mu-law"; break;
				case 'alaw': compressionName = "A-law"; break;
				default: compressionName = "";
			}
			of.WritePascalString(compressionName, 2); // human-readable compression type pascal string
		}

		if (hasLoop)
		{
			AIFFChunkGuard chunk(of, 'MARK');
			of.Write<int16_t>(2);  // 2 markers
			of.Write<int16_t>(101);  // marker ID
			of.Write<uint32_t>(info.loopStart);
			of.WritePascalString("beg loop", 2);
			of.Write<int16_t>(102);  // marker ID
			of.Write<uint32_t>(info.loopEnd);
			of.WritePascalString("end loop", 2);
		}

		if (info.baseNote != kMiddleC || hasLoop)
		{
			AIFFChunkGuard chunk(of, 'INST');
			of.Write<int8_t>(info.baseNote);
			of.Write<int8_t>(0); // detune
			of.Write<int8_t>(0x00); // lowNote
			of.Write<int8_t>(0x7F); // highNote
			of.Write<int8_t>(0x00); // lowVelocity
			of.Write<int8_t>(0x7F); // highVelocity
			of.Write<int16_t>(0); // gain
			of.Write<int16_t>(hasLoop? 1: 0);  // sustainLoop.playMode
			of.Write<int16_t>(hasLoop? 101: 0);  // sustainLoop.beginLoop
			of.Write<int16_t>(hasLoop? 102: 0);  // sustainLoop.endLoop
			of.Write<int16_t>(0);
			of.Write<int16_t>(0);
			of.Write<int16_t>(0);
		}

		if (!resourceName.empty())
		{
			AIFFChunkGuard chunk(of, 'NAME');
			of.WriteRawString(resourceName);
		}

		{
			AIFFChunkGuard chunk(of, 'ANNO');
			std::stringstream ss;
			ss << "Verbatim copy of data stream from 'snd ' resource.\n"
			   << "MIDI base note: " << int(info.baseNote)
			   << ", sustain loop: " << info.loopStart << "-" << info.loopEnd;
			of.WriteRawString(ss.str());
		}

		{
			AIFFChunkGuard chunk(of, 'SSND');
			of.Write<int32_t>(0); // offset; don't care
			of.Write<int32_t>(0); // blockSize; don't care
			of.Write(info.dataStart, info.compressedLength);
		}
	}
}

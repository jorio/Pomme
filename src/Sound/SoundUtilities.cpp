#include "Pomme.h"
#include "PommeFiles.h"
#include "PommeSound.h"
#include "Utilities/memstream.h"
#include "Utilities/bigendianstreams.h"
#include <cstring>

using namespace Pomme::Sound;

//-----------------------------------------------------------------------------
// Cookie-cutter sound command list.
// Used to generate 'snd ' resources.

static const uint8_t kSampledSoundCommandList[20] =
{
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
			SInt16 modifierCount = f.Read<SInt16>();
			SInt16 synthType = f.Read<SInt16>();
			UInt32 initBits = f.Read<UInt32>();

			if (1 != modifierCount)
				TODOFATAL2("only 1 modifier per 'snd ' is supported");

			if (5 != synthType)
				TODOFATAL2("only sampledSynth 'snd ' is supported");

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

	TODOMINOR2("didn't find offset in snd resource");
	return badFormat;
}

void Pomme::Sound::GetSoundInfo(const Ptr sndhdr, SampledSoundInfo& info)
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

void Pomme::Sound::GetSoundInfoFromSndResource(Handle sndHandle, SampledSoundInfo& info)
{
	long offsetToHeader;

	GetSoundHeaderOffset((SndListHandle) sndHandle, &offsetToHeader);

	Ptr sndhdr = (Ptr) (*sndHandle) + offsetToHeader;

	GetSoundInfo(sndhdr, info);
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

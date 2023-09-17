#include "Pomme.h"
#include "PommeFiles.h"
#include "PommeSound.h"
#include "Utilities/memstream.h"
#include "Utilities/bigendianstreams.h"
#include <Utilities/StringUtils.h>
#include <cstring>

using namespace Pomme::Sound;

enum SoundResourceType
{
	kSoundResourceType_Standard		= 0x0001,
	kSoundResourceType_HyperCard	= 0x0002,
	kSoundResourceType_Pomme		= 'po',		// Pomme extension: only sampled data, no command list
};

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
constexpr const char* kSampledSoundHeaderPackFormat = ">IiIIIbb";

enum SampledSoundEncoding
{
	kSampledSoundEncoding_stdSH		= 0x00,		// standard sound header (noncompressed 8-bit mono sample data)
	kSampledSoundEncoding_cmpSH		= 0xFE,		// compressed sound header
	kSampledSoundEncoding_extSH		= 0xFF,		// extended sound header (noncompressed 8/16-bit mono or stereo)
};

//-----------------------------------------------------------------------------
// IM:S:2-58 "MyGetSoundHeaderOffset"

OSErr GetSoundHeaderOffset(SndListHandle sndHandle, long* offset)
{
	memstream sndStream((Ptr) *sndHandle, GetHandleSize((Handle) sndHandle));
	Pomme::BigEndianIStream f(sndStream);

	// Read header
	SInt16 format = f.Read<SInt16>();
	switch (format)
	{
		case kSoundResourceType_Standard:
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

		case kSoundResourceType_HyperCard:
			f.Skip(2);  // Skip reference count (for application use)
			break;

		case kSoundResourceType_Pomme:
			*offset = 2;
			// return now - our own sound resources just have sampled data, no commands
			return noErr;

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

//-----------------------------------------------------------------------------

void Pomme::Sound::GetSoundInfo(const Ptr sndhdr, SampledSoundInfo& info)
{
	// Check if this snd resource is in Pomme's internal format.
	// If so, the resource's header is a raw SampledSoundInfo record,
	// which lets us bypass the parsing of a real Mac snd resource.
	if (0 == memcmp("POMM", sndhdr, 4))
	{
		memcpy(&info, sndhdr+4, sizeof(SampledSoundInfo));
		info.dataStart = sndhdr+4+sizeof(SampledSoundInfo);
		return;
	}

	// It's a real Mac snd resource. Parse it.

	// Prep the BE reader on the header.
	memstream headerInput(sndhdr, kSampledSoundHeaderLength + 42);
	Pomme::BigEndianIStream f(headerInput);

	// Read in SampledSoundHeader and unpack it.
	SampledSoundHeader header;
	f.Read(reinterpret_cast<char*>(&header), kSampledSoundHeaderLength);
	UnpackStructs(kSampledSoundHeaderPackFormat, kSampledSoundHeaderLength, 1, reinterpret_cast<char*>(&header));

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
		case kSampledSoundEncoding_stdSH:
			info.compressionType = 'raw ';  // unsigned (in AIFF-C files, 'NONE' means signed!)
			info.isCompressed = false;
			info.bigEndian = kIsBigEndianNative;  // just use the native endianness for this
			info.codecBitDepth = 8;
			info.nChannels = 1;
			info.nPackets = header.stdSH_nBytes;
			info.dataStart = sndhdr + f.Tell();
			info.compressedLength = header.stdSH_nBytes;
			info.decompressedLength = info.compressedLength;
			break;

		case kSampledSoundEncoding_cmpSH:
		{
			info.nPackets = f.Read<int32_t>();
			f.Skip(14);		// skip AIFFSampleRate(10), markerChunk(4)
			info.compressionType = f.Read<uint32_t>();
			f.Skip(20);		// skip futureUse2(4), stateVars(4), leftOverSamples(4), compressionID(2), packetSize(2), snthID(2)

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
			info.bigEndian = kIsBigEndianNative;  // just use the native endianness for this
			info.nChannels = header.cmpSH_nChannels;
			info.dataStart = sndhdr + f.Tell();
			info.codecBitDepth = codec->AIFFBitDepth();
			info.compressedLength   = info.nChannels * info.nPackets * codec->BytesPerPacket();
			info.decompressedLength = info.nChannels * info.nPackets * codec->SamplesPerPacket() * 2;
			break;
		}

		case kSampledSoundEncoding_extSH:
		{
			info.nPackets = f.Read<int32_t>();
			f.Skip(22);		// skip AIFFSampleRate(10), markerChunk(4), instrumentChunks(10), AESRecording(4)
			info.codecBitDepth = f.Read<int16_t>();
			f.Skip(14);		// skip futureUse1(2), futureUse2(4), futureUse3(4), futureUse4(4)

			info.isCompressed = false;
			info.bigEndian = true;
			info.compressionType = info.codecBitDepth == 8 ? 'raw ' : 'twos';  // unsigned if 8-bit, signed if 16-bit!
			info.nChannels = header.extSH_nChannels;
			info.dataStart = sndhdr + f.Tell();
			info.compressedLength = header.extSH_nChannels * info.nPackets * info.codecBitDepth / 8;
			info.decompressedLength = info.compressedLength;
			break;
		}

		default:
			TODOFATAL2("unsupported snd header encoding " << (int)header.encoding);
	}
}

//-----------------------------------------------------------------------------

void Pomme::Sound::GetSoundInfoFromSndResource(Handle sndHandle, SampledSoundInfo& info)
{
	long offsetToHeader;

	GetSoundHeaderOffset((SndListHandle) sndHandle, &offsetToHeader);

	Ptr sndhdr = (Ptr) (*sndHandle) + offsetToHeader;

	GetSoundInfo(sndhdr, info);
}

SndListHandle Pomme::Sound::SampledSoundInfo::MakeStandaloneResource(char** dataOffsetOut) const
{
	const char* data = dataStart;

	SampledSoundInfo info = *this;

	Handle h = NewHandleClear(2 + 4 + sizeof(info) + compressedLength);
	Ptr p = *h;

	memcpy(p, "poPOMM", 6);		// "po": see kSoundResourceType_Pomme; "POMM": see GetSoundInfo
	p += 6;

	info.dataStart = p + sizeof(info);
	memcpy(p, &info, sizeof(info));
	p += sizeof(info);

	if (data != nullptr)
	{
		memcpy(p, data, info.compressedLength);
	}

	if (dataOffsetOut != nullptr)
	{
		*dataOffsetOut = p;
	}

	return (SndListHandle) h;
}

//-----------------------------------------------------------------------------
// Extension: load AIFF file as resource

SndListHandle Pomme_SndLoadFileAsResource(short fRefNum)
{
	auto& spec = Pomme::Files::GetSpec(fRefNum);
	auto& stream = Pomme::Files::GetStream(fRefNum);

	u8string fileName((const char8_t*) spec.cName);
	fileName = UppercaseCopy(fileName);
	fs::path extension = fs::path(fileName).extension();

	// Guess media container from extension
	if (extension == ".AIFF"
		|| extension == ".AIFC"
		|| extension == ".AIF")
	{
		return LoadAIFFAsResource(stream);
	}
	else if (extension == ".MP3")
	{
#ifndef POMME_NO_MP3
		return LoadMP3AsResource(stream);
#endif
	}

	return nullptr;
}

//-----------------------------------------------------------------------------
// Extension: decompress

Boolean Pomme_DecompressSoundResource(SndListHandle* sndHandlePtr, long* offsetToHeader)
{
	SampledSoundInfo inInfo;
	GetSoundInfoFromSndResource((Handle) *sndHandlePtr, inInfo);

	if (!inInfo.dataStart)
	{
		throw std::runtime_error("cannot decompress snd resource without dataStart");
	}

	Handle h = NewHandleClear(2 + 4 + sizeof(SampledSoundInfo) + inInfo.decompressedLength);
	const char*		inDataStart		= inInfo.dataStart;
	char*			outDataStart	= *h+6+sizeof(SampledSoundInfo);

	SampledSoundInfo outInfo = inInfo;
	outInfo.dataStart			= nullptr;
	outInfo.isCompressed		= false;
	outInfo.compressedLength	= outInfo.decompressedLength;

	if (!inInfo.isCompressed)
	{
		// Raw PCM
		if (inInfo.decompressedLength != inInfo.compressedLength)
			throw std::runtime_error("decompressedLength != compressedLength???");

		memcpy(outDataStart, inDataStart, inInfo.decompressedLength);

		// If the PCM data's endianness doesn't match our native endianness, swap the bytes
		int bytesPerSample = inInfo.codecBitDepth / 8;
		if (inInfo.bigEndian != kIsBigEndianNative && bytesPerSample > 1)
		{
			int nIntegers = inInfo.decompressedLength / bytesPerSample;
			if (inInfo.decompressedLength != nIntegers * bytesPerSample)
				throw std::runtime_error("unexpected big-endian raw PCM decompressed length");

			ByteswapInts(bytesPerSample, nIntegers, outDataStart);
			outInfo.bigEndian = kIsBigEndianNative;
		}
	}
	else
	{
		auto codec   = Pomme::Sound::GetCodec(inInfo.compressionType);
		auto spanIn  = std::span(inDataStart, inInfo.compressedLength);
		auto spanOut = std::span(outDataStart, inInfo.decompressedLength);
		codec->Decode(inInfo.nChannels, spanIn, spanOut);

#if __BIG_ENDIAN__
		outInfo.compressionType		= 'twos';
		outInfo.bigEndian			= true;			// convert to native endianness
#else
		outInfo.compressionType		= 'swot';
		outInfo.bigEndian			= false;		// convert to native endianness
#endif
		outInfo.codecBitDepth		= 16;
		outInfo.nPackets			= codec->SamplesPerPacket() * inInfo.nPackets;
	}

	// Write header
	memcpy(*h, "poPOMM", 6);
	memcpy(*h+6, &outInfo, sizeof(SampledSoundInfo));

	// Nuke compressed sound handle, replace it with the decopmressed one we've just created
	DisposeHandle((Handle) *sndHandlePtr);
	*sndHandlePtr = (SndListHandle) h;
	*offsetToHeader = 2;

	// Check offset
	long offsetCheck = 0;
	OSErr err = GetSoundHeaderOffset((SndListHandle) h, &offsetCheck);
	if (err != noErr || offsetCheck != 2)
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

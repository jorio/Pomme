#include "PommeSound.h"
#include "Utilities/bigendianstreams.h"
#include <cstdint>
#include <map>

static void AIFFAssert(bool condition, const char* message)
{
	if (!condition)
	{
		throw std::runtime_error(message);
	}
}

static void ParseCOMM(Pomme::BigEndianIStream& f, Pomme::Sound::SampledSoundInfo& info, bool isAIFC)
{
	info.nChannels       = f.Read<uint16_t>();
	info.nPackets        = f.Read<uint32_t>();
	info.codecBitDepth   = f.Read<uint16_t>();
	info.sampleRate      = f.Read80BitFloat();

	if (isAIFC)
	{
		info.compressionType = f.Read<uint32_t>();
		f.ReadPascalString(2); // This is a human-friendly compression name. Skip it.
	}
	else
	{
		info.compressionType = 'NONE';
	}

	switch (info.compressionType)
	{
		case 'NONE': info.bigEndian = true;		info.isCompressed = false;	break;
		case 'twos': info.bigEndian = true;		info.isCompressed = false;	break;
		case 'sowt': info.bigEndian = false;	info.isCompressed = false;	break;
		case 'raw ': info.bigEndian = true;		info.isCompressed = false;	break;
		case 'MAC3': info.bigEndian = true;		info.isCompressed = true;	break;
		case 'ima4': info.bigEndian = true;		info.isCompressed = true;	break;
		case 'ulaw': info.bigEndian = true;		info.isCompressed = true;	break;
		case 'alaw': info.bigEndian = true;		info.isCompressed = true;	break;
		default:
			throw std::runtime_error("unknown AIFF-C compression type");
	}
}

static void ParseMARK(Pomme::BigEndianIStream& f, std::map<uint16_t, uint32_t>& markers)
{
	int16_t nMarkers = f.Read<int16_t>();

	for (int16_t i = 0; i < nMarkers; i++)
	{
		uint16_t markerID = f.Read<uint16_t>();
		uint32_t markerPosition = f.Read<uint32_t>();
		f.ReadPascalString(2);		// skip name

		markers[markerID] = markerPosition;
	}
}

static void ParseINST(Pomme::BigEndianIStream& f, Pomme::Sound::SampledSoundInfo& info, std::map<uint16_t, uint32_t>& markers)
{
	info.baseNote = f.Read<int8_t>();
	f.Skip(1);		// detune
	f.Skip(1);		// lowNote
	f.Skip(1);		// highNote
	f.Skip(1);		// lowVelocity
	f.Skip(1);		// highVelocity
	f.Skip(2);		// gain
	uint16_t playMode = f.Read<uint16_t>();
	uint16_t beginLoopMarkerID = f.Read<uint16_t>();
	uint16_t endLoopMarkerID = f.Read<uint16_t>();
	f.Skip(2);
	f.Skip(2);
	f.Skip(2);

	switch (playMode)
	{
		case 0:
			break;

		case 1:
			info.loopStart = markers.at(beginLoopMarkerID);
			info.loopEnd = markers.at(endLoopMarkerID);
			break;

		default:
			throw std::runtime_error("unsupported AIFF INST playMode");
	}
}

static std::streampos GetSoundInfoFromAIFF(std::istream& input, Pomme::Sound::SampledSoundInfo& info)
{
	Pomme::BigEndianIStream f(input);

	AIFFAssert('FORM' == f.Read<uint32_t>(), "AIFF: invalid FORM");
	auto formSize = f.Read<uint32_t>();
	auto endOfForm = f.Tell() + std::streampos(formSize);
	auto formType = f.Read<uint32_t>();
	AIFFAssert(formType == 'AIFF' || formType == 'AIFC', "AIFF: not an AIFF or AIFC file");

	bool gotCOMM = false;
	std::streampos sampledSoundDataOffset = 0;

	info = {};
	info.compressionType	= 'NONE';
	info.isCompressed		= false;
	info.baseNote			= 60;		// Middle C

	std::map<uint16_t, uint32_t> markers;

	while (f.Tell() != endOfForm)
	{
		auto ckID = f.Read<uint32_t>();
		auto ckSize = f.Read<uint32_t>();
		std::streampos endOfChunk = f.Tell() + std::streampos(ckSize);

		switch (ckID)
		{
		case 'FVER':
		{
			uint32_t timestamp = f.Read<uint32_t>();
			AIFFAssert(timestamp == 0xA2805140u, "AIFF: unrecognized FVER");
			break;
		}

		case 'COMM': // common chunk, 2-85
			ParseCOMM(f, info, formType=='AIFC');
			gotCOMM = true;
			break;

		case 'MARK':
			ParseMARK(f, markers);
			break;

		case 'INST':
			ParseINST(f, info, markers);
			break;

		case 'SSND':
		{
			AIFFAssert(gotCOMM, "AIFF: reached SSND before COMM");
			AIFFAssert(0 == f.Read<uint64_t>(), "AIFF: unexpected offset/blockSize in SSND");

			// sampled sound data is here
			sampledSoundDataOffset = f.Tell();

			const int ssndSize = ckSize - 8;

			info.compressedLength = ssndSize;

			if (!info.isCompressed)
			{
				info.decompressedLength = info.compressedLength;
			}
			else
			{
				auto codec = Pomme::Sound::GetCodec(info.compressionType);
				info.decompressedLength = info.nChannels * info.nPackets * codec->SamplesPerPacket() * 2;
			}

			f.Skip(ssndSize);

			break;
		}

		default:
			f.Goto(int(endOfChunk));
			break;
		}

		AIFFAssert(f.Tell() == endOfChunk, "AIFF: incorrect end-of-chunk position");

		// skip zero pad byte if odd position
		if ((f.Tell() & 1) == 1)
		{
			f.Skip(1);
		}
	}

	f.Goto(sampledSoundDataOffset);
	return sampledSoundDataOffset;
}

SndListHandle Pomme::Sound::LoadAIFFAsResource(std::istream& stream)
{
	Pomme::Sound::SampledSoundInfo info = {};
	std::streampos ssndStart = GetSoundInfoFromAIFF(stream, info);

	char* dataOffset = nullptr;
	SndListHandle h = info.MakeStandaloneResource(&dataOffset);

	stream.seekg(ssndStart, std::ios::beg);
	stream.read(dataOffset, info.compressedLength);

	return h;
}

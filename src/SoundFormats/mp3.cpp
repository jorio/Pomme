#ifndef POMME_NO_MP3

#include "PommeSound.h"

#include <vector>
#include <algorithm>

#define MINIMP3_IMPLEMENTATION
#include "SoundFormats/minimp3.h"

#define MINIMP3_IO_SIZE (128*1024) // io buffer size for streaming functions, must be greater than MINIMP3_BUF_SIZE
#define MINIMP3_BUF_SIZE (16*1024) // buffer which can hold minimum 10 consecutive mp3 frames (~16KB) worst case

SndListHandle Pomme::Sound::LoadMP3AsResource(std::istream& stream)
{
	mp3dec_t context = {};
	mp3dec_init(&context);

	mp3dec_frame_info_t frameInfo = {};

	std::vector<uint8_t> fileBuf;
	std::vector<mp3d_sample_t> songPCM;
	std::vector<mp3d_sample_t> tempPCM(MINIMP3_MAX_SAMPLES_PER_FRAME);

	int totalSamples = 0;

	while (!stream.eof() || !fileBuf.empty())
	{
		// Refill the buffer as long as data is available in the input stream.
		// Once the stream is depleted, keep feeding the buffer to mp3dec until the buffer is empty.
		if (fileBuf.size() < MINIMP3_BUF_SIZE
			&& !stream.eof())
		{
			auto oldSize = fileBuf.size();
			auto toRead = MINIMP3_BUF_SIZE - oldSize;

			fileBuf.resize(MINIMP3_BUF_SIZE);
			stream.read((char*) (fileBuf.data() + oldSize), (int) toRead);

			auto didRead = stream.gcount();
			fileBuf.resize(oldSize + didRead);
		}

		if (fileBuf.empty())
		{
			break;
		}

		int numDecodedSamples = mp3dec_decode_frame(&context, fileBuf.data(), (int) fileBuf.size(), tempPCM.data(), &frameInfo);

		if (numDecodedSamples > 0)
		{
			size_t minCapacity = songPCM.size() + (numDecodedSamples * frameInfo.channels);
			if (songPCM.capacity() < minCapacity)
			{
				songPCM.reserve(2 * minCapacity);
			}

			songPCM.insert(songPCM.end(), tempPCM.begin(), tempPCM.begin() + (numDecodedSamples * frameInfo.channels));
			totalSamples += numDecodedSamples;
		}

		fileBuf.erase(fileBuf.begin(), fileBuf.begin() + frameInfo.frame_bytes);
	}

	Pomme::Sound::SampledSoundInfo info = {};
#if __BIG_ENDIAN__
	info.compressionType	= 'twos';
	info.bigEndian			= true;
#else
	info.compressionType	= 'sowt';
	info.bigEndian			= false;
#endif
	info.isCompressed		= false;
	info.baseNote			= 60;		// Middle C
	info.codecBitDepth		= 8 * sizeof(mp3d_sample_t);
	info.sampleRate			= frameInfo.hz;
	info.nChannels			= frameInfo.channels;
	info.nPackets			= totalSamples;
	info.decompressedLength	= totalSamples * info.nChannels * sizeof(mp3d_sample_t);
	info.compressedLength	= info.decompressedLength;
	info.dataStart			= (char*) songPCM.data();
	return info.MakeStandaloneResource();
}

#endif // POMME_NO_MP3

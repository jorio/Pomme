#pragma once

#include "CompilerSupport/span.h"
#include "PommeTypes.h"
#include <vector>
#include <istream>
#include <ostream>
#include <memory>

namespace Pomme::Sound
{
	void InitMidiFrequencyTable();

	void InitMixer();
	void ShutdownMixer();

	double GetMidiNoteFrequency(int note);
	std::string GetMidiNoteName(int note);

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

		SndListHandle MakeStandaloneResource(char** dataOffsetOut = nullptr) const;
	};

	class Codec
	{
	public:
		virtual ~Codec()
		{}

		virtual int SamplesPerPacket() = 0;

		virtual int BytesPerPacket() = 0;

		virtual int AIFFBitDepth() = 0;

		virtual void Decode(const int nChannels, const std::span<const char> input, const std::span<char> output) = 0;
	};

	class MACE : public Codec
	{
	public:
		int SamplesPerPacket() override
		{ return 6; }

		int BytesPerPacket() override
		{ return 2; }

		int AIFFBitDepth() override
		{ return 8; }

		void Decode(const int nChannels, const std::span<const char> input, const std::span<char> output) override;
	};

	class IMA4 : public Codec
	{
	public:
		int SamplesPerPacket() override
		{ return 64; }

		int BytesPerPacket() override
		{ return 34; }

		int AIFFBitDepth() override
		{ return 16; }

		void Decode(const int nChannels, const std::span<const char> input, const std::span<char> output) override;
	};

	class xlaw : public Codec
	{
		const int16_t* xlawToPCM;
	public:
		xlaw(uint32_t codecFourCC);

		int SamplesPerPacket() override
		{ return 1; }

		int BytesPerPacket() override
		{ return 1; }

		int AIFFBitDepth() override
		{ return 8; }

		void Decode(const int nChannels, const std::span<const char> input, const std::span<char> output) override;
	};

	void GetSoundInfo(const Ptr sndhdr, SampledSoundInfo& info);

	void GetSoundInfoFromSndResource(Handle sndHandle, SampledSoundInfo& info);

	SndListHandle LoadAIFFAsResource(std::istream& input);
	SndListHandle LoadMP3AsResource(std::istream& input);

	std::unique_ptr<Pomme::Sound::Codec> GetCodec(uint32_t fourCC);
}

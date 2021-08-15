#include "PommeSound.h"
#include <sstream>

constexpr int kNumMidiNotes = 128;

static double gMidiNoteFrequencies[kNumMidiNotes];

void Pomme::Sound::InitMidiFrequencyTable()
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

		float freq;
		if (octave < 7)
			freq = gamme[semitone] * 440.0 / (1 << (7 - octave)); // 440/(2**octaveDiff)
		else
			freq = gamme[semitone] * 440.0 * (1 << (octave - 7)); // 440*(2**octaveDiff)

		gMidiNoteFrequencies[i] = freq;
	}
}

double Pomme::Sound::GetMidiNoteFrequency(int note)
{
	if (note < 0 || note >= kNumMidiNotes)
		return 440.0;
	else
		return gMidiNoteFrequencies[note];
}

// Note: these names are according to IM:S:2-43.
// These names won't match real-world names.
// E.g. for note 67 (A 440Hz), this will return "A6", whereas the real-world
// convention for that note is "A4".
std::string Pomme::Sound::GetMidiNoteName(int note)
{
	static const char* gamme[12] = {"A", "A#", "B", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#"};

	int octave = 1 + (note + 3) / 12;
	int semitonesFromA = (note + 3) % 12;

	std::stringstream ss;
	ss << gamme[semitonesFromA] << octave;
	return ss.str();
}

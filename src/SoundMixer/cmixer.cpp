// Adapted from cmixer by rxi (https://github.com/rxi/cmixer)

/*
** Copyright (c) 2017 rxi
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to
** deal in the Software without restriction, including without limitation the
** rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
** sell copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
** FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
** IN THE SOFTWARE.
**/

#include "cmixer.h"
#include "Utilities/structpack.h"
#include <SDL3/SDL.h>

#include <cstring>
#include <vector>
#include <fstream>
#include <list>

using namespace cmixer;

#define CLAMP(x, a, b)    ((x) < (a) ? (a) : (x) > (b) ? (b) : (x))
#define MIN(a, b)         ((a) < (b) ? (a) : (b))
#define MAX(a, b)         ((a) > (b) ? (a) : (b))

#define FX_BITS (12)
#define FX_UNIT (1 << FX_BITS)
#define FX_MASK (FX_UNIT - 1)
#define FX_FROM_FLOAT(f)  ((long)((f) * FX_UNIT))
#define DOUBLE_FROM_FX(f)  ((double)f / FX_UNIT)
#define FX_LERP(a, b, p)  ((a) + ((((b) - (a)) * (p)) >> FX_BITS))

#define BUFFER_MASK (BUFFER_SIZE - 1)

//-----------------------------------------------------------------------------
// Global mixer

static struct Mixer
{
	SDL_Mutex* sdlAudioMutex;

	std::list<Source*> sources;   // Linked list of active (playing) sources
	int32_t pcmmixbuf[BUFFER_SIZE]; // Internal master buffer
	int16_t pcmclipbuf[BUFFER_SIZE]; // Internal clip buffer
	int samplerate;               // Master samplerate
	int gain;                     // Master gain (fixed point)

	void Init(int samplerate);

	void Process(SDL_AudioStream* stream, int len);

	void Lock();

	void Unlock();

	void SetMasterGain(double newGain);
} gMixer = {};

//-----------------------------------------------------------------------------
// Global init/shutdown

static bool sdlAudioSubSystemInited = false;
static SDL_AudioDeviceID sdlDeviceID = 0;

static void SDLCALL AudioCallback(void *userData, SDL_AudioStream *stream, int additionalAmount, int totalAmount)
{
	(void) totalAmount;
	(void) userData;

	// Calculate a little more audio here, write it to `stream`
	if (additionalAmount > 0)
	{
		gMixer.Process(stream, additionalAmount / 2);
	}
}

// SDL2 used to offer SDL_AUDIO_ALLOW_FREQUENCY_CHANGE to avoid crackles and
// pops if the hardware doesn't work at the exact frequency we've asked for
// (typically we'd ask for 44100 and get back 48000).
// I couldn't find an equivalent to SDL_AUDIO_ALLOW_FREQUENCY_CHANGE in SDL3.
// So, use this function before opening the audio device to query the hardware
// for its preferred frequency (sample rate).
static int GetHardwareFrequency(void)
{
	SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL, NULL, NULL);
	if (!stream)
		throw std::runtime_error(SDL_GetError());

	SDL_AudioSpec spec;
	bool success = SDL_GetAudioStreamFormat(stream, NULL, &spec);
	if (!success)
		throw std::runtime_error(SDL_GetError());

	sdlDeviceID = SDL_GetAudioStreamDevice(stream);
	if (!sdlDeviceID)
		throw std::runtime_error("invalid audio stream device");
	SDL_CloseAudioDevice(sdlDeviceID);

	return spec.freq;
}

void cmixer::InitWithSDL()
{
	if (sdlAudioSubSystemInited)
		throw std::runtime_error("SDL audio subsystem already inited");

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
		throw std::runtime_error("couldn't init SDL audio subsystem");

	sdlAudioSubSystemInited = true;

	// Init SDL audio
	SDL_AudioSpec spec = {.format=SDL_AUDIO_S16, .channels=2, .freq=GetHardwareFrequency()};

	SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(
				SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
				&spec,
				AudioCallback,
				nullptr);
	if (!stream)
		throw std::runtime_error(SDL_GetError());
	sdlDeviceID = SDL_GetAudioStreamDevice(stream);

	// Init library
	gMixer.Init(spec.freq);
	gMixer.SetMasterGain(0.5);

	// Start audio
	SDL_ResumeAudioDevice(sdlDeviceID);
}

void cmixer::ShutdownWithSDL()
{
	if (sdlDeviceID)
	{
		SDL_CloseAudioDevice(sdlDeviceID);
		sdlDeviceID = 0;
	}
	if (gMixer.sdlAudioMutex)
	{
		SDL_DestroyMutex(gMixer.sdlAudioMutex);
		gMixer.sdlAudioMutex = nullptr;
	}
	if (sdlAudioSubSystemInited)
	{
		SDL_QuitSubSystem(SDL_INIT_AUDIO);
		sdlAudioSubSystemInited = false;
	}
}

double cmixer::GetMasterGain()
{
	return DOUBLE_FROM_FX(gMixer.gain);
}

void cmixer::SetMasterGain(double newGain)
{
	gMixer.SetMasterGain(newGain);
}

//-----------------------------------------------------------------------------
// Global mixer impl

void Mixer::Lock()
{
	SDL_LockMutex(sdlAudioMutex);
}

void Mixer::Unlock()
{
	SDL_UnlockMutex(sdlAudioMutex);
}

void Mixer::Init(int newSamplerate)
{
	sdlAudioMutex = SDL_CreateMutex();

	samplerate = newSamplerate;
	gain = FX_UNIT;
}

void Mixer::SetMasterGain(double newGain)
{
	if (newGain < 0)
		newGain = 0;
	gain = (int) FX_FROM_FLOAT(newGain);
}

void Mixer::Process(SDL_AudioStream* stream, int len)
{
	// Process in chunks of BUFFER_SIZE if `len` is larger than BUFFER_SIZE
	while (len > BUFFER_SIZE)
	{
		Process(stream, BUFFER_SIZE);
		len -= BUFFER_SIZE;
	}

	// Zeroset internal buffer
	memset(pcmmixbuf, 0, len * sizeof(pcmmixbuf[0]));

	// Process active sources
	Lock();
	for (auto si = sources.begin(); si != sources.end();)
	{
		auto& s = **si;
		s.Process(len);
		// Remove source from list if it is no longer playing
		if (s.state != CM_STATE_PLAYING)
		{
			s.active = false;
			si = sources.erase(si);
		}
		else
		{
			++si;
		}
	}
	Unlock();

	// Copy internal buffer to destination and clip
	for (int i = 0; i < len; i++)
	{
		int x = (pcmmixbuf[i] * gain) >> FX_BITS;
		pcmclipbuf[i] = CLAMP(x, -32768, 32767);
	}

	// Feed SDL audio stream
	SDL_PutAudioStreamData(stream, pcmclipbuf, len * 2);
}

//-----------------------------------------------------------------------------
// Source implementation

Source::Source()
{
	ClearPrivate();
	active = false;
}

void Source::ClearPrivate()
{
	samplerate	= 0;
	length		= 0;
	end			= 0;
	state		= CM_STATE_STOPPED;
	position	= 0;
	lgain		= 0;
	rgain		= 0;
	rate		= 0;
	nextfill	= 0;
	loop		= false;
	rewind		= true;
	interpolate = false;
	// DON'T touch active. The source may still be in gMixer!
	gain		= 0;
	pan			= 0;
	onComplete	= nullptr;
}

void Source::Clear()
{
	gMixer.Lock();
	ClearPrivate();
	ClearImplementation();
	gMixer.Unlock();
}

void Source::Init(int theSampleRate, int theLength)
{
	this->samplerate = theSampleRate;
	this->length = theLength;
	this->sustainOffset = 0;
	SetGain(1);
	SetPan(0);
	SetPitch(1);
	SetLoop(false);
	Stop();
}

void Source::RemoveFromMixer()
{
	gMixer.Lock();
	if (active)
	{
		gMixer.sources.remove(this);
		active = false;
	}
	gMixer.Unlock();
}

Source::~Source()
{
	if (active)
	{
		// You MUST call RemoveFromMixer before destroying a source. If you get here, your program is incorrect.
		fprintf(stderr, "Source wasn't removed from mixer prior to destruction!\n");
#if _DEBUG
		std::terminate();
#endif
	}
}

void Source::Rewind()
{
	RewindImplementation();
	position = 0;
	rewind = false;
	end = length;
	nextfill = 0;
}

void Source::FillBuffer(int offset, int fillLength)
{
	FillBuffer(pcmbuf + offset, fillLength);
}

void Source::Process(int len)
{
	int32_t* dst = gMixer.pcmmixbuf;

	// Do rewind if flag is set
	if (rewind)
	{
		Rewind();
	}

	// Don't process if not playing
	if (state != CM_STATE_PLAYING)
	{
		return;
	}

	// Process audio
	while (len > 0)
	{
		// Get current position frame
		int frame = int(position >> FX_BITS);

		// Fill buffer if required
		if (frame + 3 >= nextfill)
		{
			FillBuffer((nextfill * 2) & BUFFER_MASK, BUFFER_SIZE / 2);
			nextfill += BUFFER_SIZE / 4;
		}

		// Handle reaching the end of the playthrough
		if (frame >= end)
		{
			// As streams continiously fill the raw buffer in a loop we simply
			// increment the end idx by one length and continue reading from it for
			// another play-through
			end = frame + this->length;
			// Set state and stop processing if we're not set to loop
			if (!loop)
			{
				state = CM_STATE_STOPPED;
				if (onComplete != nullptr)
					onComplete();
				break;
			}
		}

		// Work out how many frames we should process in the loop
		int n = MIN(nextfill - 2, end) - frame;
		int count = (n << FX_BITS) / rate;
		count = MAX(count, 1);
		count = MIN(count, len / 2);
		len -= count * 2;

		// Add audio to master buffer
		if (rate == FX_UNIT)
		{
			// Add audio to buffer -- basic
			n = frame * 2;
			for (int i = 0; i < count; i++)
			{
				dst[0] += (pcmbuf[(n    ) & BUFFER_MASK] * lgain) >> FX_BITS;
				dst[1] += (pcmbuf[(n + 1) & BUFFER_MASK] * rgain) >> FX_BITS;
				n += 2;
				dst += 2;
			}
			this->position += count * FX_UNIT;
		}
		else if (interpolate)
		{
			// Resample audio (with linear interpolation) and add to buffer
			for (int i = 0; i < count; i++)
			{
				n = int(position >> FX_BITS) * 2;
				int p = position & FX_MASK;
				int a = pcmbuf[(n    ) & BUFFER_MASK];
				int b = pcmbuf[(n + 2) & BUFFER_MASK];
				dst[0] += (FX_LERP(a, b, p) * lgain) >> FX_BITS;
				n++;
				a = pcmbuf[(n    ) & BUFFER_MASK];
				b = pcmbuf[(n + 2) & BUFFER_MASK];
				dst[1] += (FX_LERP(a, b, p) * rgain) >> FX_BITS;
				position += rate;
				dst += 2;
			}
		}
		else
		{
			// Resample audio (without interpolation) and add to buffer
			for (int i = 0; i < count; i++)
			{
				n = int(position >> FX_BITS) * 2;
				dst[0] += (pcmbuf[(n    ) & BUFFER_MASK] * lgain) >> FX_BITS;
				dst[1] += (pcmbuf[(n + 1) & BUFFER_MASK] * rgain) >> FX_BITS;
				position += rate;
				dst += 2;
			}
		}
	}
}

double Source::GetLength() const
{
	return length / (double) samplerate;
}

double Source::GetPosition() const
{
	return ((position >> FX_BITS) % length) / (double) samplerate;
}

int Source::GetState() const
{
	return state;
}

void Source::RecalcGains()
{
	double l = this->gain * (pan <= 0. ? 1. : 1. - pan);
	double r = this->gain * (pan >= 0. ? 1. : 1. + pan);
	this->lgain = (int) FX_FROM_FLOAT(l);
	this->rgain = (int) FX_FROM_FLOAT(r);
}

void Source::SetGain(double newGain)
{
	gain = newGain;
	RecalcGains();
}

void Source::SetPan(double newPan)
{
	pan = CLAMP(newPan, -1.0, 1.0);
	RecalcGains();
}

void Source::SetPitch(double newPitch)
{
	double newRate;
	if (newPitch > 0.)
	{
		newRate = samplerate / (double) gMixer.samplerate * newPitch;
	}
	else
	{
		newRate = 0.001;
	}
	rate = (int) FX_FROM_FLOAT(newRate);
}

void Source::SetLoop(bool newLoop)
{
	loop = newLoop;
}

void Source::SetInterpolation(bool newInterpolation)
{
	interpolate = newInterpolation;
}

void Source::Play()
{
	if (length == 0)
	{
		// Don't attempt to play an empty source as this would result
		// in instant starvation when filling mixer buffer
		return;
	}

	gMixer.Lock();
	state = CM_STATE_PLAYING;
	if (!active)
	{
		active = true;
		gMixer.sources.push_front(this);
	}
	gMixer.Unlock();
}

void Source::Pause()
{
	state = CM_STATE_PAUSED;
}

void Source::TogglePause()
{
	if (state == CM_STATE_PAUSED)
		Play();
	else if (state == CM_STATE_PLAYING)
		Pause();
}

void Source::Stop()
{
	state = CM_STATE_STOPPED;
	rewind = true;
}

//-----------------------------------------------------------------------------
// WavStream implementation

#define WAV_PROCESS_LOOP(X) \
	while (n--)             \
	{                       \
		X                   \
		dst += 2;           \
		idx++;              \
	}

WavStream::WavStream()
	: Source()
{
	ClearImplementation();
}

void WavStream::ClearImplementation()
{
	bitdepth = 0;
	channels = 0;
	bigEndian = kIsBigEndianNative;
	idx = 0;
	userBuffer.clear();
}

void WavStream::Init(
	int theSampleRate,
	int theBitDepth,
	int theNChannels,
	bool theBigEndian,
	std::span<char> theSpan)
{
	Clear();
	Source::Init(theSampleRate, int((theSpan.size() / (theBitDepth / 8)) / theNChannels));
	this->bitdepth = theBitDepth;
	this->channels = theNChannels;
	this->idx = 0;
	this->span = theSpan;
	this->bigEndian = theBigEndian;
}

std::span<char> WavStream::GetBuffer(int nBytesOut)
{
	userBuffer.clear();
	userBuffer.reserve(nBytesOut);
	return std::span(userBuffer.data(), nBytesOut);
}

std::span<char> WavStream::SetBuffer(std::vector<char>&& data)
{
	userBuffer = std::move(data);
	return std::span(userBuffer.data(), userBuffer.size());
}

void WavStream::RewindImplementation()
{
	idx = 0;
}

void WavStream::FillBuffer(int16_t* dst, int fillLength)
{
	int x, n;

	fillLength /= 2;

	while (fillLength > 0)
	{
		n = MIN(fillLength, length - idx);

		fillLength -= n;

		if (bigEndian && bitdepth == 16 && channels == 1)
		{
			WAV_PROCESS_LOOP({
				dst[0] = dst[1] = UnpackI16BE(&data16()[idx]);
			});
		}
		else if (bigEndian && bitdepth == 16 && channels == 2)
		{
			WAV_PROCESS_LOOP({
				x = idx * 2;
				dst[0] = UnpackI16BE(&data16()[x]);
				dst[1] = UnpackI16BE(&data16()[x + 1]);
			});
		}
		else if (bitdepth == 16 && channels == 1)
		{
			WAV_PROCESS_LOOP({
				dst[0] = dst[1] = UnpackI16LE(&data16()[idx]);
			});
		}
		else if (bitdepth == 16 && channels == 2)
		{
			WAV_PROCESS_LOOP({
				x = idx * 2;
				dst[0] = UnpackI16LE(&data16()[x]);
				dst[1] = UnpackI16LE(&data16()[x + 1]);
			});
		}
		else if (bitdepth == 8 && channels == 1)
		{
			WAV_PROCESS_LOOP({
				dst[0] = dst[1] = (data8()[idx] - 128) << 8;
			});
		}
		else if (bitdepth == 8 && channels == 2)
		{
			WAV_PROCESS_LOOP({
				x = idx * 2;
				dst[0] = (data8()[x] - 128) << 8;
				dst[1] = (data8()[x + 1] - 128) << 8;
			});
		}
		// Loop back and continue filling buffer if we didn't fill the buffer
		if (fillLength > 0)
		{
			idx = sustainOffset;
		}
	}
}

#if 0
//-----------------------------------------------------------------------------
// LoadWAVFromFile for testing

static std::vector<char> LoadFile(char const* filename)
{
	std::ifstream ifs(filename, std::ios::binary | std::ios::ate);
	auto pos = ifs.tellg();
	std::vector<char> bytes(pos);
	ifs.seekg(0, std::ios::beg);
	ifs.read(&bytes[0], pos);
	return bytes;
}

static const char* FindChunk(const char* data, int len, const char* id, int* size)
{
	// TODO : Error handling on malformed wav file
	int idlen = strlen(id);
	const char* p = data + 12;
next:
	*size = *((cm_UInt32*)(p + 4));
	if (memcmp(p, id, idlen)) {
		p += 8 + *size;
		if (p > data + len) return NULL;
		goto next;
	}
	return p + 8;
}

WavStream cmixer::LoadWAVFromFile(const char* path)
{
	int sz;
	auto filebuf = LoadFile(path);
	auto len = filebuf.size();
	const char* data = filebuf.data();
	const char* p = (char*)data;

	// Check header
	if (memcmp(p, "RIFF", 4) || memcmp(p + 8, "WAVE", 4))
		throw std::invalid_argument("bad wav header");

	// Find fmt subchunk
	p = FindChunk(data, len, "fmt ", &sz);
	if (!p)
		throw std::invalid_argument("no fmt subchunk");

	// Load fmt info
	int format = *((cm_UInt16*)(p));
	int channels = *((cm_UInt16*)(p + 2));
	int samplerate = *((cm_UInt32*)(p + 4));
	int bitdepth = *((cm_UInt16*)(p + 14));
	if (format != 1)
		throw std::invalid_argument("unsupported format");
	if (channels == 0 || samplerate == 0 || bitdepth == 0)
		throw std::invalid_argument("bad format");

	// Find data subchunk
	p = FindChunk(data, len, "data", &sz);
	if (!p)
		throw std::invalid_argument("no data subchunk");

	return WavStream(
		samplerate,
		bitdepth,
		channels,
		std::vector<char>(p, p + sz));
}
#endif

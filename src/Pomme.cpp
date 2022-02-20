#include <iostream>
#include <cstring>

#include "Pomme.h"
#include "PommeInit.h"
#include "PommeFiles.h"
#include "PommeGraphics.h"
#include "PommeSound.h"
#include "PommeInput.h"

#if _WIN32
	#include "Platform/Windows/PommeWindows.h"
#endif

//-----------------------------------------------------------------------------
// Our own utils

const char* Pomme::QuitRequest::what() const noexcept
{
	return "the user has requested to quit the application";
}

//-----------------------------------------------------------------------------
// Misc

void ExitToShell()
{
	throw Pomme::QuitRequest();
}

void SysBeep(short duration)
{
	(void) duration;

#ifdef _WIN32
	Pomme::Platform::Windows::SysBeep();
#else
	TODOMINOR();
#endif
}

void FlushEvents(short, short)
{
	TODOMINOR();
}

//-----------------------------------------------------------------------------
// Our own init

void Pomme::Init()
{
	Pomme::Files::Init();

#ifndef POMME_NO_GRAPHICS
	Pomme::Graphics::Init();
#endif

#ifndef POMME_NO_SOUND_FORMATS
	Pomme::Sound::InitMidiFrequencyTable();
#endif

#ifndef POMME_NO_SOUND_MIXER
	Pomme::Sound::InitMixer();
#endif

#ifndef POMME_NO_INPUT
	Pomme::Input::Init();
#endif
}

void Pomme::Shutdown()
{
#ifndef POMME_NO_SOUND_MIXER
	Pomme::Sound::ShutdownMixer();
#endif
}

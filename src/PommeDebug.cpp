#include "PommeDebug.h"

#include <SDL.h>
#include <sstream>
#include <iostream>

void ImplementMe(const char* fn, std::string msg, int severity)
{
	if (severity >= 0)
	{
		std::stringstream ss;
		ss << "[TODO] \x1b[1m" << fn << "\x1b[22m";
		if (!msg.empty())
		{
			ss << ": " << msg;
		}
		auto str = ss.str();
		std::cerr << (severity > 0 ? "\x1b[31m" : "\x1b[33m") << str << "\x1b[0m\n";
	}

	if (severity >= 2)
	{
		std::stringstream ss;
		ss << fn << "()";
		if (!msg.empty()) ss << "\n" << msg;

		auto str = ss.str();

		int mbflags = SDL_MESSAGEBOX_ERROR;
		if (severity == 0) mbflags = SDL_MESSAGEBOX_INFORMATION;
		if (severity == 1) mbflags = SDL_MESSAGEBOX_WARNING;

		SDL_ShowSimpleMessageBox(mbflags, "Source port TODO", str.c_str(), nullptr);
	}

	if (severity >= 2)
	{
		abort();
	}
}

std::string Pomme::FourCCString(uint32_t t, char filler)
{
	char b[5];
	*(uint32_t*) b = t;
#if !(TARGET_RT_BIGENDIAN)
	std::reverse(b, b + 4);
#endif
	// Replace any non-alphanumeric character with the filler character.
	// This ensures that the resulting string is suitable for use as a filename.
	for (int i = 0; i < 4; i++)
	{
		if (!isalnum(b[i]))
			b[i] = filler;
	}
	b[4] = '\0';
	return b;
}

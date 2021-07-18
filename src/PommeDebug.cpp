#include "PommeDebug.h"
#include <cstring>
#include <iostream>

void ImplementMe(const char* fn, std::string msg, int severity)
{
	std::stringstream ss;
	ss << "[TODO] \x1b[1m" << fn << "\x1b[22m";

	if (!msg.empty())
		ss << ": " << msg;

	auto str = ss.str();
	std::cerr << (severity > 0 ? "\x1b[31m" : "\x1b[33m") << str << "\x1b[0m\n";

	if (severity >= 2)
		throw std::runtime_error(str);
}

std::string Pomme::FourCCString(uint32_t fourCC, char filler)
{
	char stringBuffer[5];

	int shift = 24;

	for (int i = 0; i < 4; i++)
	{
		char c = (fourCC >> shift) & 0xFF;

		// Replace symbols to make suitable for use as filename
		if (!isalnum(c) && !strchr("!#$%&'()+,-.;=@[]^_`{}", c))
			c = filler;

		stringBuffer[i] = c;

		shift -= 8;
	}

	stringBuffer[4] = '\0';

	return stringBuffer;
}

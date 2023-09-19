#pragma once

#if __cplusplus

#include <string>
#include <sstream>
#include <cstdint>

#if !defined(POMME_DEBUG_MEMORY)
#define POMME_DEBUG_MEMORY		0
#endif

#if !defined(POMME_DEBUG_SOUND)
#define POMME_DEBUG_SOUND		0
#endif

#if !defined(POMME_DEBUG_PICT)
#define POMME_DEBUG_PICT		0
#endif

#if !defined(POMME_DEBUG_FILES)
#define POMME_DEBUG_FILES		0
#endif

#if !defined(POMME_DEBUG_RESOURCES)
#define POMME_DEBUG_RESOURCES	0
#endif

#if !defined(POMME_DEBUG_INPUT)
#define POMME_DEBUG_INPUT		0
#endif

#if !defined(POMME_DEBUG_3DMF)
#define POMME_DEBUG_3DMF		0
#endif

#define POMME_GENLOG(define, prefix) if (!define) {} else std::cout << "[" << prefix << "] " << __func__ << ":\t"
#define POMME_GENLOG_NOPREFIX(define) if (!define) {} else std::cout

namespace Pomme
{
	std::string FourCCString(uint32_t t, char filler = '?');
}

//-----------------------------------------------------------------------------
// My todos

void ImplementMe(const char* fn, std::string msg, int severity);

#define TODOCUSTOM(message, severity) { \
	std::stringstream ss; \
	ss << message; \
	ImplementMe(__func__, ss.str(), severity); \
}

#define TODOFATAL()      TODOCUSTOM("", 2)
#define TODOFATAL2(x)    TODOCUSTOM(x, 2)

#if _DEBUG

	#define TODOMINOR()      TODOCUSTOM("", 0)
	#define TODOMINOR2(x)    TODOCUSTOM(x, 0)
	#define TODO()           TODOCUSTOM("", 1)
	#define TODO2(x)         TODOCUSTOM(x, 1)

	#define ONCE(x)			{ \
		static bool once = false; \
		if (!once) { \
			once = true; \
			{x} \
			printf("  \x1b[90m\\__ this todo won't be shown again\x1b[0m\n"); \
		} \
	}

#else

	#define TODOMINOR()     {}
	#define TODOMINOR2(x)   {}
	#define TODO()          {}
	#define TODO2(x)        {}
	#define ONCE(x)         {x}

#endif // _DEBUG

#endif // __cplusplus

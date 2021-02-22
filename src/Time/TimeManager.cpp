#include "Pomme.h"
#include "PommeTypes.h"

#include <chrono>
#include <ctime>

using namespace std::chrono;

// System time point on application start
static const time_point<high_resolution_clock> gBootTimePoint = high_resolution_clock::now();

// Timestamp of the Mac epoch (Jan 1, 1904, 00:00:00), relative to UNIX epoch (Jan 1, 1970, 00:00:00)
static constexpr int JANUARY_1_1904 = -2'082'844'800;

static int64_t GetMicrosecondsSinceBoot()
{
	auto now = high_resolution_clock::now();
	microseconds usecs = duration_cast<microseconds>(now - gBootTimePoint);
	return usecs.count();
}

//-----------------------------------------------------------------------------
// Time Manager

void GetDateTime(unsigned long* secs)
{
	*secs = (unsigned long) (std::time(nullptr) + JANUARY_1_1904);
}

void Microseconds(UnsignedWide* usecsOut)
{
	auto usecs = GetMicrosecondsSinceBoot();
	usecsOut->lo = (usecs      ) & 0xFFFFFFFFL;
	usecsOut->hi = (usecs >> 32) & 0xFFFFFFFFL;
}

UInt32 TickCount()
{
	// A tick is approximately 1/60 of a second
	return (UInt32) (60L * GetMicrosecondsSinceBoot() / 1'000'000L);
}

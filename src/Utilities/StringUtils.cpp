#include "Utilities/StringUtils.h"

#include <algorithm>
#include <iterator>

u8string UppercaseCopy(const u8string& in)
{
	u8string out;
	std::transform(
		in.begin(),
		in.end(),
		std::back_inserter(out),
		[](unsigned char c) -> unsigned char
		{
			return (c >= 'a' && c <= 'z') ? ('A' + c - 'a') : c;
		});
	return out;
}

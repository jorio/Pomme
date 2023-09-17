#include "Utilities/StringUtils.h"

#include <algorithm>
#include <iterator>

std::u8string UppercaseCopy(const u8string& in)
{
	std::u8string out;
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

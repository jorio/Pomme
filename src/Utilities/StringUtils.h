#pragma once

#include <string>

#if defined(__cplusplus) && __cplusplus > 201703L && defined(__cpp_lib_char8_t)
    typedef std::u8string u8string;
#else
    typedef std::string u8string;
	typedef char char8_t;
#endif

u8string UppercaseCopy(const u8string&);

#pragma once

// Do NOT use the built-in C++20 filesystem implementation on the following configs:
// - macOS: it starts shipping <filesystem> at version 10.15; we want our builds to work on older macOS versions
// - FreeBSD: its implementation appears problematic (on version 13.0-BETA1 with clang 11)
// - GCC 8: its implementation requires linking an extra library; don't bother with that

#if !__APPLE__ \
	&& !__FreeBSD__ \
	&& !(defined(__GNUC__) && __GNUC__ < 9) \
	&& defined(__cplusplus) && __cplusplus >= 201703L \
	&& defined(__has_include) && __has_include(<filesystem>)

    #include <filesystem>
    namespace fs = std::filesystem;

#else

	#define LEGACY_FILESYSTEM_IMPLEMENTATION 1
	#include "CompilerSupport/filesystem_implementation.hpp"
	namespace fs = ghc::filesystem;

#endif

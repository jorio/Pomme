#pragma once

#if !__APPLE__ && !__FreeBSD__ /* don't use built-in filesystem implementation so we can support older macOS and FreeBSD */ && \
defined(__cplusplus) && __cplusplus >= 201703L && defined(__has_include) && __has_include(<filesystem>)
    #include <filesystem>
    namespace fs = std::filesystem;
#else
	#define LEGACY_FILESYSTEM_IMPLEMENTATION 1
    #include "CompilerSupport/filesystem_implementation.hpp"
    namespace fs = ghc::filesystem;
#endif

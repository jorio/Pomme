cmake_minimum_required(VERSION 3.13)
set(CMAKE_CXX_STANDARD 20)
project(Pomme CXX)

set(POMME_SRCDIR src)

set(POMME_SOURCES
	${POMME_SRCDIR}/Pomme.cpp
	${POMME_SRCDIR}/Pomme.h
	${POMME_SRCDIR}/PommeDebug.cpp
	${POMME_SRCDIR}/PommeDebug.h
	${POMME_SRCDIR}/PommeEnums.h
	${POMME_SRCDIR}/PommeFiles.h
	${POMME_SRCDIR}/PommeGraphics.h
	${POMME_SRCDIR}/PommeInit.h
	${POMME_SRCDIR}/PommeInput.h
	${POMME_SRCDIR}/PommeSound.h
	${POMME_SRCDIR}/PommeTypes.h
	${POMME_SRCDIR}/PommeVideo.h
	${POMME_SRCDIR}/Files/Files.cpp
	${POMME_SRCDIR}/Files/HostVolume.cpp
	${POMME_SRCDIR}/Files/HostVolume.h
	${POMME_SRCDIR}/Files/Resources.cpp
	${POMME_SRCDIR}/Files/Volume.h
	${POMME_SRCDIR}/Memory/Memory.cpp
	${POMME_SRCDIR}/Text/TextUtilities.cpp
	${POMME_SRCDIR}/Time/TimeManager.cpp
	${POMME_SRCDIR}/Utilities/bigendianstreams.cpp
	${POMME_SRCDIR}/Utilities/bigendianstreams.h
	${POMME_SRCDIR}/Utilities/FixedPool.h
	${POMME_SRCDIR}/Utilities/GrowablePool.h
	${POMME_SRCDIR}/Utilities/IEEEExtended.cpp
	${POMME_SRCDIR}/Utilities/IEEEExtended.h
	${POMME_SRCDIR}/Utilities/memstream.cpp
	${POMME_SRCDIR}/Utilities/memstream.h
	${POMME_SRCDIR}/Utilities/StringUtils.cpp
	${POMME_SRCDIR}/Utilities/StringUtils.h
	${POMME_SRCDIR}/Utilities/structpack.cpp
	${POMME_SRCDIR}/Utilities/structpack.h
	$<$<BOOL:${WIN32}>:${POMME_SRCDIR}/Platform/Windows/PommeWindows.cpp>
	$<$<BOOL:${WIN32}>:${POMME_SRCDIR}/Platform/Windows/PommeWindows.h>
)

if (NOT(POMME_NO_SOUND_FORMATS))
	list(APPEND POMME_SOURCES
		${POMME_SRCDIR}/SoundFormats/AIFF.cpp
		${POMME_SRCDIR}/SoundFormats/IMA4.cpp
		${POMME_SRCDIR}/SoundFormats/MACE.cpp
		${POMME_SRCDIR}/SoundFormats/Midi.cpp
		${POMME_SRCDIR}/SoundFormats/SoundFormats.cpp
		${POMME_SRCDIR}/SoundFormats/xlaw.cpp
	)
else()
	add_compile_definitions(POMME_NO_SOUND_FORMATS)
endif()

if (NOT(POMME_NO_SOUND_MIXER))
	list(APPEND POMME_SOURCES
		${POMME_SRCDIR}/SoundMixer/ChannelImpl.cpp
		${POMME_SRCDIR}/SoundMixer/ChannelImpl.h
		${POMME_SRCDIR}/SoundMixer/cmixer.cpp
		${POMME_SRCDIR}/SoundMixer/cmixer.h
		${POMME_SRCDIR}/SoundMixer/SoundManager.cpp
	)
else()
	add_compile_definitions(POMME_NO_SOUND_MIXER)
endif()

if (NOT(POMME_NO_GRAPHICS))
	list(APPEND POMME_SOURCES
		${POMME_SRCDIR}/Graphics/ARGBPixmap.cpp
		${POMME_SRCDIR}/Graphics/Color.cpp
		${POMME_SRCDIR}/Graphics/ColorManager.cpp
		${POMME_SRCDIR}/Graphics/Graphics.cpp
		${POMME_SRCDIR}/Graphics/Icons.cpp
		${POMME_SRCDIR}/Graphics/PICT.cpp
		${POMME_SRCDIR}/Graphics/SysFont.h
		${POMME_SRCDIR}/Graphics/SystemPalettes.cpp
	)
else()
	add_compile_definitions(POMME_NO_GRAPHICS)
endif()

if (NOT(POMME_NO_INPUT))
	list(APPEND POMME_SOURCES
		${POMME_SRCDIR}/Input/SDLInput.cpp
	)
else()
	add_compile_definitions(POMME_NO_INPUT)
endif()

if (NOT(POMME_NO_VIDEO))
	list(APPEND POMME_SOURCES
		${POMME_SRCDIR}/Video/Cinepak.cpp
		${POMME_SRCDIR}/Video/Cinepak.h
		${POMME_SRCDIR}/Video/moov.cpp
	)
else()
	add_compile_definitions(POMME_NO_VIDEO)
endif()

if (NOT(POMME_NO_QD3D))
	list(APPEND POMME_SOURCES
			${POMME_SRCDIR}/QD3D/3DMFInternal.h
			${POMME_SRCDIR}/QD3D/3DMFParser.cpp
			${POMME_SRCDIR}/QD3D/QD3D.cpp
			${POMME_SRCDIR}/QD3D/QD3D.h
			${POMME_SRCDIR}/QD3D/QD3DMath.cpp
			${POMME_SRCDIR}/QD3D/QD3DMath.h
			)
else()
	add_compile_definitions(POMME_NO_QD3D)
endif()

add_library(${PROJECT_NAME} ${POMME_SOURCES})

find_package(SDL2 REQUIRED)

add_compile_definitions(
	"$<$<CONFIG:DEBUG>:_DEBUG>"
)

target_include_directories(${PROJECT_NAME} PRIVATE
	${SDL2_INCLUDE_DIRS}
	${POMME_SRCDIR}
)

if(MSVC)
	# By default, MSVC may add /EHsc to CMAKE_CXX_FLAGS, which we don't want (we use /EHs below)
	string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})

	# By default, MSVC may add /W3 to CMAKE_CXX_FLAGS, which we don't want (we use /W4 below)
	# Note that this is not required with "cmake_minimum_required(VERSION 3.15)" or later
	string(REPLACE "/W3" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})

	# On Win32, we need NOGDI and NOUSER to be able to define some Mac functions
	# whose names are otherwise taken by Windows APIs.
	target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOGDI NOUSER)

	target_compile_options(${PROJECT_NAME} PRIVATE
		/EHs	# synchronous exceptions; also, extern "C" functions may throw exceptions
		/W4
		/wd4068 # ignore unrecognized pragmas
		/wd4201 # nonstandard extension (nameless struct)
		/wd4244 # conversion from double to float
		/wd4458 # declaration of variable hides class member
		/MP		# multiprocessor compilation
	)
else()
	target_compile_options(${PROJECT_NAME} PRIVATE
		-fexceptions
		-Wall
		-Wextra
		-Wshadow
		-Wno-multichar
		-Wno-unknown-pragmas
		-Wstrict-aliasing=2
	)
endif()

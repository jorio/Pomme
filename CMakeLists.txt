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
	${POMME_SRCDIR}/PommeTime.h
	${POMME_SRCDIR}/PommeTypes.h
	${POMME_SRCDIR}/PommeVideo.h
	${POMME_SRCDIR}/Files/Files.cpp
	${POMME_SRCDIR}/Files/HostVolume.cpp
	${POMME_SRCDIR}/Files/HostVolume.h
	${POMME_SRCDIR}/Files/Resources.cpp
	${POMME_SRCDIR}/Files/Volume.h
	${POMME_SRCDIR}/Graphics/ARGBPixmap.cpp
	${POMME_SRCDIR}/Graphics/Color.cpp
	${POMME_SRCDIR}/Graphics/Graphics.cpp
	${POMME_SRCDIR}/Graphics/PICT.cpp
	${POMME_SRCDIR}/Graphics/SysFont.h
	${POMME_SRCDIR}/Graphics/SystemPalettes.cpp
	${POMME_SRCDIR}/Input/SDLInput.cpp
	${POMME_SRCDIR}/Memory/Memory.cpp
	${POMME_SRCDIR}/Sound/AIFF.cpp
	${POMME_SRCDIR}/Sound/cmixer.cpp
	${POMME_SRCDIR}/Sound/cmixer.h
	${POMME_SRCDIR}/Sound/IMA4.cpp
	${POMME_SRCDIR}/Sound/MACE.cpp
	${POMME_SRCDIR}/Sound/SoundManager.cpp
	${POMME_SRCDIR}/Sound/xlaw.cpp
	${POMME_SRCDIR}/Text/TextUtilities.cpp
	${POMME_SRCDIR}/Time/TimeManager.cpp
	${POMME_SRCDIR}/Utilities/BigEndianIStream.cpp
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
	${POMME_SRCDIR}/Video/Cinepak.cpp
	${POMME_SRCDIR}/Video/Cinepak.h
	${POMME_SRCDIR}/Video/moov.cpp
	$<$<BOOL:${WIN32}>:${POMME_SRCDIR}/Platform/Windows/PommeWindows.cpp>
	$<$<BOOL:${WIN32}>:${POMME_SRCDIR}/Platform/Windows/PommeWindows.h>
)

add_library(${PROJECT_NAME} ${POMME_SOURCES})

find_package(SDL2 REQUIRED)

target_include_directories(${PROJECT_NAME} PRIVATE
	${SDL2_INCLUDE_DIRS}
	${POMME_SRCDIR}
)

if(MSVC)
	# On Win32, we need NOGDI and NOUSER to be able to define some Mac functions
	# whose names are otherwise taken by Windows APIs.
	target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOGDI NOUSER)

	target_compile_options(${PROJECT_NAME} PRIVATE
		/EHa
		/W4
		/wd4100	# unreferenced formal parameter
		/wd4201 # nonstandard extension
		/wd4244 # conversion from double to float
		/wd4458 # declaration of variable hides class member
	)
else()
	target_compile_options(${PROJECT_NAME} PRIVATE
		-Wall
		-Wextra
		-Wshadow
		-Wno-multichar
		-Wno-unused-parameter
		-fexceptions
	)
endif()

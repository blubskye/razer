cmake_minimum_required(VERSION 3.10)
project(razer C CXX)

# ---------------------------------------------------------------------------
# Custom build types: O3 and O3LTO
# Usage: cmake -B build -DCMAKE_BUILD_TYPE=O3
#        cmake -B build -DCMAKE_BUILD_TYPE=O3LTO
# ---------------------------------------------------------------------------
set(CMAKE_C_FLAGS_O3   "-O3 -DNDEBUG" CACHE STRING "C flags for O3 build"   FORCE)
set(CMAKE_CXX_FLAGS_O3 "-O3 -DNDEBUG" CACHE STRING "C++ flags for O3 build" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_O3    "" CACHE STRING "Linker flags for O3 build"        FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_O3 "" CACHE STRING "Shared linker flags for O3 build" FORCE)

set(CMAKE_C_FLAGS_O3LTO   "-O3 -DNDEBUG" CACHE STRING "C flags for O3+LTO build"   FORCE)
set(CMAKE_CXX_FLAGS_O3LTO "-O3 -DNDEBUG" CACHE STRING "C++ flags for O3+LTO build" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_O3LTO    "" CACHE STRING "Linker flags for O3+LTO build"        FORCE)
set(CMAKE_SHARED_LINKER_FLAGS_O3LTO "" CACHE STRING "Shared linker flags for O3+LTO build" FORCE)

include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo_ok OUTPUT _ipo_out LANGUAGES C CXX)
if(_ipo_ok)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_O3LTO ON)
else()
    message(WARNING "LTO not supported by this toolchain: ${_ipo_out}")
endif()

if (COMMAND cmake_policy)
	# Libraries linked via full path no longer produce linker search paths.
	cmake_policy(SET CMP0003 NEW)
	# Preprocessor definition values shall be escaped automatically.
	cmake_policy(SET CMP0005 NEW)
endif (COMMAND cmake_policy)

include("${razer_SOURCE_DIR}/scripts/cmake.global")
include(CheckIncludeFile)

macro(CHECK_LIB _LIB _HEADER)
	check_include_file(${_HEADER} ${_HEADER}_ERR)
	if (NOT ${_HEADER}_ERR)
		message(FATAL_ERROR "Could not find library \"lib${_LIB}\" "
			"with header ${_HEADER}")
	endif (NOT ${_HEADER}_ERR)
endmacro(CHECK_LIB)

check_lib(usb-1.0 libusb.h)

add_subdirectory(librazer)
add_subdirectory(librazerd)

configure_file("udev.rules.template" "udev.rules")

install(FILES ${CMAKE_BINARY_DIR}/udev.rules
	DESTINATION /etc/udev/rules.d
	RENAME 80-razer.rules)

configure_file("pm-hook.sh.template" "pm-hook.sh" @ONLY)

install(FILES ${CMAKE_BINARY_DIR}/pm-hook.sh
	DESTINATION /etc/pm/sleep.d
	RENAME 50-razer
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
		    GROUP_READ GROUP_EXECUTE
		    WORLD_READ WORLD_EXECUTE)

configure_file("razerd.service.template" "razerd.service")

install(FILES ${CMAKE_BINARY_DIR}/razerd.service
	DESTINATION /usr/lib/systemd/system)
	
install(FILES ${CMAKE_SOURCE_DIR}/tmpfile.conf
	DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/tmpfiles.d
	RENAME razerd.conf)

add_subdirectory(razerd)
add_subdirectory(ui)

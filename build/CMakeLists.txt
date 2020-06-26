cmake_minimum_required(VERSION 2.6)
project(nvi2 C)

include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckCSourceCompiles)

# unused, just hide them
mark_as_advanced(FORCE CMAKE_BUILD_TYPE CMAKE_INSTALL_PREFIX)

option(DEBUG "Build a debugging version" OFF)
option(USE_WIDECHAR "Enable wide character support" ON)
option(USE_ICONV "Enable iconv support" ON)

function(JOIN VALUES OUTPUT)
	string(REPLACE ";" " " _TMP_STR "${VALUES}")
	set(${OUTPUT} "${_TMP_STR}" PARENT_SCOPE)
endfunction()

if(DEBUG)
	set(_arg_str "${CMAKE_C_FLAGS} -O0 -g"
		"-DCOMLOG"
		"-Wall"
		"-Wno-parentheses"
		"-Wno-uninitialized"	# meaningless without -O
		"-Wmissing-prototypes"
		"-Wsystem-headers"
		"-Wstack-protector -fstack-protector"
		"-Wstrict-aliasing -fstrict-aliasing")
	if("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
		set(_arg_str "${_arg_str}"
			"-Wno-unused-const-variable")
	endif()
	join("${_arg_str}" CMAKE_C_FLAGS)
else(DEBUG)
	set(_arg_str "${CMAKE_C_FLAGS} -O2"
		"-Wuninitialized"
		"-Wstack-protector -fstack-protector")
	if("${CMAKE_C_COMPILER_ID}" STREQUAL "Clang")
		set(_arg_str "${_arg_str}"
			"-Wno-dangling-else"
			# avoid the "argument unused" error in clang
			"-Qunused-arguments")
	endif()
	join("${_arg_str}" CMAKE_C_FLAGS)
endif(DEBUG)

# commands to generate the public headers
set(extract_protos sed -n 's/^ \\* PUBLIC: \\\(.*\\\)/\\1/p')
set(extract_version sed -n
	's/^.*version \\\([^\)]*\)\\\).*/\#define VI_VERSION \\\"\\1\\\"/p')
add_custom_command(OUTPUT ../cl/extern.h
	COMMAND ${extract_protos} ../cl/*.c > $@
	DEPENDS ../cl/*.c)
add_custom_command(OUTPUT ../common/extern.h
	COMMAND ${extract_protos} ../common/*.c > $@
	DEPENDS ../common/*.c)
add_custom_command(OUTPUT ../ex/extern.h
	COMMAND ${extract_protos} ../ex/*.c > $@
	DEPENDS ../ex/*.c)
add_custom_command(OUTPUT ../vi/extern.h
	COMMAND ${extract_protos} ../vi/*.c > $@
	DEPENDS ../vi/*.c)
add_custom_command(OUTPUT ../common/options_def.h
	COMMAND awk -f ../common/options.awk ../common/options.c > $@
	DEPENDS ../common/options.c)
add_custom_command(OUTPUT ../ex/ex_def.h
	COMMAND awk -f ../ex/ex.awk ../ex/ex_cmd.c > $@
	DEPENDS ../ex/ex_cmd.c)
add_custom_command(OUTPUT ../ex/version.h
	COMMAND ${extract_version} ../README > $@
	DEPENDS ../README)

set(MAIN_PROTOS
	../cl/extern.h ../common/extern.h ../ex/extern.h ../vi/extern.h
	../common/options_def.h ../ex/ex_def.h ../ex/version.h)
FILE(GLOB MAIN_SRCS ../cl/*.c ../common/*.c ../ex/*.c ../vi/*.c)
FILE(GLOB REGEX_SRCS ../regex/reg*.c)

check_function_exists(openpty UTIL_IN_LIBC)
if(NOT UTIL_IN_LIBC)
	find_library(UTIL_LIBRARY util)
endif(NOT UTIL_IN_LIBC)

check_function_exists(__b64_ntop RESOLV_IN_LIBC)
if(NOT RESOLV_IN_LIBC)
	find_library(RESOLV_LIBRARY resolv)
endif(NOT RESOLV_IN_LIBC)

include_directories(. ..)

find_library(_curses_lib NAMES ncurses curses HINTS /usr/lib)
find_library(_cursesw_lib NAMES ncursesw cursesw curses HINTS /usr/lib)
mark_as_advanced(_curses_lib _cursesw_lib)

if(USE_WIDECHAR)
	set(CURSES_LIBRARY ${_cursesw_lib})

	# link to the wchar_t awared BSD libregex.a
	add_definitions(-D__REGEX_PRIVATE)
	include_directories(../regex)
	add_library(regex STATIC ${REGEX_SRCS})
else(USE_WIDECHAR)
	set(CURSES_LIBRARY ${_curses_lib})
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-pointer-sign")
endif(USE_WIDECHAR)

if(USE_ICONV)
	check_function_exists(__iconv ICONV_IN_LIBC)
	if(NOT ICONV_IN_LIBC)
		find_path(ICONV_INCLUDE_DIR iconv.h)
		find_library(ICONV_LIBRARY iconv)
	endif(NOT ICONV_IN_LIBC)

	# detect the prototype of iconv(3)
	set(CMAKE_C_FLAGS_BACKUP "${CMAKE_C_FLAGS}")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Werror")
	set(CMAKE_REQUIRED_INCLUDES "${ICONV_INCLUDE_DIR}")
	set(CMAKE_REQUIRED_LIBRARIES "${ICONV_LIBRARY}")
	check_c_source_compiles("
	#include <iconv.h>
	int main() {
		iconv_t conv = 0;
		char* in = 0;
		size_t ilen = 0;
		char* out = 0;
		size_t olen = 0;
		iconv(conv, &in, &ilen, &out, &olen);
		return 0;
	}
	" ICONV_TRADITIONAL)
	set(CMAKE_REQUIRED_INCLUDES)
	set(CMAKE_REQUIRED_LIBRARIES)
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS_BACKUP}")

	include_directories(${ICONV_INCLUDE_DIR})
endif(USE_ICONV)

check_include_files(libutil.h HAVE_LIBUTIL_H)
check_include_files(ncurses.h HAVE_NCURSES_H)
check_include_files(term.h HAVE_TERM_H)

configure_file(${CMAKE_SOURCE_DIR}/config.h.in ${CMAKE_SOURCE_DIR}/config.h)

set(vi_cv_path_preserve /var/tmp/vi.recover/)
if(APPLE)
	set(vi_cv_path_msgcat /usr/local/share/vi/catalog/)
else()
	set(vi_cv_path_msgcat /usr/share/vi/catalog/)
endif()

configure_file(${CMAKE_SOURCE_DIR}/pathnames.h.in
	${CMAKE_SOURCE_DIR}/pathnames.h)
configure_file(${CMAKE_SOURCE_DIR}/recover.in
	${CMAKE_SOURCE_DIR}/recover @ONLY)

add_executable(nvi ${MAIN_SRCS} ${MAIN_PROTOS})
target_link_libraries(nvi ${CURSES_LIBRARY} ${UTIL_LIBRARY} ${RESOLV_LIBRARY})
if(USE_WIDECHAR)
	target_link_libraries(nvi regex)
	if(USE_ICONV)
		target_link_libraries(nvi ${ICONV_LIBRARY})
	endif(USE_ICONV)
endif(USE_WIDECHAR)

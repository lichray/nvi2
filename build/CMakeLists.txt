cmake_minimum_required(VERSION 2.6)
project(nvi2)

include(CheckIncludeFiles)
include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckLibraryExists)
include(CheckCSourceCompiles)

# unused, just hide them
mark_as_advanced(FORCE CMAKE_BUILD_TYPE CMAKE_INSTALL_PREFIX)

option(DEBUG "Build a debugging version" OFF)
option(HAVE_BSD_CURSES "Old BSD Style curses library" OFF)
option(USE_WIDECHAR "Enable wide character support" ON)
option(USE_ICONV "Enable iconv support" ON)

function(JOIN VALUES OUTPUT)
	string(REPLACE ";" " " _TMP_STR "${VALUES}")
	set(${OUTPUT} "${_TMP_STR}" PARENT_SCOPE)
endfunction()

if(DEBUG)
	set(_arg_str "${CMAKE_C_FLAGS} -ansi -O0 -g"
		"-fstrict-aliasing -Wstrict-aliasing"
		"-Wformat"
		"-Wmissing-prototypes"
		"-Wmissing-braces"
		"-Wno-pointer-sign")
	join("${_arg_str}" CMAKE_C_FLAGS)
else(DEBUG)
	set(_arg_str "${CMAKE_C_FLAGS} -ansi -O2"
		"-Wuninitialized"
		"-Wno-pointer-sign")
	join("${_arg_str}" CMAKE_C_FLAGS)
endif(DEBUG)

# generate the public headers
execute_process(COMMAND ./distrib WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

FILE(GLOB MAIN_SRCS ../cl/*.c ../common/*.c ../ex/*.c ../vi/*.c)
FILE(GLOB REGEX_SRCS ../regex/reg*.c)

check_function_exists(vfork HAVE_VFORK)
check_function_exists(revoke HAVE_REVOKE)

check_c_source_compiles("
#include <sys/types.h>
#include <sys/time.h>
int main(void) { gettimeofday(0); }" HAVE_BROKEN_GETTIMEOFDAY)

check_c_source_compiles("
#include <fcntl.h>
int main(void) { flock(0, 0); }" HAVE_LOCK_FLOCK)

if(NOT HAVE_LOCK_FLOCK)
	check_c_source_compiles("
	#include <fcntl.h>
	int main(void) { flock(0, F_SETLK, 0); }" HAVE_LOCK_FCNTL)
endif(NOT HAVE_LOCK_FLOCK)

include_directories(. ..)
check_include_files(ncursesw/ncurses.h HAVE_NCURSESW_NCURSES_H)
check_include_files(ncurses.h HAVE_NCURSES_H)

find_library(_curses_lib NAMES ncurses curses)
find_library(_cursesw_lib NAMES ncursesw cursesw)
mark_as_advanced(_curses_lib _cursesw_lib)

if(USE_WIDECHAR)
	set(CURSES_LIBRARY ${_cursesw_lib})

	# link to the wchar_t awared BSD libregex.a
	add_definitions(-D__REGEX_PRIVATE)
	include_directories(../regex)
	add_library(regex STATIC ${REGEX_SRCS})
else(USE_WIDECHAR)
	set(CURSES_LIBRARY ${_curses_lib})
endif(USE_WIDECHAR)

if(USE_ICONV)
	find_path(ICONV_INCLUDE_DIR iconv.h)
	check_function_exists(libiconv_open ICONV_IN_LIBC)
	if(NOT ICONV_IN_LIBC)
		find_library(ICONV_LIBRARY iconv)
	endif(NOT ICONV_IN_LIBC)
	include_directories(${ICONV_INCLUDE_DIR})
endif(USE_ICONV)

check_library_exists(${CURSES_LIBRARY} waddnstr "" HAVE_CURSES_WADDNSTR)
check_library_exists(${CURSES_LIBRARY} beep "" HAVE_CURSES_BEEP)
check_library_exists(${CURSES_LIBRARY} flash "" HAVE_CURSES_FLASH)
check_library_exists(${CURSES_LIBRARY} idlok "" HAVE_CURSES_IDLOK)
check_library_exists(${CURSES_LIBRARY} keypad "" HAVE_CURSES_KEYPAD)
check_library_exists(${CURSES_LIBRARY} newterm "" HAVE_CURSES_NEWTERM)
check_library_exists(${CURSES_LIBRARY} setupterm "" HAVE_CURSES_SETUPTERM)
check_library_exists(${CURSES_LIBRARY} tigetstr "" HAVE_CURSES_TIGETSTR)

check_include_files(sys/mman.h HAVE_SYS_MMAN_H)
check_include_files(sys/select.h HAVE_SYS_SELECT_H)
check_include_files(sys/stropts.h HAVE_SYS_STROPTS_H)
check_include_files(term.h HAVE_TERM_H)

check_function_exists(grantpt HAVE_SYS5_PTY)

configure_file(${CMAKE_SOURCE_DIR}/config.h.in ${CMAKE_SOURCE_DIR}/config.h)

find_program(vi_cv_path_shell sh)
find_program(vi_cv_path_sendmail sendmail)

# test for existing vi.recover first, then try to mkdir if failed
find_file(vi_cv_path_preserve vi.recover
	PATHS /var/preserve /var/tmp /usr/tmp NO_DEFAULT_PATH)
if(NOT vi_cv_path_preserve)
	foreach(_path /var/preserve /var/tmp /usr/tmp)
		execute_process(COMMAND /bin/mkdir -p ${_path}/vi.recover
			RESULT_VARIABLE _path_st)
		if(NOT _path_st)
			set(vi_cv_path_preserve ${_path}/vi.recover)
			break()
		endif(NOT _path_st)
	endforeach(_path /var/preserve /var/tmp /usr/tmp)
endif(NOT vi_cv_path_preserve)

mark_as_advanced(vi_cv_path_shell vi_cv_path_sendmail vi_cv_path_preserve)

configure_file(${CMAKE_SOURCE_DIR}/pathnames.h.in
	${CMAKE_SOURCE_DIR}/pathnames.h)
configure_file(${CMAKE_SOURCE_DIR}/recover.in
	${CMAKE_SOURCE_DIR}/recover @ONLY)

add_executable(nvi ${MAIN_SRCS})
target_link_libraries(nvi ${CURSES_LIBRARY})
if(USE_WIDECHAR)
	target_link_libraries(nvi regex)
	if(USE_ICONV)
		target_link_libraries(nvi ${ICONV_LIBRARY})
	endif(USE_ICONV)
endif(USE_WIDECHAR)

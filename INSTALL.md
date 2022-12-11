
# Installing from source

For a detailed explanation of building nvi2 see [porting](https://github.com/lichray/nvi2/wiki/Porting)
in the wiki. This document is just a quick overview of the build and installation process.

## Overview

nvi2 project supports multiple building approaches.

- Out-of-source build
- Ninja
- Multi-configuration build with Ninja

## System Requirements

- CMake >= 3.9;
- Ninja
- POSIX.1-2008-compatible libc;
- libiconv (for USE_ICONV);
- libncursesw (for USE_WIDECHAR);
- libutil;
- uudecode(1) with -m option (Base64);

Anything required by a minimal nvi-1.79, notably:

- Berkeley DB1 in libc;
- /var/tmp/vi.recover/ with mode 41777.

## Building nvi2 using cmake and Ninja

nvi2 project supports multi-configuration builds including with the [Ninja](https://ninja-build.org/).
Ninga is a very fast parallel build system. You will need to have cmake, ninja build, and
your C toolchain installed.

Building nvi2 using cmake and ninja is done with the following commands

> cmake -G Ninja -B build -DCMAKE_BUILD_TYPE=Debug
> ninja -C build

## Multi-configuration build with Ninja

It is often annoying to switch the build between Debug and Release. With CMake >= 3.17, you can hold both in the same build directory:

> cmake -G "Ninja Multi-Config" -B build
> ninja -C build              # default -- do debug build
> ninja -C build -f build-Release.ninja  # do release build

The debug build produces the nvi binary under build/Debug/, and the release build produces binary under build/Release/.


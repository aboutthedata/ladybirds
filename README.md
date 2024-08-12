# Ladybirds project

This implements a Ladybirds C compiler.
It takes a program specified in Ladybirds C language and translates it, using different backends to choose from,
to multi-threaded code running on different multi-core system.

For more high-level documentation, please refer to the scientific work related to Ladybirds:  
Andreas Tretter: [On Efficient Data Exchange in Multicore Architectures](https://doi.org/10.3929/ethz-b-000309314), 
TIK-Schriftenreihe 175, ETH Zürich, 2018.

## Installation

### Dependencies

To build Ladybirds, the following development packets (i.e., libraries + includes) must be available on the system:

  * [Clang/LLVM](https://Clang.llvm.org/), version 15
  * [Lua](https://www.lua.org/), version 5.3 or higher.


At runtime, the following lua libraries need to be available:

  * [LuaFileSystem](https://keplerproject.github.io/luafilesystem/)
  * [lua-fastache](https://github.com/aboutthedata/lua-fastache)

### How to build

CMake is used to build Ladybirds. For simplicity, we recommend building under Linux.
To build and install in the simplest form, one can issue following commands in a bash shell in the Ladybirds directory:

   ````bash
   mkdir build
   cd build
   cmake ..
   cmake --build . -j9
   sudo cmake --install .
   ````

## Usage

The command line syntax for ladybirds is  

    ladybirds [options] <specification file>

Where *specification file* is a `.lb` source file, written in Ladybirds C.
Code examples can be found in the `examples` folder.
Currently, only single-translation-unit programs are supported.

The following options can be chosen:

  * `-b=<backend>`: The most important option. Specifies which back-end is to be used, i.e.,
    for which target platform code is to be produced.
    Specify `-b=list` to list all available back-ends.
  * `-p=<project info>`: Specify a project information file.
    It is a simple lua script containing information on which additional files (sources, headers, inputs, ...)
    should be added to the generated code.
  * `-m=<mapping spec>`: Specify a mapping information file.
    This is a simple lua script containing information on which tasks are to be placed in the same group,
    i.e., depending on the back-end, they will end up in the same thread, on the same processing element or similar.

Ladybirds will generate its output in the local working directory, typically in a subfolder called `gencode/<backend>`.


## Implementing back-ends

A Ladybirds back-end is essentially a lua script. It will be instantiated by the ladybirds compiler,
but then drives the entire process of parsing the inputs, running optimisation passes and generating the output code.
Ladybirds will search for back-ends in the installation folder (typically `/usr/share/ladybirds/codegen` under Linux),
but also in the user's home folder (typically `~/.ladybirds/codegen` under Linux).
Each back-end will have a subfolder of its name in one of these paths. This folder then must contain a `main.lua`,
which is launched after executing `codegen/common/init.lua`. Have a look at the existing backends to see how they work.


## Copyright and distribution

The code has been created at and is owned by ETH Zürich,
Computer Engineering and Networks Laboratory (TIK) under Prof. Dr. Lothar Thiele.
©2022 ETH Zürich.  
Contributors: Dr. Andreas Tretter, Matthias Baer, Marc Beusch, Praveenth Sanmugarajah, Marc Urech.

Using Clang/LLVM as its parser, it is distributed under the same open licence, Apache License v2.0 with LLVM Exceptions.
See LICENSE.TXT for details.

The contents of the folder `src/parse/Clang-code` have been taken directly from the Clang source code
and are distributed here under the same open licence.
The copyright ownership for these files remains with the respective holders.

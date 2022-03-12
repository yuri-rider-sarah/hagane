# The Hagane Programming Language

Hagane is a compiled, statically-typed, functional programming language.

## Installation

Move the executable, either downloaded or built, into a directory in `$PATH`.

## Building

Only do this if necessary. The x86\_64 Linux version is avaiable to download as a release.

Compiling v0.1.2 of the Hagane compiler requires v0.1.1 of the compiler to be available in `$PATH` as `hagane-0.1.1`.
It also requires LLVM-12, `clang++`, and Python 3.

To build the compiler, execute:
```
make
```

The compiler will be found in the file named `hagane-0.1.2`.

## Usage
```
hagane-0.1.2 input-file ... [OPTION ...]
```

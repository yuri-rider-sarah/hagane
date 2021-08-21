# The Hagane Programming Language

Hagane is a compiled, statically-typed, functional programming language.

To learn how to use the language, read the Language Guide, which is available in `guide.md`.

## Installation

Move the executable, either downloaded or built, into a directory in `$PATH`.

## Building

Only do this if necessary. The x86\_64 Linux version is avaiable to download as a release.

Compiling v0.1 of the Hagane compiler requires [v0.0.5 of the compiler](https://github.com/yuri-rider-sarah/hagane-old) to be available in `$PATH` as `hagane-0.0.5`.
It also requires LLVM-12, `clang++`, and Python 3.

First, execute:
```
make
```

The compiler produced by this will be correct, but very inefficient because it was compiler with v0.0.5.
To produce a more efficient compiler, execute the following commands.
Note that this will require a large amount of memory (around 30 GB) and take a long time.

```
mv hagane-0.1 hagane-0.1_
make HAGANE=./hagane-0.1_
rm hagane-0.1_
```

## Usage
```
hagane-0.1 input-file ... [OPTION ...]
```
For the available options, see the appropriate chapter of the Language Guide.

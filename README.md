![Logo](resources/logo.svg)

# Orbit

A low-latency cryptocurrency arbitrage detector and surveillance tool made in modern C++ (C++ 17).

The purpose of this project was to research and practice implementing advanced low-latency programming techniques and concurrency in C++ that are common within high-frequency trading.

In addition, to learn how market surveillance and arbitrage detection work in real markets using Orbit.

Orbit practices the following:

1. TCP/IP & Socket Programming
2. Concurrency in C++
3. Template metaprogramming
4. Multithreading
5. Compile time optimization

## Building Orbit

Either you can run the provided bash script ``build.sh`` or type the following in your terminal:

```sh
mkdir build
cd build
cmake ..
make
```

## How cryptocurrency arbitrage works

## How Orbit works

## Project Organization

```txt
docs/ --> Doxygen project documentation
resources/ --> Any additional files (images, etc.) 
src/ --> Application source code
external/ --> 3rd-Party Libraries
libs/ --> Static Libraries
cmake/ --> CMake configuration files
```

## Disclaimer

Orbit is **NOT** a tool to determine financial decisions and I do not take any responsibility regarding the decisions a user may take. I do not give financial advise whatsoever and the development of Orbit is purely for educational purposes.

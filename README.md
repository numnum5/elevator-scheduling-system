# Elevator Control System

This project is a C++ rewrite and continuation of an elevator control-system assignment from my third-year Systems Programming course.

Checklist:
- [x] Pass all test cases provided by the original project
- [ ] Implement multi car scheduling algorithm
- [ ] Implement common logging interface

## Motivation

When I first completed this project at university, I was unable to make the implementation as complete or reliable as I wanted. Some edge cases remained unresolved, and the program did not pass every test case.

I have returned to the project as a personal challenge: to understand the original problems, redesign the weaker parts, and build the system as correctly and completely as possible. As part of that process, I am rewriting the original C implementation in modern C++ (C++ 20). This gives me an opportunity to improve the program's structure and safety while revisiting concepts such as concurrent programming, inter-process communication, networking, synchronization, and scheduling, as well as learn more about C++ programming.

The goal is not merely to reproduce the original submission, but to create the robust version of the project that I wanted to build the first time.

## Project overview

The program simulates a multi-car elevator system. A central controller receives passenger requests, tracks the available elevator cars, and selects a suitable car to service each request. Separate processes represent the controller, elevator cars, external call buttons, and each car's internal controls.

The implementation uses:

- TCP sockets for communication between the controller, cars, and call clients
- POSIX shared memory for exposing an elevator car's state to its internal controls
- POSIX threads, mutexes, and condition variables for concurrent state management
- A scheduling system for assigning calls and ordering requested floors
- Support for normal operation, individual service mode, emergency state, door controls, and basement floors

## Components

- `controller` coordinates the system and assigns requests to elevator cars.
- `car` simulates an individual elevator and its state transitions.
- `call` submits a request from one floor to another.
- `internal` represents the controls inside an elevator car.
- `tcp` provides shared message-transport helpers.
- `logger` provides common logging support.

## Building

The project requires a C++20 compiler, CMake 3.20 or newer, and a POSIX-compatible environment.

```sh
cmake -S . -B build
cmake --build build --target build_all
```

The resulting executables are placed in the `build` directory.

## Example interfaces

```text
./build/controller
./build/car <name> <lowest-floor> <highest-floor> <delay-ms>
./build/call <source-floor> <destination-floor>
./build/internal <car-name> <operation>
```

Floor names may be ordinary floor numbers, such as `1` or `12`, or basement floors such as `B1` and `B2`.

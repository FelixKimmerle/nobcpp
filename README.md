# nobcpp

> **⚠️ WARNING: Pre-Alpha Software**  
> nobcpp is in a very early stage of development.  
> Expect bugs, missing features, and breaking changes.  
> **Do not use in production or for critical projects (yet)!**

**nobcpp** is a C++-only build tool loosely inspired by [nob](https://github.com/tsoiding/nob), designed to simplify and accelerate the build process for large, modular C++ projects.

- **Self-contained:** Can be bootstrapped with only a C++23 compiler—no external dependencies required.
- **Self-rebuilding:** Automatically rebuilds itself if the build process definition changes.

## Features

- **Parallel builds**  
  Efficiently compiles multiple files in parallel to speed up build times (currently uses a simple, naive approach).

- **Incremental builds**  
  Only recompiles files that have changed, saving time on repeated builds.

- **Automatic header dependency tracking**  
  Detects changes in header files and automatically rebuilds all source files that include them, ensuring correct and up-to-date builds.

## Upcoming Features

- **Flexible build profiles**  
  Easily switch between different build configurations (e.g., debug, release, sanitizers) without duplicating configuration or directory structures.

 - **Export compile database**  
  Generate a `compile_commands.json` file for integration with tools like clangd and other language servers.

## Goals

- Make building large, modular C++ projects straightforward and fast.
- Minimize external dependencies—nobcpp is implemented in pure C++.
- Provide a clean and scalable alternative to traditional build systems.

---

> There are n build tools out there, and they all suck.  
> When I'm finished with this project, there will be n+1 build tools and they will all suck.

 *Feedback, suggestions, and contributions are welcome!*

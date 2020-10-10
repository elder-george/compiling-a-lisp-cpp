# Compiling a Lisp, C++ edition

Based on the [series of posts](https://bernsteinbear.com/blog/compiling-a-lisp-0/).

## To compile

Use `cmake` with [`vcpkg`](https://github.com/microsoft/vcpkg):

    $ cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake -G Ninja
    $ cmake --build build


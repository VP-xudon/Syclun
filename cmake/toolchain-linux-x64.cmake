# ============================================================
# cmake/toolchain-linux-x64.cmake
#
# Cross-compile Synth-OOP (Syclun) for Linux x86_64 from a host that
# provides the `x86_64-linux-gnu` toolchain (common on Debian/Ubuntu as
# `gcc-x86_64-linux-gnu`, or via the `musl-cross` / `crosstool-ng`
# families). Used by package.sh when building the `linux-x64`
# distribution target on a non-Linux host.
#
# 用 x86_64-linux-gnu 工具链从其它宿主交叉编译 Synth-OOP（Syclun）到
# Linux x86_64。package.sh 在非 Linux 宿主上构建 linux-x64 分发包时
# 通过 -DCMAKE_TOOLCHAIN_FILE 引用本文件。
# ============================================================

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_CXX_COMPILER x86_64-linux-gnu-g++)
set(CMAKE_C_COMPILER   x86_64-linux-gnu-gcc)

# The project pins -O2 and (on Linux) relies only on the C++ standard
# library, so a static-ish or glibc-linked binary built here runs on any
# x86_64 Linux with a reasonably modern glibc.
# 项目固定 -O2，且 Linux 下仅依赖 C++ 标准库；此处产出的二进制可在任何
# 带较新 glibc 的 x86_64 Linux 上运行。

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

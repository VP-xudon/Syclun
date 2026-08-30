# ============================================================
# cmake/toolchain-windows-arm64.cmake
#
# Cross-compile Synth-OOP (Syclun) for Windows on ARM64 from any
# host that provides the `aarch64-w64-mingw32` MinGW-w64 toolchain
# (e.g. the `mingw-w64-aarch64` / `aarch64-w64-mingw32-gcc` packages,
# or LLVM-MinGW). Used by package.sh when building the
# `windows-arm64` distribution target.
#
# 用 aarch64-w64-mingw32 工具链从任意宿主交叉编译 Synth-OOP
# （Syclun）到 Windows ARM64。package.sh 构建 windows-arm64 分发包时
# 通过 -DCMAKE_TOOLCHAIN_FILE 引用本文件。
# ============================================================

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Compilers — adjust the triple if your distribution names them differently
# (e.g. `aarch64-w64-mingw32-g++-posix`). / 若发行版命名不同请相应调整。
set(CMAKE_CXX_COMPILER aarch64-w64-mingw32-g++)
set(CMAKE_C_COMPILER   aarch64-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER   aarch64-w64-mingw32-windres)

# The 32 MB native stack (RECURSION_LIMIT = 1000) is set by the project's
# CMakeLists.txt via -Wl,--stack,33554432 for GNU ld, so nothing extra here.
# 项目 CMakeLists.txt 已对 GNU ld 设 -Wl,--stack,33554432（32MB 原生栈），
# 此处无需额外设置。

# Search the cross toolchain's sysroot first; do not pull host libraries.
# 优先搜索交叉工具链的 sysroot；不引入宿主库。
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

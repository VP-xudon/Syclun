# ============================================================
# CMakeLists.txt — Synth-OOP interpreter + verification suite
# Synth-OOP 解释器与验证套件的 CMake 构建
#
# Replaces the hand-written build.sh: one description of the build that works
# on Windows (MinGW / MSVC), Linux, macOS and other Unix systems, with
# incremental compilation handled by the generator (Make / Ninja / MSBuild).
# 取代手写的 build.sh：一份构建描述即可在 Windows（MinGW / MSVC）、Linux、
# macOS 及其它 Unix 系统上工作，增量编译由生成器（Make / Ninja / MSBuild）
# 负责。
#
# Quick start / 快速开始：
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j
#   ctest --test-dir build --output-on-failure      # run all suites
#
# Faster builds / 更快的构建（推荐 Ninja）：
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#
# Binaries land directly in the build directory (build/) because the
# interpreter resolves its standard library as "<exe_dir>/../lib"
# (see src/main.cpp).
# 二进制直接落在构建目录（build/）下，因为解释器按 "<exe_dir>/../lib"
# 解析标准库（见 src/main.cpp）。
# ============================================================

cmake_minimum_required(VERSION 3.16)

project(synthoop
    VERSION 1.27.0
    DESCRIPTION "Synth-OOP: a verification-philosophy object language"
    LANGUAGES CXX
)

# ---- C++ standard -------------------------------------------------------
# C++23. cxx_std_23 / CMAKE_CXX_STANDARD 23 require CMake >= 3.20, so older
# CMake gets the equivalent raw compiler flag instead (keeps older distro
# toolchains usable).
# C++23。cxx_std_23 / CMAKE_CXX_STANDARD 23 需要 CMake >= 3.20，故较旧的
# CMake 改用等价的原始编译选项（使旧发行版工具链仍可用）。
set(CMAKE_CXX_EXTENSIONS OFF)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.20")
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
else()
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-std=c++23" SYNTHOOP_HAS_CXX23)
    if(SYNTHOOP_HAS_CXX23)
        add_compile_options(-std=c++23)
    else()
        check_cxx_compiler_flag("-std=c++2b" SYNTHOOP_HAS_CXX2B)
        if(SYNTHOOP_HAS_CXX2B)
            add_compile_options(-std=c++2b)
        else()
            message(FATAL_ERROR "A C++23-capable compiler is required "
                                "(needs -std=c++23 or -std=c++2b).")
        endif()
    endif()
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# The project has always been validated at -O2, so pin it explicitly rather
# than inheriting the generator default (-O3 on GCC/Clang).
# 本项目一直以 -O2 验证，故显式固定它，而非沿用生成器默认（GCC/Clang 为
# -O3）。
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "-O2")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")
endif()

# The project is header-only: every target below is a single translation unit
# that includes the headers. They are independent executables, so there is no
# library target to build — keeping the graph flat makes builds fast and lets
# the generator compile all five targets in parallel.
# 本项目为 header-only：下方每个目标都是包含这些头文件的单一翻译单元。
# 它们是相互独立的可执行文件，故没有库目标——扁平的依赖图使构建更快，
# 并让生成器并行编译全部五个目标。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
foreach(cfg IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} "${CMAKE_BINARY_DIR}")
endforeach()

# The headers live in src/ and the native standard libraries in lib/cpp/.
# 头文件位于 src/，原生标准库位于 lib/cpp/。
set(SYNTHOOP_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src"
                          "${CMAKE_CURRENT_SOURCE_DIR}/lib/cpp")

# ---- Per-target helper --------------------------------------------------
# Add one interpreter/test executable and apply the settings shared by all of
# them: include dirs and (where the platform supports it) a 32 MB native
# stack.
# 添加一个解释器 / 测试可执行文件，并应用全部目标共用的设置：包含目录，
# 以及（平台支持时）32MB 的原生栈。
function(synthoop_add_executable target source)
    add_executable(${target} "${source}")
    target_include_directories(${target} PRIVATE ${SYNTHOOP_INCLUDE_DIRS})

    # A large native stack keeps the interpreter's recursion guard
    # (RECURSION_LIMIT = 1000) reachable: each Synth self-recursion level
    # consumes several C++ frames, and the Windows default of 2 MB overflows
    # long before the guard can fire. Enlarge it where a link-time setting
    # exists; on Linux/BSD the stack is an rlimit (see the note printed below).
    # 更大的原生栈使解释器的递归护栏（上限 1000）可达：每层 Synth 自递归
    # 消耗多个 C++ 帧，Windows 默认 2MB 远在护栏触发前就会溢出。凡有链接期
    # 设置的平台一律放大；Linux/BSD 的栈是 rlimit（见下方提示）。
    if(WIN32)
        if(MSVC)
            target_link_options(${target} PRIVATE /STACK:33554432)
        else()   # MinGW / GNU ld
            target_link_options(${target} PRIVATE -Wl,--stack,33554432)
        endif()
    elseif(APPLE)
        # ld64 takes a hexadecimal stack size / ld64 的栈大小用十六进制。
        target_link_options(${target} PRIVATE -Wl,-stack_size,0x2000000)
    endif()
endfunction()

# ---- Targets ------------------------------------------------------------
# The interpreter itself.
# 解释器本体。
synthoop_add_executable(synth "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
set_target_properties(synth PROPERTIES OUTPUT_NAME "synth")

# Verification suite / 验证套件。
synthoop_add_executable(parser_cli
    "${CMAKE_CURRENT_SOURCE_DIR}/verify/unit/parser_cli.cpp")
synthoop_add_executable(assert_lexer
    "${CMAKE_CURRENT_SOURCE_DIR}/verify/unit/assert_lexer.cpp")
synthoop_add_executable(assert_parser
    "${CMAKE_CURRENT_SOURCE_DIR}/verify/unit/assert_parser.cpp")
synthoop_add_executable(assert_runtimes
    "${CMAKE_CURRENT_SOURCE_DIR}/verify/unit/assert_runtimes.cpp")

# The runtime suite must find `synth` beside it; CMake guarantees that order.
# 运行时套件需要在其旁找到 `synth`；CMake 保证该构建顺序。
add_dependencies(assert_runtimes synth)

# ---- Testing ------------------------------------------------------------
enable_testing()
add_test(NAME lexer   COMMAND assert_lexer)
add_test(NAME parser  COMMAND assert_parser)
add_test(NAME runtimes COMMAND assert_runtimes)

# Convenience: build and run every suite in one step.
# 便捷：一步构建并运行全部套件。
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS synth assert_lexer assert_parser assert_runtimes
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Build and run the full Synth-OOP verification suite"
    VERBATIM
)

# ---- Notes --------------------------------------------------------------
message(STATUS "Synth-OOP ${PROJECT_VERSION}")
message(STATUS "  C++ standard : C++${CMAKE_CXX_STANDARD} (fallback flags below 3.20)")
message(STATUS "  Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "  Binaries     : ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
if(UNIX AND NOT APPLE)
    message(STATUS "  Note: on Linux/BSD the stack size is a runtime limit; if a")
    message(STATUS "        deep-recursion test overflows, raise it with 'ulimit -s 32768'.")
endif()

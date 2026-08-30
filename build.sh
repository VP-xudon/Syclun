#!/usr/bin/env bash
# ============================================================
# build.sh — thin convenience wrapper around the CMake build
# build.sh —— CMake 构建之上的便捷封装
#
# CMakeLists.txt is the single source of truth for how this project is built
# (see the header there for the full rationale). This script only forwards to
# it, so the two can never drift apart, and it keeps the familiar `bash
# build.sh` entry point working.
# CMakeLists.txt 是本项目构建方式的唯一事实来源（完整理由见其文件头注释）。
# 本脚本仅做转发，故二者永不脱节，同时保留熟悉的 `bash build.sh` 入口。
#
# Usage / 用法：
#   ./build.sh                 # configure (if needed) + build / 配置（如需）+ 构建
#   ./build.sh --clean         # wipe the build dir and rebuild / 清空构建目录后重建
#   ./build.sh --test          # build, then run all suites / 构建后运行全部套件
#   ./build.sh --debug         # Debug build / 调试构建
#   ./build.sh --ninja         # prefer the Ninja generator / 优先用 Ninja 生成器
#
# The project is portable: this same flow works on Windows (MinGW / MSVC),
# Linux, macOS and other Unix systems. Binaries land in build/.
# 本项目可移植：同一流程适用于 Windows（MinGW / MSVC）、Linux、macOS 及其它
# Unix 系统。二进制落在 build/ 下。
# ============================================================
set -e

cd "$(dirname "$0")"            # project root / 项目根目录

BUILD_DIR="build"
BUILD_TYPE="Release"
GENERATOR=""
DO_CLEAN=0
DO_TEST=0

while [ $# -gt 0 ]; do
    case "$1" in
        --clean)  DO_CLEAN=1 ;;
        --test)   DO_TEST=1 ;;
        --debug)  BUILD_TYPE="Debug" ;;
        --ninja)  GENERATOR="Ninja" ;;
        -h|--help)
            sed -n '2,30p' "$0"
            exit 0
            ;;
        *)
            echo "Unknown option: $1 (try --help)" >&2
            exit 2
            ;;
    esac
    shift
done

# Prefer Ninja when available: it is markedly faster than Make for this
# header-only project (all five binaries are independent translation units).
# Ninja 可用时优先之：对本项目（header-only，五个二进制各自独立翻译单元）
# 它显著快于 Make。
if [ -z "$GENERATOR" ] && command -v ninja >/dev/null 2>&1; then
    GENERATOR="Ninja"
fi

if [ "$DO_CLEAN" -eq 1 ]; then
    echo "[*] cleaning $BUILD_DIR/"
    rm -rf "$BUILD_DIR"
fi

GEN_ARGS=()
if [ -n "$GENERATOR" ]; then
    GEN_ARGS=(-G "$GENERATOR")
fi

echo "[1/2] configure  ($BUILD_TYPE, ${GENERATOR:-default generator})"
cmake -S . -B "$BUILD_DIR" "${GEN_ARGS[@]}" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

echo "[2/2] build"
cmake --build "$BUILD_DIR"

if [ "$DO_TEST" -eq 1 ]; then
    echo "[+] run verification suite / 运行验证套件"
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

echo "Build complete. Binaries are in $BUILD_DIR/"
echo "构建完成，二进制位于 $BUILD_DIR/"

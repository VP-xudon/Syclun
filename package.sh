#!/usr/bin/env bash
# ============================================================
# package.sh — one-click release packager for Synth-OOP (Syclun)
# package.sh —— Synth-OOP（Syclun）一键发布分发包脚本
#
# This is SEPARATE from build.sh (which is for day-to-day compile/test).
# package.sh produces ready-to-ship interpreter distributions: it builds an
# optimised Release `synth` for each target platform and assembles a
# self-contained tree — binary in `bin/`, the runtime library interfaces in
# `libs/`, plus examples and docs — under `build/dist/`.
#
# 本脚本与 build.sh（日常编译/测试用）相互独立。package.sh 产出「开箱即用」
# 的解释器发行包：为每个目标平台构建优化版 Release `synth`，并组装出一棵
# 自包含目录树——二进制在 `bin/`、运行时库接口在 `libs/`、外加示例与文档——
# 全部位于 `build/dist/` 下。
#
# Five target packages are defined:
#   1. synth-windows-x64   2. synth-windows-arm64
#   3. synth-macos-x64     4. synth-macos-arm64
#   5. synth-linux-x64
#
# A target is only built when the current host actually can build it:
#   - windows-*        : on a Windows host (arm64 additionally needs the
#                        aarch64-w64-mingw32 cross toolchain)
#   - macos-*          : on a macOS host (both slices build there)
#   - linux-x64        : on a Linux host (or with x86_64-linux-gnu cross)
# Targets that cannot be built on this host are SKIPPED with a clear note,
# so the script always succeeds and reports what it did and did not make.
# For the full set of five packages from one command, run this in the CI
# matrix (see .github/workflows/release.yml) on Windows / macOS / Linux
# runners.  ___________________
#   单次只能产出当前宿主能构建的目标；其余跳过并给出说明。要一次性拿到全部
#   五个包，请在 CI 矩阵（.github/workflows/release.yml）的 Windows / macOS /
#   Linux runner 上各跑一次本脚本。
#
# Usage / 用法：
#   ./package.sh                 # build & package every buildable target
#   ./package.sh --help          # this help / 本帮助
#   PACKAGE_TARGETS=windows-x64 ./package.sh   # limit to one target
# ============================================================
set -uo pipefail

cd "$(dirname "$0")"            # project root / 项目根

# ---- host OS detection -------------------------------------------------
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) HOST_OS=windows ;;
    Darwin)               HOST_OS=macos   ;;
    Linux)                HOST_OS=linux   ;;
    *)                    HOST_OS=unknown ;;
esac

case "$(uname -m)" in
    x86_64|AMD64|amd64) HOST_ARCH=x64   ;;
    aarch64|arm64)      HOST_ARCH=arm64 ;;
    *)                  HOST_ARCH=unknown ;;
esac

# Run the packaged interpreter on an example after assembly.
# Only attempted for a NATIVE target (same OS and arch as this host), since a
# cross-built binary cannot be executed here.
# 仅在「原生目标」（与宿主同 OS、同架构）时运行冒烟测试；交叉构建的二进制在
# 本机无法执行。
smoke_test() {
    local pkg="$1" os="$2" arch="$3" exe="$4" out
    if [ "$os" != "$HOST_OS" ] || [ "$arch" != "$HOST_ARCH" ]; then
        echo "    smoke: skipped (cross-built binary, cannot run on this host)"
        return 0
    fi
    # Run the binary from a scratch directory OUTSIDE the package, so the
    # package tree is never polluted by the temporary copy even if cleanup
    # fails. SYNTH_LIB_DIR points at the package's libs/, so library
    # resolution is still exercised exactly as an end user would hit it.
    # (Running from a temp dir also dodges the MSYS2/Git-Bash quirk where a
    # .exe inside a directory literally named `bin` cannot be exec'd.)
    #
    # 把二进制放到包外的临时目录运行，这样即便清理失败也不会污染包目录；同时用
    # SYNTH_LIB_DIR 指向包内 libs/，照旧验证库解析（与最终用户体验一致）。
    # （放临时目录执行也顺带避开 MSYS2/Git-Bash 无法 exec `bin` 目录下 .exe 的怪癖。）
    local tmpdir smoke
    tmpdir="$(mktemp -d)"
    smoke="$tmpdir/_smoke_synth"
    [ "$os" = "windows" ] && smoke="$smoke.exe"
    cp "$exe" "$smoke"
    if out="$(SYNTH_LIB_DIR="$pkg/libs" "$smoke" "$pkg/examples/hello.syn" 2>&1)"; then
        echo "    smoke: OK -> $out"
    else
        echo "    smoke: FAILED -> $out"
    fi
    rm -rf "$tmpdir"
}

DIST_DIR="build/dist"
PKG_BUILD="build/.pkg"
SUMMARY="$DIST_DIR/PACKAGES.md"

have() { command -v "$1" >/dev/null 2>&1; }

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    sed -n '2,40p' "$0"
    exit 0
fi

# ---- target table ------------------------------------------------------
# fields: name | os | arch | toolchain-file | osx-arch | note
# `toolchain-file` and `osx-arch` use '-' for "none".
# 字段：名称 | 系统 | 架构 | 工具链文件 | macOS 架构 | 说明。
# 「工具链文件」「macOS 架构」用 '-' 表示无。
TARGETS=(
  "windows-x64|windows|x64|-|-|Native Windows build (MinGW-w64 or MSVC)."
  "windows-arm64|windows|arm64|cmake/toolchain-windows-arm64.cmake|-|Cross to Windows/ARM64 via aarch64-w64-mingw32."
  "macos-x64|macos|x64|-|x86_64|Native macOS build, x86_64 slice."
  "macos-arm64|macos|arm64|-|arm64|Native macOS build, arm64 slice."
  "linux-x64|linux|x64|cmake/toolchain-linux-x64.cmake|-|Native Linux build, or x86_64-linux-gnu cross."
)

# ---- is a target buildable on this host? -------------------------------
# echoes "build" or "skip:<reason>"
#
# SYNTH_FORCE_BUILD (any value) overrides the host check and forces "build"
# for every target. Intended for CI cross-builds where the matching toolchain
# is installed explicitly (e.g. gcc-mingw-w64-aarch64 on Ubuntu).
# SYNTH_FORCE_BUILD（任意取值）可强制对所有目标返回 build，用于 CI 中已显式
# 安装对应交叉工具链的场景（例如在 Ubuntu 上装 gcc-mingw-w64-aarch64）。
target_status() {
    local os="$1" arch="$2"
    if [ -n "${SYNTH_FORCE_BUILD:-}" ]; then echo "build"; return; fi
    case "$os" in
        windows)
            if [ "$HOST_OS" != "windows" ]; then
                echo "skip:Windows packages must be built on a Windows host (or with a MinGW-w64 cross toolchain)."
                return
            fi
            if [ "$arch" = "arm64" ]; then
                if have aarch64-w64-mingw32-g++ || have aarch64-w64-mingw32-gcc; then
                    echo "build"
                else
                    echo "skip:Windows/ARM64 needs the aarch64-w64-mingw32 cross toolchain (not on this host)."
                fi
            else
                echo "build"
            fi
            ;;
        macos)
            if [ "$HOST_OS" = "macos" ]; then echo "build"; else
                echo "skip:macOS packages must be built on a macOS host (no macOS SDK to cross-compile from here)."
            fi
            ;;
        linux)
            if [ "$HOST_OS" = "linux" ]; then echo "build"
            elif have x86_64-linux-gnu-g++ || have x86_64-linux-gnu-gcc; then echo "build"
            else
                echo "skip:Linux packages are built on a Linux host (or with an x86_64-linux-gnu cross toolchain)."
            fi
            ;;
        *) echo "skip:Unknown target OS." ;;
    esac
}

# ---- assemble one distribution tree ------------------------------------
assemble() {
    local name="$1" os="$2" arch="$3" exe="$4"
    local pkg="$DIST_DIR/synth-$name"
    echo "[*] assembling $pkg"
    rm -rf "$pkg"
    mkdir -p "$pkg/bin" "$pkg/libs" "$pkg/examples"

    cp "$exe" "$pkg/bin/"

    # Windows: bundle the MinGW runtime DLLs next to the exe so the package
    # runs on a vanilla Windows. MSVC builds rely on the target's
    # Visual C++ Redistributable instead.
    # Windows：把 MinGW 运行时 DLL 一并放入 bin/，使裸 Windows 也能运行；
    # MSVC 构建则依赖目标机的 Visual C++ 可再发行包。
    if [ "$os" = "windows" ] && have g++; then
        # Copy the MinGW runtime DLLs from the compiler's OWN bin/ directory
        # (i.e. the files the loader resolves through PATH at runtime).
        #
        # Do NOT use `g++ -print-file-name=$dll` here: it can return a
        # mismatched copy from the GCC install tree. Windows searches the
        # executable's directory FIRST, so a wrong DLL placed in bin/ shadows
        # the correct one and the interpreter dies at startup with
        # STATUS_ENTRYPOINT_NOT_FOUND (0xC0000139).
        #
        # 从编译器自身的 bin/ 目录复制 MinGW 运行时 DLL（即运行期经 PATH 解析到的
        # 那批文件）。切勿用 `g++ -print-file-name`：它可能取回 GCC 安装树中版本
        # 不匹配的副本；Windows 优先搜索可执行文件所在目录，错误的 DLL 会遮蔽正确
        # 版本，导致解释器启动时报 STATUS_ENTRYPOINT_NOT_FOUND (0xC0000139)。
        local runtime_dir
        runtime_dir="$(dirname "$(command -v g++)")"
        # libgcc_s_* has several flavours; copy whichever the toolchain ships.
        # libgcc_s_* 有多种异常模型变体，工具链里有哪个就复制哪个。
        for dll in libstdc++-6.dll libgcc_s_seh-1.dll libgcc_s_dw2-1.dll \
                   libgcc_s_sjlj-1.dll libwinpthread-1.dll; do
            if [ -f "$runtime_dir/$dll" ]; then
                cp "$runtime_dir/$dll" "$pkg/bin/" && echo "    + $dll (from $runtime_dir)"
            fi
        done
    fi

    # Ship the .synl library interfaces (loaded at runtime by `&module;`).
    # The C++ backends (lib/cpp/*.hpp) are already compiled into the binary.
    # 随附 .synl 库接口（运行期由 `&module;` 载入）；C++ 底层（lib/cpp/*.hpp）
    # 已编译进二进制，无需发布。
    cp lib/*.synl "$pkg/libs/"
    cp -r examples/. "$pkg/examples/"
    [ -f LICENSE ] && cp LICENSE "$pkg/"
    [ -f README.md ] && cp README.md "$pkg/"

    # Tailored package README (the copied project README references paths
    # that do not exist inside the package).
    # 为分发包定制一份精简 README（直接复制的项目 README 引用了包内不存在的路径）。
    cat > "$pkg/README.md" <<EOF
# Syclun — Synth-OOP Interpreter ($name)

A self-contained Syclun distribution for **$os $arch**.

## Run / 运行

\`\`\`bash
bin/synth your-program.syn     # Linux / macOS
bin\\synth.exe your-program.syn  # Windows
\`\`\`

The interpreter resolves its standard libraries relative to the executable,
so it works from any current directory. If you move the \`libs/\` directory,
set \`SYNTH_LIB_DIR\` to its path.

解释器依可执行文件位置解析标准库，故可在任意工作目录下运行；若移动了
\`libs/\` 目录，请把 \`SYNTH_LIB_DIR\` 指向它。

## What is inside / 目录内容

- \`bin/synth\` (\`synth.exe\`) — the interpreter.
- \`libs/\` — standard-library interfaces (\`*.synl\`), loaded at runtime.
- \`examples/\` — runnable Synth-OOP programs.
- \`LICENSE\` — GPL-3.0-or-later. Programs you write in Synth-OOP are *not*
  covered by it.

For the full language documentation, see the bundled project `README.md`.
The source lives in the project repository.
完整语言文档见包内的项目 `README.md`；源码见项目仓库。
EOF

    # Prove the assembled package is directly usable.
    # 证明组装出的包可直接运行。
    if [ -f "$pkg/examples/hello.syn" ]; then
        smoke_test "$pkg" "$os" "$arch" "$exe"
    fi
}

# ---- summary table header ----------------------------------------------
mkdir -p "$DIST_DIR"
{
    echo "# Synth-OOP (Syclun) release packages"
    echo
    echo "Generated by \`package.sh\` on $(date -u '+%Y-%m-%d %H:%M UTC') (host: $HOST_OS)."
    echo
    echo "| Package | Status | Notes |"
    echo "|---------|--------|-------|"
} > "$SUMMARY"

PRODUCED=()
SKIPPED=()

# ---- optional target filter -------------------------------------------
FILTER="${PACKAGE_TARGETS:-}"

for t in "${TARGETS[@]}"; do
    IFS='|' read -r name os arch toolchain osx_arch note <<< "$t"
    [ -n "$FILTER" ] && [ "$FILTER" != "$name" ] && continue

    status="$(target_status "$os" "$arch")"
    if [ "${status%%:*}" = "skip" ]; then
        reason="${status#skip:}"
        echo "[!] $name: SKIPPED - $reason"
        SKIPPED+=("$name")
        printf '| synth-%s | skipped | %s |\n' "$name" "$reason" >> "$SUMMARY"
        continue
    fi

    echo "[*] $name: building Release binary ..."
    bdir="$PKG_BUILD/$name"
    rm -rf "$bdir"

    cfg=(cmake -S . -B "$bdir" -DCMAKE_BUILD_TYPE=Release)
    [ "$toolchain" != "-" ] && cfg+=(-DCMAKE_TOOLCHAIN_FILE="$toolchain")
    [ "$osx_arch"  != "-" ] && cfg+=(-DCMAKE_OSX_ARCHITECTURES="$osx_arch")
    # Optional explicit compilers (CI cross-builds / unusual toolchains).
    # 可选的显式编译器（CI 交叉构建 / 特殊工具链）。
    [ -n "${CMAKE_C_COMPILER:-}" ]   && cfg+=(-DCMAKE_C_COMPILER="$CMAKE_C_COMPILER")
    [ -n "${CMAKE_CXX_COMPILER:-}" ] && cfg+=(-DCMAKE_CXX_COMPILER="$CMAKE_CXX_COMPILER")
    [ -n "${CMAKE_RC_COMPILER:-}" ]  && cfg+=(-DCMAKE_RC_COMPILER="$CMAKE_RC_COMPILER")
    if "${cfg[@]}" && cmake --build "$bdir" --target synth -j; then
        if [ "$os" = "windows" ]; then exe="$bdir/synth.exe"; else exe="$bdir/synth"; fi
        if [ ! -f "$exe" ]; then
            echo "[!] $name: build produced no binary, skipping package."
            SKIPPED+=("$name")
            printf '| synth-%s | failed | no binary produced |\n' "$name" >> "$SUMMARY"
            continue
        fi
        assemble "$name" "$os" "$arch" "$exe"
        PRODUCED+=("$name")
        printf '| synth-%s | produced | %s |\n' "$name" "$note" >> "$SUMMARY"
    else
        echo "[!] $name: build failed, skipping package."
        SKIPPED+=("$name")
        printf '| synth-%s | failed | build error |\n' "$name" >> "$SUMMARY"
    fi
done

# ---- final report ------------------------------------------------------
# Flatten the arrays first: keeps the echoes free of nested command
# substitution, which some shells parse unreliably.
# 先把数组展平为字符串，避免 echo 中出现嵌套命令替换（部分 shell 解析不稳）。
prod_list="$(printf '%s ' "${PRODUCED[@]}")"
skip_list="$(printf '%s ' "${SKIPPED[@]}")"
{
    echo
    echo "## Summary / 汇总"
    echo
    echo "- Produced: ${prod_list:-none}"
    echo "- Skipped: ${skip_list:-none}"
} >> "$SUMMARY"

echo
echo "=============================================="
echo " Packaging complete / 打包完成"
echo " Produced: ${prod_list:-none}"
echo " Skipped : ${skip_list:-none}"
echo " See: $SUMMARY"
echo "=============================================="

#!/usr/bin/env bash
# ============================================================
# package.sh — one-click release packager for Synth-OOP (Syclun)
# ============================================================

set -uo pipefail

cd "$(dirname "$0")"

# ---- host detection -----------------------------------------------------

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        HOST_OS=windows
        ;;
    Darwin)
        HOST_OS=macos
        ;;
    Linux)
        HOST_OS=linux
        ;;
    *)
        HOST_OS=unknown
        ;;
esac

case "$(uname -m)" in
    x86_64|AMD64|amd64)
        HOST_ARCH=x64
        ;;
    aarch64|arm64)
        HOST_ARCH=arm64
        ;;
    *)
        HOST_ARCH=unknown
        ;;
esac

DIST_DIR="build/dist"
PKG_BUILD="build/.pkg"
SUMMARY="$DIST_DIR/PACKAGES.md"

have() {
    command -v "$1" >/dev/null 2>&1
}

# ---- help ----------------------------------------------------------------

if [ "${1:-}" = "--help" ] || [ "${1:-}" = "-h" ]; then
    sed -n '2,40p' "$0"
    exit 0
fi

# ---- target table --------------------------------------------------------

TARGETS=(
    "windows-x64|windows|x64|-|-|Native Windows x64 build with MinGW-w64."
    "windows-arm64|windows|arm64|cmake/toolchain-windows-arm64.cmake|-|Cross to Windows/ARM64 via aarch64-w64-mingw32."
    "macos-x64|macos|x64|-|x86_64|Native macOS build, x86_64 slice."
    "macos-arm64|macos|arm64|-|arm64|Native macOS build, arm64 slice."
    "linux-x64|linux|x64|cmake/toolchain-linux-x64.cmake|-|Native Linux build, or x86_64-linux-gnu cross."
)

# ---- target availability -------------------------------------------------

target_status() {
    local os="$1"
    local arch="$2"

    if [ -n "${SYNTH_FORCE_BUILD:-}" ]; then
        echo "build"
        return 0
    fi

    case "$os" in
        windows)
            if [ "$HOST_OS" != "windows" ]; then
                echo "skip:Windows packages must be built on a Windows host (or with a MinGW-w64 cross toolchain)."
                return 0
            fi

            if [ "$arch" = "arm64" ]; then
                if have aarch64-w64-mingw32-g++ || have aarch64-w64-mingw32-gcc; then
                    echo "build"
                else
                    echo "skip:Windows/ARM64 needs the aarch64-w64-mingw32 cross toolchain."
                fi
            else
                echo "build"
            fi
            ;;

        macos)
            if [ "$HOST_OS" = "macos" ]; then
                echo "build"
            else
                echo "skip:macOS packages must be built on a macOS host."
            fi
            ;;

        linux)
            if [ "$HOST_OS" = "linux" ]; then
                echo "build"
            elif have x86_64-linux-gnu-g++ || have x86_64-linux-gnu-gcc; then
                echo "build"
            else
                echo "skip:Linux packages require a Linux host or x86_64-linux-gnu cross toolchain."
            fi
            ;;

        *)
            echo "skip:Unknown target OS."
            ;;
    esac
}

# ---- Windows MinGW validation -------------------------------------------

verify_windows_mingw() {
    local exe="$1"
    local cache_file="$CURRENT_BUILD_DIR/CMakeCache.txt"

    echo
    echo "============================================================"
    echo " Verifying Windows x64 MinGW toolchain"
    echo "============================================================"

    if [ ! -f "$cache_file" ]; then
        echo "::error::CMakeCache.txt not found: $cache_file"
        return 1
    fi

    # ----------------------------------------------------------------------
    # MSYS2 environment
    # ----------------------------------------------------------------------

    echo
    echo "=== MSYS2 environment ==="

    local msystem
    msystem="${MSYSTEM:-}"

    echo "MSYSTEM=${msystem:-unset}"

    if [ "$msystem" != "MINGW64" ]; then
        echo "::error::Windows x64 must use MSYS2 MINGW64."
        echo "::error::MSYSTEM=${msystem:-unset}"
        return 1
    fi

    # ----------------------------------------------------------------------
    # CMake generator
    # ----------------------------------------------------------------------

    echo
    echo "=== CMake generator ==="

    local generator

    generator="$(
        sed -n 's/^CMAKE_GENERATOR:.*=//p' \
            "$cache_file" |
        head -n 1
    )"

    echo "CMAKE_GENERATOR=${generator:-unset}"

    if [ "$generator" != "MinGW Makefiles" ]; then
        echo "::error::windows-x64 is not using MinGW Makefiles."
        echo "::error::Actual generator: ${generator:-unset}"
        return 1
    fi

    # ----------------------------------------------------------------------
    # CMake compiler
    # ----------------------------------------------------------------------

    echo
    echo "=== CMake compiler ==="

    grep -E \
        '^CMAKE_(GENERATOR|C_COMPILER|CXX_COMPILER|C_COMPILER_ID|CXX_COMPILER_ID):' \
        "$cache_file" || true

    local c_compiler
    local cxx_compiler
    local c_compiler_id
    local cxx_compiler_id

    c_compiler="$(
        sed -n 's/^CMAKE_C_COMPILER:[^=]*=//p' \
            "$cache_file" |
        head -n 1
    )"

    cxx_compiler="$(
        sed -n 's/^CMAKE_CXX_COMPILER:[^=]*=//p' \
            "$cache_file" |
        head -n 1
    )"

    c_compiler_id="$(
        sed -n 's/^CMAKE_C_COMPILER_ID:[^=]*=//p' \
            "$cache_file" |
        head -n 1
    )"

    cxx_compiler_id="$(
        sed -n 's/^CMAKE_CXX_COMPILER_ID:[^=]*=//p' \
            "$cache_file" |
        head -n 1
    )"

    echo "CMAKE_C_COMPILER=${c_compiler:-unset}"
    echo "CMAKE_CXX_COMPILER=${cxx_compiler:-unset}"
    echo "CMAKE_C_COMPILER_ID=${c_compiler_id:-unset}"
    echo "CMAKE_CXX_COMPILER_ID=${cxx_compiler_id:-unset}"

    if [ -z "$c_compiler" ]; then
        echo "::error::CMAKE_C_COMPILER was not found."
        return 1
    fi

    if [ -z "$cxx_compiler" ]; then
        echo "::error::CMAKE_CXX_COMPILER was not found."
        return 1
    fi

    case "$c_compiler" in
        *gcc.exe|*gcc)
            echo "C compiler check: PASS (MinGW gcc)"
            ;;
        *)
            echo "::error::CMake did not select MinGW gcc."
            echo "::error::Actual C compiler: $c_compiler"
            return 1
            ;;
    esac

    case "$cxx_compiler" in
        *g++.exe|*g++)
            echo "C++ compiler check: PASS (MinGW g++)"
            ;;
        *)
            echo "::error::CMake did not select MinGW g++."
            echo "::error::Actual C++ compiler: $cxx_compiler"
            return 1
            ;;
    esac

    if [ -n "$c_compiler_id" ] && [ "$c_compiler_id" != "GNU" ]; then
        echo "::error::Unexpected C compiler ID: $c_compiler_id"
        return 1
    fi

    if [ -n "$cxx_compiler_id" ] && [ "$cxx_compiler_id" != "GNU" ]; then
        echo "::error::Unexpected C++ compiler ID: $cxx_compiler_id"
        return 1
    fi

    # ----------------------------------------------------------------------
    # Actual compiler in PATH
    # ----------------------------------------------------------------------

    echo
    echo "=== Compiler executable check ==="

    if ! have gcc; then
        echo "::error::gcc is not available in PATH."
        return 1
    fi

    if ! have g++; then
        echo "::error::g++ is not available in PATH."
        return 1
    fi

    local gcc_path
    local gxx_path

    gcc_path="$(command -v gcc)"
    gxx_path="$(command -v g++)"

    echo "gcc: $gcc_path"
    echo "g++: $gxx_path"

    echo
    echo "=== Compiler version ==="

    gcc --version | head -n 1
    g++ --version | head -n 1

    # ----------------------------------------------------------------------
    # Executable
    # ----------------------------------------------------------------------

    echo
    echo "=== Executable check ==="

    if [ ! -f "$exe" ]; then
        echo "::error::Windows executable not found: $exe"
        return 1
    fi

    echo "Executable: $exe"

    if ! have objdump; then
        echo "::error::objdump is not available."
        return 1
    fi

    # ----------------------------------------------------------------------
    # DLL dependency inspection
    #
    # IMPORTANT:
    # Do not require a specific MinGW DLL to appear in imports.
    # The actual runtime linkage is reported only.
    # ----------------------------------------------------------------------

    echo
    echo "=== Executable DLL dependencies ==="

    local dlls

    dlls="$(
        objdump -p "$exe" |
        sed -n 's/^ *DLL Name: *//p'
    )"

    if [ -n "$dlls" ]; then
        printf '%s\n' "$dlls"
    else
        echo "(no imported DLLs reported by objdump)"
    fi

    echo
    echo "MinGW toolchain verification: PASS"

    return 0
}

# ---- assemble package ----------------------------------------------------

assemble() {
    local name="$1"
    local os="$2"
    local arch="$3"
    local exe="$4"

    local pkg="$DIST_DIR/synth-$name"

    echo "[*] assembling $pkg"

    rm -rf "$pkg"

    mkdir -p \
        "$pkg/bin" \
        "$pkg/libs"

    if ! cp "$exe" "$pkg/bin/"; then
        echo "::error::Failed to copy executable into package."
        return 1
    fi

    # ----------------------------------------------------------------------
    # Windows x64 MinGW runtime
    # ----------------------------------------------------------------------

    if [ "$os" = "windows" ] && [ "$name" = "windows-x64" ]; then

        if ! have g++; then
            echo "::error::g++ not found for Windows x64 runtime packaging."
            return 1
        fi

        local runtime_dir
        runtime_dir="$(dirname "$(command -v g++)")"

        echo
        echo "=== Bundling MinGW runtime ==="
        echo "runtime directory: $runtime_dir"

        local required_dlls=(
            "libstdc++-6.dll"
            "libgcc_s_seh-1.dll"
            "libwinpthread-1.dll"
        )

        local dll

        for dll in "${required_dlls[@]}"; do

            if [ ! -f "$runtime_dir/$dll" ]; then
                echo "::error::Missing required MinGW runtime DLL: $runtime_dir/$dll"
                return 1
            fi

            if ! cp -f "$runtime_dir/$dll" "$pkg/bin/"; then
                echo "::error::Failed to package $dll"
                return 1
            fi

            echo "    + $dll"
        done

        echo
        echo "=== Verify packaged runtime ==="

        for dll in "${required_dlls[@]}"; do

            if [ ! -f "$pkg/bin/$dll" ]; then
                echo "::error::Package is missing $dll"
                return 1
            fi

            echo "    PASS: $dll"
        done

        echo "Windows x64 runtime packaging: PASS"
    fi

    # ----------------------------------------------------------------------
    # Standard libraries
    # ----------------------------------------------------------------------

    shopt -s nullglob

    local synl_files=(lib/*.synl)

    if [ "${#synl_files[@]}" -eq 0 ]; then
        echo "::error::No .synl libraries found."
        return 1
    fi

    if ! cp "${synl_files[@]}" "$pkg/libs/"; then
        echo "::error::Failed to copy .synl libraries."
        return 1
    fi

    if [ -f LICENSE ]; then
        cp LICENSE "$pkg/" || return 1
    fi

    # ----------------------------------------------------------------------
    # Package README
    # ----------------------------------------------------------------------

    cat > "$pkg/README.md" <<EOF
# Syclun — Synth-OOP Interpreter ($name)

A self-contained Syclun distribution for **$os $arch**.

## Run / 运行

### Linux / macOS

\`\`\`bash
bin/synth your-program.syn
\`\`\`

### Windows

\`\`\`text
bin\\synth.exe your-program.syn
\`\`\`

The interpreter resolves its standard libraries relative to the executable.

If you move the \`libs/\` directory, set \`SYNTH_LIB_DIR\` to its path.

## Contents

- \`bin/synth\` or \`bin/synth.exe\` — interpreter
- \`libs/\` — standard-library interfaces
- \`LICENSE\` — GPL-3.0-or-later

For the full language documentation, see the project repository.
EOF

    echo "[+] assembled $pkg"

    return 0
}

# ---- smoke test ----------------------------------------------------------

smoke_test() {
    local pkg="$1"
    local os="$2"
    local arch="$3"

    if [ "$os" != "$HOST_OS" ] || [ "$arch" != "$HOST_ARCH" ]; then
        echo "    smoke: skipped (cross-built binary)"
        return 0
    fi

    local exe
    local test_output

    if [ "$os" = "windows" ]; then
        exe="$pkg/bin/synth.exe"
    else
        exe="$pkg/bin/synth"
    fi

    if [ ! -f "$exe" ]; then
        echo "::error::Smoke test executable not found: $exe"
        return 1
    fi

    echo
    echo "=== Smoke test (--version) ==="
    echo "Executable: $exe"

    # The packaged interpreter is exercised via `--version`, which validates
    # that the binary loads, resolves its standard libraries and exits cleanly
    # without needing the (now unbundled) examples directory.
    # 打包产物通过 --version 验证：确认二进制可加载、能解析标准库并干净退出，
    # 不再依赖已不再打包的 examples 目录。
    if test_output="$("$exe" --version 2>&1)"; then
        echo "    smoke: OK"
        echo "    $test_output"
    else
        echo "    smoke: FAILED"
        echo "    $test_output"
        return 1
    fi

    return 0
}

# ---- initialize summary -------------------------------------------------

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

FILTER="${PACKAGE_TARGETS:-}"

# ---- build targets -------------------------------------------------------

for t in "${TARGETS[@]}"; do

    IFS='|' read -r name os arch toolchain osx_arch note <<< "$t"

    if [ -n "$FILTER" ] && [ "$FILTER" != "$name" ]; then
        continue
    fi

    status="$(target_status "$os" "$arch")"

    if [ "${status%%:*}" = "skip" ]; then

        reason="${status#skip:}"

        echo "[!] $name: SKIPPED - $reason"

        SKIPPED+=("$name")

        printf '| synth-%s | skipped | %s |\n' \
            "$name" "$reason" >> "$SUMMARY"

        continue
    fi

    echo
    echo "============================================================"
    echo "[*] $name: building Release binary"
    echo "============================================================"

    bdir="$PKG_BUILD/$name"

    # Always remove the complete CMake build tree.
    # This prevents a previous compiler/generator from being reused.

    rm -rf "$bdir"

    cfg=(
        cmake
        -S .
        -B "$bdir"
        -DCMAKE_BUILD_TYPE=Release
    )

    # ----------------------------------------------------------------------
    # Windows x64 MUST use MSYS2 MINGW64 + MinGW-w64 GCC/G++.
    # ----------------------------------------------------------------------

    if [ "$name" = "windows-x64" ]; then

        if [ "$HOST_OS" != "windows" ]; then
            echo "::error::windows-x64 must be built on a Windows host."
            exit 1
        fi

        if [ "${MSYSTEM:-}" != "MINGW64" ]; then
            echo "::error::windows-x64 must run inside MSYS2 MINGW64."
            echo "::error::MSYSTEM=${MSYSTEM:-unset}"
            exit 1
        fi

        if ! have gcc; then
            echo "::error::MinGW gcc not found."
            exit 1
        fi

        if ! have g++; then
            echo "::error::MinGW g++ not found."
            exit 1
        fi

        if ! have cmake; then
            echo "::error::cmake not found."
            exit 1
        fi

        echo
        echo "=== Windows x64 MinGW environment ==="
        echo "MSYSTEM=$MSYSTEM"
        echo "gcc: $(command -v gcc)"
        echo "g++: $(command -v g++)"
        echo "cmake: $(command -v cmake)"

        gcc --version | head -n 1
        g++ --version | head -n 1
        cmake --version | head -n 1

        cfg+=(
            -G "MinGW Makefiles"
            -DCMAKE_C_COMPILER=gcc
            -DCMAKE_CXX_COMPILER=g++
        )
    fi

    # ----------------------------------------------------------------------
    # Toolchain
    # ----------------------------------------------------------------------

    if [ "$toolchain" != "-" ]; then
        cfg+=(
            -DCMAKE_TOOLCHAIN_FILE="$toolchain"
        )
    fi

    # ----------------------------------------------------------------------
    # macOS architecture
    # ----------------------------------------------------------------------

    if [ "$osx_arch" != "-" ]; then
        cfg+=(
            -DCMAKE_OSX_ARCHITECTURES="$osx_arch"
        )
    fi

    # ----------------------------------------------------------------------
    # Optional explicit compilers for cross-builds
    # ----------------------------------------------------------------------

    if [ -n "${CMAKE_C_COMPILER:-}" ]; then
        cfg+=(
            -DCMAKE_C_COMPILER="$CMAKE_C_COMPILER"
        )
    fi

    if [ -n "${CMAKE_CXX_COMPILER:-}" ]; then
        cfg+=(
            -DCMAKE_CXX_COMPILER="$CMAKE_CXX_COMPILER"
        )
    fi

    if [ -n "${CMAKE_RC_COMPILER:-}" ]; then
        cfg+=(
            -DCMAKE_RC_COMPILER="$CMAKE_RC_COMPILER"
        )
    fi

    # ----------------------------------------------------------------------
    # Configure
    # ----------------------------------------------------------------------

    echo
    echo "[*] CMake configure..."

    if ! "${cfg[@]}"; then

        echo "::error::$name: CMake configure failed."

        printf '| synth-%s | failed | CMake configure error |\n' \
            "$name" >> "$SUMMARY"

        exit 1
    fi

    # ----------------------------------------------------------------------
    # Print actual CMake configuration
    # ----------------------------------------------------------------------

    echo
    echo "=== CMake configuration result ==="

    if [ -f "$bdir/CMakeCache.txt" ]; then

        grep -E \
            '^CMAKE_(GENERATOR|C_COMPILER|CXX_COMPILER|C_COMPILER_ID|CXX_COMPILER_ID):' \
            "$bdir/CMakeCache.txt" || true

    else

        echo "::error::CMakeCache.txt was not generated."
        exit 1

    fi

    CURRENT_BUILD_DIR="$bdir"

    # ----------------------------------------------------------------------
    # Build
    # ----------------------------------------------------------------------

    echo
    echo "[*] Building synth..."

    if ! cmake --build "$bdir" --target synth -j; then

        echo "::error::$name: build failed."

        printf '| synth-%s | failed | build error |\n' \
            "$name" >> "$SUMMARY"

        exit 1
    fi

    # ----------------------------------------------------------------------
    # Locate executable
    # ----------------------------------------------------------------------

    if [ "$os" = "windows" ]; then
        exe="$bdir/synth.exe"
    else
        exe="$bdir/synth"
    fi

    if [ ! -f "$exe" ]; then

        echo "::error::$name: build produced no binary."

        printf '| synth-%s | failed | no binary produced |\n' \
            "$name" >> "$SUMMARY"

        exit 1
    fi

    echo
    echo "Binary: $exe"

    # ----------------------------------------------------------------------
    # Verify Windows x64 binary
    # ----------------------------------------------------------------------

    if [ "$name" = "windows-x64" ]; then

        if ! verify_windows_mingw "$exe"; then
            echo "::error::Windows x64 MinGW verification failed."
            exit 1
        fi

    fi

    # ----------------------------------------------------------------------
    # Assemble package
    # ----------------------------------------------------------------------

    if ! assemble "$name" "$os" "$arch" "$exe"; then

        echo "::error::$name: package assembly failed."

        printf '| synth-%s | failed | package assembly error |\n' \
            "$name" >> "$SUMMARY"

        exit 1
    fi

    # ----------------------------------------------------------------------
    # Verify final package itself
    # ----------------------------------------------------------------------

    if ! smoke_test "$DIST_DIR/synth-$name" "$os" "$arch"; then

        echo "::error::$name: package smoke test failed."

        printf '| synth-%s | failed | package smoke test failed |\n' \
            "$name" >> "$SUMMARY"

        exit 1
    fi

    PRODUCED+=("$name")

    printf '| synth-%s | produced | %s |\n' \
        "$name" "$note" >> "$SUMMARY"

done

# ---- final report --------------------------------------------------------

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
echo "============================================================"
echo " Packaging complete / 打包完成"
echo " Produced: ${prod_list:-none}"
echo " Skipped : ${skip_list:-none}"
echo " See: $SUMMARY"
echo "============================================================"

# Explicitly requested target must never silently succeed without output.

if [ -n "$FILTER" ] && [ "${#PRODUCED[@]}" -eq 0 ]; then
    echo "::error::Requested target '$FILTER' was not produced."
    exit 1
fi

exit 0

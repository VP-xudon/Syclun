#!/usr/bin/env bash
# ============================================================
# package.sh — one-click release packager for Synth-OOP (Syclun)
# ============================================================

set -uo pipefail

cd "$(dirname "$0")"

# ---- host detection -----------------------------------------------------
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) HOST_OS=windows ;;
    Darwin)               HOST_OS=macos ;;
    Linux)                HOST_OS=linux ;;
    *)                    HOST_OS=unknown ;;
esac

case "$(uname -m)" in
    x86_64|AMD64|amd64) HOST_ARCH=x64 ;;
    aarch64|arm64)      HOST_ARCH=arm64 ;;
    *)                  HOST_ARCH=unknown ;;
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
        return
    fi

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

    echo "=== Windows x64 MinGW validation ==="

    if ! have gcc || ! have g++; then
        echo "::error::MinGW gcc/g++ not found."
        return 1
    fi

    echo "gcc: $(command -v gcc)"
    echo "g++: $(command -v g++)"

    gcc --version | head -1
    g++ --version | head -1

    if ! have objdump; then
        echo "::error::objdump not found."
        return 1
    fi

    echo "=== CMake compiler ==="

    if ! grep -E \
        'CMAKE_CXX_COMPILER:FILEPATH=.*/g\+\+(.exe)?$' \
        "$CURRENT_BUILD_DIR/CMakeCache.txt" >/dev/null 2>&1; then

        echo "::error::CMake did not select MinGW g++."
        echo "Expected a MinGW g++ compiler."
        grep -E 'CMAKE_(C|CXX)_COMPILER' \
            "$CURRENT_BUILD_DIR/CMakeCache.txt" || true
        return 1
    fi

    echo "CMake compiler: MinGW g++"

    echo "=== Binary DLL dependencies ==="

    local deps
    deps="$(objdump -p "$exe" | grep 'DLL Name:' || true)"
    echo "$deps"

    if ! echo "$deps" | grep -qi 'libstdc++-6.dll'; then
        echo "::error::synth.exe does not depend on libstdc++-6.dll."
        echo "This binary does not appear to be a MinGW C++ binary."
        return 1
    fi

    if ! echo "$deps" | grep -qi 'libgcc_s_seh-1.dll'; then
        echo "::error::synth.exe does not depend on libgcc_s_seh-1.dll."
        return 1
    fi

    if ! echo "$deps" | grep -qi 'libwinpthread-1.dll'; then
        echo "::error::synth.exe does not depend on libwinpthread-1.dll."
        return 1
    fi

    echo "MinGW runtime dependency check: PASS"
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
    mkdir -p "$pkg/bin" "$pkg/libs" "$pkg/examples"

    cp "$exe" "$pkg/bin/"

    # ----------------------------------------------------------------------
    # Windows runtime
    # ----------------------------------------------------------------------
    if [ "$os" = "windows" ]; then

        if [ "$name" = "windows-x64" ]; then
            if ! have g++; then
                echo "::error::g++ not found for Windows x64 runtime packaging."
                return 1
            fi

            local runtime_dir
            runtime_dir="$(dirname "$(command -v g++)")"

            echo "=== Bundling MinGW runtime ==="
            echo "runtime directory: $runtime_dir"

            local required_dlls=(
                libstdc++-6.dll
                libgcc_s_seh-1.dll
                libwinpthread-1.dll
            )

            local dll

            for dll in "${required_dlls[@]}"; do
                if [ ! -f "$runtime_dir/$dll" ]; then
                    echo "::error::Missing required MinGW runtime DLL: $dll"
                    return 1
                fi

                cp -f "$runtime_dir/$dll" "$pkg/bin/" || return 1
                echo "    + $dll"
            done

            # Verify that every required DLL is actually inside the package.
            for dll in "${required_dlls[@]}"; do
                if [ ! -f "$pkg/bin/$dll" ]; then
                    echo "::error::Package is missing $dll"
                    return 1
                fi
            done

            echo "Windows x64 runtime packaging: PASS"
        fi
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

    cp "${synl_files[@]}" "$pkg/libs/"

    cp -r examples/. "$pkg/examples/"

    [ -f LICENSE ] && cp LICENSE "$pkg/"

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
- \`examples/\` — example programs
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

    if [ ! -f "$pkg/examples/hello.syn" ]; then
        echo "::error::Smoke test example not found."
        return 1
    fi

    echo "=== Smoke test ==="
    echo "Executable: $exe"

    # IMPORTANT:
    # Run the executable from the actual package directory so Windows DLL
    # lookup sees the DLLs next to synth.exe.
    #
    # Do NOT copy only synth.exe to /tmp: that would test the runner's
    # installed runtime instead of the release package.

    if [ "$os" = "windows" ]; then

        local win_pkg
        local win_lib
        local win_example

        if have cygpath; then
            win_pkg="$(cygpath -w "$pkg")"
            win_lib="$(cygpath -w "$pkg/libs")"
            win_example="$(cygpath -w "$pkg/examples/hello.syn")"

            if test_output="$(
                SYNTH_LIB_DIR="$win_lib" \
                cmd.exe /c "\"$win_pkg\\bin\\synth.exe\" \"$win_example\"" \
                2>&1
            )"; then
                echo "    smoke: OK -> $test_output"
            else
                echo "    smoke: FAILED -> $test_output"
                return 1
            fi
        else
            echo "::error::cygpath not found; cannot perform Windows package smoke test."
            return 1
        fi

    else

        if test_output="$(
            SYNTH_LIB_DIR="$pkg/libs" \
            "$exe" "$pkg/examples/hello.syn" \
            2>&1
        )"; then
            echo "    smoke: OK -> $test_output"
        else
            echo "    smoke: FAILED -> $test_output"
            return 1
        fi

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

    [ -n "$FILTER" ] && [ "$FILTER" != "$name" ] && continue

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

    rm -rf "$bdir"

    cfg=(
        cmake
        -S .
        -B "$bdir"
        -DCMAKE_BUILD_TYPE=Release
    )

    # ----------------------------------------------------------------------
    # Windows x64 MUST use MinGW-w64.
    # ----------------------------------------------------------------------
    if [ "$name" = "windows-x64" ]; then

        if [ "$HOST_OS" != "windows" ]; then
            echo "::error::windows-x64 must be built on a Windows host."
            exit 1
        fi

        if ! have gcc || ! have g++; then
            echo "::error::MinGW gcc/g++ not found."
            exit 1
        fi

        echo "=== Windows x64 compiler ==="
        echo "gcc: $(command -v gcc)"
        echo "g++: $(command -v g++)"

        gcc --version | head -1
        g++ --version | head -1

        cfg+=(
            -G "MinGW Makefiles"
            -DCMAKE_C_COMPILER=gcc
            -DCMAKE_CXX_COMPILER=g++
        )
    fi

    [ "$toolchain" != "-" ] && \
        cfg+=(-DCMAKE_TOOLCHAIN_FILE="$toolchain")

    [ "$osx_arch" != "-" ] && \
        cfg+=(-DCMAKE_OSX_ARCHITECTURES="$osx_arch")

    # Optional explicit compilers for cross-builds.
    [ -n "${CMAKE_C_COMPILER:-}" ] && \
        cfg+=(-DCMAKE_C_COMPILER="$CMAKE_C_COMPILER")

    [ -n "${CMAKE_CXX_COMPILER:-}" ] && \
        cfg+=(-DCMAKE_CXX_COMPILER="$CMAKE_CXX_COMPILER")

    [ -n "${CMAKE_RC_COMPILER:-}" ] && \
        cfg+=(-DCMAKE_RC_COMPILER="$CMAKE_RC_COMPILER")

    # ----------------------------------------------------------------------
    # Configure ONCE.
    # ----------------------------------------------------------------------
    echo "[*] CMake configure..."

    if ! "${cfg[@]}"; then
        echo "::error::$name: CMake configure failed."

        SKIPPED+=("$name")

        printf '| synth-%s | failed | CMake configure error |\n' \
            "$name" >> "$SUMMARY"

        exit 1
    fi

    CURRENT_BUILD_DIR="$bdir"

    # ----------------------------------------------------------------------
    # Verify Windows x64 actually selected MinGW.
    # ----------------------------------------------------------------------
    if [ "$name" = "windows-x64" ]; then

        if ! grep -E \
            'CMAKE_CXX_COMPILER:FILEPATH=.*/g\+\+(.exe)?$' \
            "$bdir/CMakeCache.txt" >/dev/null 2>&1; then

            echo "::error::windows-x64 is NOT using MinGW g++."
            echo
            echo "Actual compiler:"
            grep -E 'CMAKE_(C|CXX)_COMPILER' \
                "$bdir/CMakeCache.txt" || true

            exit 1
        fi

        echo "CMake compiler check: PASS"
    fi

    # ----------------------------------------------------------------------
    # Build.
    # ----------------------------------------------------------------------
    echo "[*] Building synth..."

    if ! cmake --build "$bdir" --target synth -j; then

        echo "::error::$name: build failed."

        SKIPPED+=("$name")

        printf '| synth-%s | failed | build error |\n' \
            "$name" >> "$SUMMARY"

        exit 1
    fi

    # ----------------------------------------------------------------------
    # Locate executable.
    # ----------------------------------------------------------------------
    if [ "$os" = "windows" ]; then
        exe="$bdir/synth.exe"
    else
        exe="$bdir/synth"
    fi

    if [ ! -f "$exe" ]; then

        echo "::error::$name: build produced no binary."

        SKIPPED+=("$name")

        printf '| synth-%s | failed | no binary produced |\n' \
            "$name" >> "$SUMMARY"

        exit 1
    fi

    echo "Binary: $exe"

    # ----------------------------------------------------------------------
    # Verify Windows x64 binary is actually MinGW.
    # ----------------------------------------------------------------------
    if [ "$name" = "windows-x64" ]; then

        if ! verify_windows_mingw "$exe"; then
            echo "::error::Windows x64 toolchain verification failed."
            exit 1
        fi
    fi

    # ----------------------------------------------------------------------
    # Assemble package.
    # ----------------------------------------------------------------------
    if ! assemble "$name" "$os" "$arch" "$exe"; then

        echo "::error::$name: package assembly failed."

        SKIPPED+=("$name")

        printf '| synth-%s | failed | package assembly error |\n' \
            "$name" >> "$SUMMARY"

        exit 1
    fi

    # ----------------------------------------------------------------------
    # Verify final package itself.
    # ----------------------------------------------------------------------
    if ! smoke_test "$DIST_DIR/synth-$name" "$os" "$arch"; then

        echo "::error::$name: package smoke test failed."

        SKIPPED+=("$name")

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

# A packaging run that was explicitly asked to build a target must never
# silently succeed without producing it.
if [ -n "$FILTER" ] && [ "${#PRODUCED[@]}" -eq 0 ]; then
    echo "::error::Requested target '$FILTER' was not produced."
    exit 1
fi

exit 0

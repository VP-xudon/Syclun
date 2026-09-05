#!/usr/bin/env bash
# ============================================================
# run_asserts.sh — build Synth-OOP and run the FULL assertion suite.
# 构建 Synth-OOP 并运行「全部」断言套件。
#
# This is the single source of truth for "does the code build and do all
# assertions pass". It is used by both:
#   - .github/workflows/ci.yml       (push / PR gate)
#   - .github/workflows/release.yml  (release verification gate)
#
# Design note: the script ALWAYS exits 0 after writing its report. The CALLER
# inspects `build/verify-artifacts/assert-report.json` (field `all_passed`) to
# decide pass/fail. This lets CI hard-fail while the release gate can choose to
# downgrade to a warning when `allow_defects` is set.
# 设计说明：脚本写完报告后总是以 0 退出；由调用方读取报告中的 `all_passed`
# 字段决定通过/失败。这样 CI 可硬性失败，而 Release 门禁在 allow_defects
# 时可降级为警告。
# ============================================================
set -o pipefail

cd "$(dirname "$0")/../.."            # repo root / 仓库根
ROOT="$(pwd)"

# host detection + executable extension
# 宿主识别与可执行文件扩展名
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*|Windows_NT) EXT=".exe" ;;
  *) EXT="" ;;
esac

ART_DIR="${ARTIFACT_DIR:-build/verify-artifacts}"
mkdir -p "$ART_DIR"

# ---- build -------------------------------------------------------------
echo "::group::configure + build"
bash build.sh
BUILD_RC=$?
echo "::endgroup::"

if [ "$BUILD_RC" -ne 0 ]; then
  echo "::error::build failed (rc=$BUILD_RC)"
  cat > "$ART_DIR/assert-report.json" <<JSON
{
  "generated_at": "$(date -u '+%Y-%m-%dT%H:%M:%SZ')",
  "host": "$(uname -s)",
  "build_ok": false,
  "all_passed": 0,
  "total_passed": 0,
  "total_failed": 0,
  "suites": {},
  "examples": {},
  "defects": ["build failed (rc=$BUILD_RC)"]
}
JSON
  {
    echo "# Synth-OOP Verification Report"
    echo
    echo "Generated $(date -u '+%Y-%m-%d %H:%M UTC') on host $(uname -s)."
    echo
    echo "## Result: BUILD FAILED ❌"
    echo
    echo "The project did not compile; no assertions were run."
  } > "$ART_DIR/assert-report.md"
  exit 0
fi

# ---- run every assertion suite -----------------------------------------
# Three regression suites are the authoritative "asserts":
#   lexer (assert_lexer) / parser (assert_parser) / runtimes (assert_runtimes)
# 三套回归套件即权威「断言」：词法 / 语法 / 运行期。
# The CMake targets are `assert_<name>`; report labels drop the prefix.
# CMake 目标名为 `assert_<name>`，报告标签去掉前缀。
SUITES="assert_lexer assert_parser assert_runtimes"
TOTAL_PASS=0
TOTAL_FAIL=0
ALL_OK=1
DEFECTS=()

{
  echo "# Synth-OOP Verification Report"
  echo
  echo "Generated $(date -u '+%Y-%m-%d %H:%M UTC') on host $(uname -s)."
  echo
  echo "## Assertion suites / 断言套件"
  echo
  echo "| Suite | Passed | Failed | Result |"
  echo "|-------|--------|--------|--------|"
} > "$ART_DIR/assert-report.md"

JSON_SUITES=""

for s in $SUITES; do
  label="${s#assert_}"
  out="$ART_DIR/assert_$label.out"
  build/$s$EXT > "$out" 2>&1
  rc=$?
  p=""; f=""
  case "$label" in
    lexer)
      p=$(grep -E '^  passed:' "$out" | sed -E 's/[^0-9]//g')
      f=$(grep -E '^  failed:' "$out" | sed -E 's/[^0-9]//g')
      ;;
    parser)
      line=$(grep -E 'PASSED [0-9]+ / FAILED [0-9]+' "$out")
      p=$(echo "$line" | sed -E 's/.*PASSED ([0-9]+).*/\1/')
      f=$(echo "$line" | sed -E 's/.*FAILED ([0-9]+).*/\1/')
      ;;
    runtimes)
      line=$(grep -E '[0-9]+ passed, [0-9]+ failed' "$out")
      p=$(echo "$line" | sed -E 's/([0-9]+) passed.*/\1/')
      f=$(echo "$line" | sed -E 's/.*, ([0-9]+) failed.*/\1/')
      ;;
  esac
  [ -z "$p" ] && p=0
  [ -z "$f" ] && f=0
  TOTAL_PASS=$((TOTAL_PASS + p))
  TOTAL_FAIL=$((TOTAL_FAIL + f))
  ok=1
  if [ "$rc" -ne 0 ] || [ "$f" -ne 0 ]; then ok=0; ALL_OK=0; DEFECTS+=("$label suite failed (passed=$p failed=$f rc=$rc)"); fi
  res="PASS"; [ "$ok" -eq 0 ] && res="FAIL"
  if [ "$ok" -eq 0 ]; then echo "::error::assert suite '$label' FAILED (passed=$p failed=$f)"; else echo "::notice::assert suite '$label' passed ($p/$((p+f)))"; fi
  echo "| $label | $p | $f | $res |" >> "$ART_DIR/assert-report.md"
  JSON_SUITES="$JSON_SUITES \"$label\": {\"passed\": $p, \"failed\": $f, \"ok\": $ok},"
done

# ---- example smoke (informational; not a gating assert) ----------------
# 示例冒烟（仅作信息，不计入门禁断言）。
EXAMPLES="hello counter flow"
{
  echo
  echo "## Example programs (runtime smoke) / 示例程序冒烟"
  echo
  echo "| Example | Result |"
  echo "|---------|--------|"
} >> "$ART_DIR/assert-report.md"
JSON_EXAMPLES=""
for ex in $EXAMPLES; do
  if build/synth$EXT "examples/$ex.syn" > "$ART_DIR/ex-$ex.out" 2>&1; then
    echo "| $ex | OK |" >> "$ART_DIR/assert-report.md"
    JSON_EXAMPLES="$JSON_EXAMPLES \"$ex\": true,"
  else
    echo "| $ex | FAIL |" >> "$ART_DIR/assert-report.md"
    JSON_EXAMPLES="$JSON_EXAMPLES \"$ex\": false,"
    echo "::warning::example '$ex.syn' failed (non-gating smoke)"
  fi
done

# ---- result block ------------------------------------------------------
{
  echo
  if [ "$ALL_OK" -eq 1 ]; then
    echo "## Result: ALL ASSERTIONS PASSED ✅"
  else
    echo "## Result: DEFECTS RECORDED ❌"
    echo
    echo "Defects / 缺陷："
    if [ "${#DEFECTS[@]}" -gt 0 ]; then
      for d in "${DEFECTS[@]}"; do echo "- $d"; done
    fi
  fi
  echo
  echo "Totals: $TOTAL_PASS passed, $TOTAL_FAIL failed."
} >> "$ART_DIR/assert-report.md"

# ---- machine-readable report -------------------------------------------
{
  echo "{"
  echo "  \"generated_at\": \"$(date -u '+%Y-%m-%dT%H:%M:%SZ')\","
  echo "  \"host\": \"$(uname -s)\","
  echo "  \"build_ok\": true,"
  echo "  \"all_passed\": $ALL_OK,"
  echo "  \"total_passed\": $TOTAL_PASS,"
  echo "  \"total_failed\": $TOTAL_FAIL,"
  echo "  \"suites\": { ${JSON_SUITES%,} },"
  echo "  \"examples\": { ${JSON_EXAMPLES%,} },"
  echo "  \"defects\": ["
  first=1
  if [ "${#DEFECTS[@]}" -gt 0 ]; then
    for d in "${DEFECTS[@]}"; do
      if [ $first -eq 1 ]; then first=0; else echo ","; fi
      echo "    \"$d\""
    done
  fi
  echo "  ]"
  echo "}"
} > "$ART_DIR/assert-report.json"

echo "::group::assert-report.json"
cat "$ART_DIR/assert-report.json"
echo "::endgroup::"
exit 0

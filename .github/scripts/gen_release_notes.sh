#!/usr/bin/env bash
# ============================================================
# gen_release_notes.sh — assemble a FORMATTED release body from the
# assertion report + produced packages + changelog. Pure bash, no external
# dependencies. Replaces the previously hand-written release body.
# 从断言报告 + 产物包 + 变更日志拼装「格式化」的 Release 说明（纯 bash，
# 无外部依赖）。取代原先手写的 Release 正文。
#
# Inputs (environment) / 输入（环境变量）：
#   VERSION       release version / tag, e.g. v1.31.0
#   ART_DIR       directory holding assert-report.{json,md}
#   ASSETS_DIR    directory holding the *.zip + assets/*.vsix to list
#   CHANGELOG     markdown bullet list of changes (may be empty)
#   OUT           output file (default: release-notes.md)
# ============================================================
set -o pipefail

ART_DIR="${ART_DIR:-build/verify-artifacts}"
VERSION="${VERSION:-unknown}"
CHANGELOG="${CHANGELOG:-}"
OUT="${OUT:-release-notes.md}"
ASSETS_DIR="${ASSETS_DIR:-.}"

now="$(date -u '+%Y-%m-%d %H:%M UTC')"

# ---- package / asset listing -------------------------------------------
pkgs=""
for z in "$ASSETS_DIR"/*.zip; do
  [ -e "$z" ] || continue
  bn="$(basename "$z")"
  pkgs="$pkgs| \`$bn\` |\n"
done
vsix=""
if [ -d "$ASSETS_DIR/assets" ]; then
  for v in "$ASSETS_DIR/assets"/*.vsix; do
    [ -e "$v" ] || continue
    bn="$(basename "$v")"
    vsix="$vsix| \`$bn\` |\n"
  done
fi

# ---- assertion summary --------------------------------------------------
assert_block=""
if [ -f "$ART_DIR/assert-report.md" ]; then
  assert_block="$(cat "$ART_DIR/assert-report.md")"
fi

all_passed="unknown"
if [ -f "$ART_DIR/assert-report.json" ]; then
  grep -q '"all_passed": 1' "$ART_DIR/assert-report.json" 2>/dev/null && all_passed="PASSED ✅"
  grep -q '"all_passed": 0' "$ART_DIR/assert-report.json" 2>/dev/null && all_passed="DEFECTS ❌"
fi

# ---- assemble -----------------------------------------------------------
{
  echo "# Synth-OOP (Syclun) $VERSION"
  echo
  echo "_Released $now · verification: **$all_passed**_"
  echo
  echo "Prebuilt Syclun (Synth-OOP) interpreter distributions, plus the VS Code"
  echo "syntax-highlighting extension."
  echo
  echo "预构建的 Syclun（Synth-OOP）解释器发行包，外加 VS Code 语法高亮插件。"
  echo
  echo "## Verification / 验证"
  echo
  if [ -n "$assert_block" ]; then
    echo "$assert_block"
  else
    echo "_assertion report unavailable / 断言报告缺失_"
  fi
  echo
  echo "## Packages / 分发包"
  echo
  echo "| Asset |"
  echo "|-------|"
  printf "%b" "$pkgs"
  printf "%b" "$vsix"
  echo
  echo "Each zip unpacks to \`synth-<target>/\` containing \`bin/synth(.exe)\`,"
  echo "\`libs/*.synl\`, \`examples/\`, and docs."
  echo
  echo "每个 zip 解压出 \`synth-<target>/\`，内含 \`bin/synth(.exe)\`、\`libs/*.synl\`、\`examples/\` 与文档。"
  echo
  if [ -n "$CHANGELOG" ]; then
    echo "## What changed / 变更"
    echo
    echo "$CHANGELOG"
    echo
  fi
  echo "---"
  echo "Built from source via \`package.sh\`; verification produced by \`run_asserts.sh\`."
  echo "Built from source via \`package.sh\`；验证由 \`run_asserts.sh\` 生成。GPL-3.0-or-later。"
} > "$OUT"

echo "wrote $OUT"
